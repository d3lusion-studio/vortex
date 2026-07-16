#include "assets.hpp"

#include "vortex/app/app.hpp"
#include "vortex/asset/image.hpp"
#include "vortex/core/log.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/rhi/device.hpp"

#include <filesystem>
#include <string>
#include <vector>

#ifndef VORTEX_FARM_ASSET_DIR
#define VORTEX_FARM_ASSET_DIR "assets/2d/FarmRPG"
#endif

namespace farm {

using namespace vortex;

namespace {

const std::filesystem::path kRoot = VORTEX_FARM_ASSET_DIR;

[[nodiscard]] std::string path(const std::string& relative) {
    return (kRoot / relative).string();
}

[[nodiscard]] assets::Image loadImage(pf::IFileSystem& fs, const std::string& relative) {
    const std::string        full  = path(relative);
    const std::vector<std::byte> bytes = fs.readFile(full.c_str());
    if (bytes.empty()) {
        VORTEX_ERROR("Farm", "Missing art: %s", full.c_str());
        return {};
    }
    assets::Image image = assets::decodeImage(bytes.data(), bytes.size());
    if (!image.valid()) VORTEX_ERROR("Farm", "Undecodable art: %s", full.c_str());
    return image;
}

// Build one animation sheet of the player: the doll layers in draw order, plus the
// tool the action swings, flattened into a single page.
[[nodiscard]] rhi::TextureHandle buildCharacterSheet(app::App& app, const CharacterLook& look,
                                                     const std::string& actionName,
                                                     const std::string& weapon,
                                                     u32& widthOut) {
    pf::IFileSystem& fs = app.fileSystem();

    const std::string action = "Character/Character/PNG/" + actionName;

    assets::Image sheet = loadImage(fs, action + "/Skins/" + std::to_string(look.skin) + ".png");
    if (!sheet.valid()) return {};

    const std::string layers[] = {
        action + "/Clothers/Farm/" + look.clothes + ".png",
        action + "/Eyes/" + look.eyesSex + "/" + look.eyesColor + ".png",
        action + "/Hair's/" + look.hair + "/" + look.hairColor + ".png",
    };
    for (const std::string& layer : layers) assets::compositeOver(sheet, loadImage(fs, layer));

    if (!weapon.empty())
        assets::compositeOver(sheet, loadImage(fs, action + "/Weapons/" + weapon + "/" +
                                                     std::to_string(look.toolTint) + ".png"));

    widthOut = sheet.width;
    return app.device().createTexture(
        {.width = sheet.width, .height = sheet.height, .debugName = "farm_character"},
        sheet.pixels.data());
}

// Register one clip per facing off a single-row sheet. The sheets store the four
// facings back to back in Dir order, so a facing is just an offset into the strip.
[[nodiscard]] DirClips addDirClips(ecs::Scene& scene, const renderer::SpriteSheet& sheet,
                                   u32 framesPerDir, f32 fps, bool loop) {
    DirClips clips;
    for (u32 d = 0; d < 4; ++d)
        clips.byDir[d] = scene.animations.addFromSheet(sheet, d * framesPerDir, framesPerDir,
                                                       fps, loop);
    return clips;
}

[[nodiscard]] renderer::SpriteSheet charSheet(rhi::TextureHandle texture, u32 width) {
    return {.texture       = texture,
            .textureWidth  = width,
            .textureHeight = static_cast<u32>(kCharPx),
            .columns       = width / static_cast<u32>(kCharPx),
            .rows          = 1};
}

// Read a crop sheet instead of describing it. The pack's convention is: growth
// stages from frame 0, a gap, then the harvested item's icon last. Finding the gap
// is what tells us how many stages this particular crop has.
[[nodiscard]] bool probeCropArt(app::App& app, const CropDef& def, CropArt& out) {
    const assets::Image image = loadImage(app.fileSystem(), std::string("Crops/") + def.path);
    if (!image.valid()) return false;

    const u32 frames = image.width / kTilePx;
    if (frames == 0) return false;

    std::vector<bool> occupied(frames, false);
    for (u32 f = 0; f < frames; ++f) {
        for (u32 y = 0; y < image.height && !occupied[f]; ++y) {
            for (u32 x = f * kTilePx; x < (f + 1) * kTilePx; ++x) {
                if (image.pixels[(static_cast<usize>(y) * image.width + x) * 4 + 3] > 128) {
                    occupied[f] = true;
                    break;
                }
            }
        }
    }

    u32 lastUsed = 0;
    for (u32 f = 0; f < frames; ++f)
        if (occupied[f]) lastUsed = f;

    // The icon is the last drawn frame. Walk back over the gap that separates it from
    // the growth strip; whatever the gap ends on is the mature stage.
    //
    // Counting FORWARD from frame 0 instead would be wrong, and quietly: Pumpkin's
    // second stage is blank (the artist drew nothing between seed and sprout), so a
    // forward scan stops at frame 1 and the crop never grows past a single frame.
    i32 mature = static_cast<i32>(lastUsed) - 1;
    while (mature >= 0 && !occupied[static_cast<usize>(mature)]) --mature;
    const u32 stages = static_cast<u32>(mature + 1);

    if (stages == 0) {
        VORTEX_ERROR("Farm", "Crop sheet %s has no growth frames", def.path);
        return false;
    }

    const renderer::SpriteSheet sheet{.texture       = app.loadTexture(path(std::string("Crops/") + def.path).c_str()),
                                      .textureWidth  = image.width,
                                      .textureHeight = image.height,
                                      .columns       = frames,
                                      .rows          = 1};
    if (!sheet.texture.valid()) return false;

    out.texture = sheet.texture;
    out.stageUV.clear();
    for (u32 f = 0; f < stages; ++f) out.stageUV.push_back(sheet.frameUV(f));
    out.itemUV    = sheet.frameUV(lastUsed);
    out.frameSize = sheet.cellSize();

    VORTEX_INFO("Farm", "%-12s %u growth stages, icon frame %u", def.name, stages, lastUsed);
    return true;
}

// The tilled-soil blob table, DERIVED from the sheet rather than transcribed off it.
//
// Each cell of the module was asked which neighbours it was drawn to meet, by reading
// whether its edge pixels are the fill colour or the dark outline. Alpha cannot answer
// that here — tilled soil is opaque right out to its border — which is why the scan goes
// by colour.
//
// Two things make the result trustworthy, and both had to be learnt the hard way:
//
//   * Skip the module's one BLANK cell before matching. It has no fill on any edge, so it
//     reads as "isolated" and claims mask 0 ahead of the real lone-tile cell — after which
//     a single hoed tile draws nothing at all, silently.
//   * Then check the count: all 47 reachable masks come back exactly once, none missing.
//     That is what says the layout really is a 47-blob and the scan really read it.
//
// To re-derive after a pack update, re-run that scan; do not hand-edit this.
constexpr u8 kSoilBlobCells[256] = {
    36, 24, 37, 25,  0, 12,  1, 13, 39, 27, 38, 26,  3, 15,  2, 14,
    36, 24, 37, 44,  0, 12,  1, 28, 39, 27, 38, 41,  3, 15,  2,  7,
    36, 24, 37, 25,  0, 12,  8, 16, 39, 27, 38, 26,  3, 15,  5, 43,
    36, 24, 37, 44,  0, 12,  8, 20, 39, 27, 38, 41,  3, 15,  5, 32,
    36, 24, 37, 25,  0, 12,  1, 13, 39, 27, 38, 26, 11, 19,  6, 40,
    36, 24, 37, 44,  0, 12,  1, 28, 39, 27, 38, 41, 11, 19,  6, 21,
    36, 24, 37, 25,  0, 12,  8, 16, 39, 27, 38, 26, 11, 19, 10,  9,
    36, 24, 37, 44,  0, 12,  8, 20, 39, 27, 38, 41, 11, 19, 10, 17,
    36, 24, 37, 25,  0, 12,  1, 13, 39, 47, 38, 42,  3, 31,  2,  4,
    36, 24, 37, 44,  0, 12,  1, 28, 39, 47, 38, 45,  3, 31,  2, 46,
    36, 24, 37, 25,  0, 12,  8, 16, 39, 47, 38, 42,  3, 31,  5, 34,
    36, 24, 37, 44,  0, 12,  8, 20, 39, 47, 38, 45,  3, 31,  5, 29,
    36, 24, 37, 25,  0, 12,  1, 13, 39, 47, 38, 42, 11, 35,  6, 23,
    36, 24, 37, 44,  0, 12,  1, 28, 39, 47, 38, 45, 11, 35,  6, 30,
    36, 24, 37, 25,  0, 12,  8, 16, 39, 47, 38, 42, 11, 35, 10, 18,
    36, 24, 37, 44,  0, 12,  8, 20, 39, 47, 38, 45, 11, 35, 10, 33,
};

const char* kSeasonGround[4] = {
    "Tileset/Tileset Grass Spring.png",
    "Tileset/Tileset Grass Summer.png",
    "Tileset/Tileset Grass Fall.png",
    "Tileset/Tileset Grass Winter.png",
};

}   // namespace

renderer::BlobSet soilBlobSet() {
    // The dry-soil module is the 12x4 block at the top-left of a 24-wide page (the wet
    // recolour sits below it, and a second pair to the right). firstCell is 1 because
    // tile id 0 means "empty", so the page's first cell is id 1.
    return {.cells         = kSoilBlobCells,
            .firstCell     = 1,
            .moduleColumns = 12,
            .pageColumns   = 24};
}

bool loadAssets(app::App& app, ecs::Scene& scene, const CharacterLook& look, Assets& out) {
    // --- Ground, one page per season ------------------------------------------
    for (i32 s = 0; s < 4; ++s) {
        out.groundTex[s] = app.loadTexture(path(kSeasonGround[s]).c_str());
        if (!out.groundTex[s].valid()) {
            VORTEX_ERROR("Farm", "Could not load %s", kSeasonGround[s]);
            return false;
        }
        out.groundSheet[s] = {.texture       = out.groundTex[s],
                              .textureWidth  = 384,
                              .textureHeight = 640,
                              .columns       = 24,
                              .rows          = 40};
    }

    out.soilTex = app.loadTexture(path("Tileset/Tilled Soil and wet soil.png").c_str());
    if (!out.soilTex.valid()) return false;
    out.soilSheet = {.texture       = out.soilTex,
                     .textureWidth  = 384,
                     .textureHeight = 128,
                     .columns       = 24,
                     .rows          = 8};

    out.propsTex = app.loadTexture(path("Tileset/ALL props seasons.png").c_str());
    if (!out.propsTex.valid()) return false;
    out.propsSheet = {.texture       = out.propsTex,
                      .textureWidth  = 352,
                      .textureHeight = 192,
                      .columns       = kPropsColumns,
                      .rows          = 12};

    // --- Props -----------------------------------------------------------------
    out.treeTex  = app.loadTexture(path("Objects/Tree/Common/Shadow/Maple Tree.png").c_str());
    out.treeSize = {32.0f, 48.0f};
    out.houseTex = app.loadTexture(path("Objects/Exterior/Houses/Tiny House.png").c_str());
    out.houseSize = {72.0f, 96.0f};

    // The bin sprite sits in the lower half of its 16x32 cell, so the rect is the
    // art rather than the cell — anchoring to the cell would float it a tile up.
    out.binTex = app.loadTexture(path("Objects/Exterior/shipping box.png").c_str());
    out.binUV  = renderer::pixelsToUV({0.0f, 16.0f, 16.0f, 16.0f}, 48, 64);

    // Tool icons are 32x16 sheets of two 16px frames; the first is the item itself.
    const char* toolIcons[kToolLast + 1] = {nullptr, "Hoe.png", "Watering can.png", "Sickle.png"};
    for (i32 t = kToolFirst; t <= kToolLast; ++t) {
        out.toolIcon[t] = app.loadTexture(
            path(std::string("Icons/RPG icons/Weapons and Armor/1. Wood/") + toolIcons[t]).c_str());
        if (!out.toolIcon[t].valid()) return false;
    }
    out.toolIconUV = renderer::pixelsToUV({0.0f, 0.0f, 16.0f, 16.0f}, 32, 16);

    // --- UI --------------------------------------------------------------------
    //
    // button.png stacks eleven 48x48 panels in one column, 48 apart from y=16 — found by
    // measuring the sheet, since the panels touch and no alpha gap separates them. Each
    // is the same rounded widget in a different colour, so the HUD's whole palette is
    // five windows onto one page and one draw call.
    out.buttonTex = app.loadTexture(path("UI/button.png").c_str());
    if (!out.buttonTex.valid()) return false;

    const auto panelSkin = [&](i32 row) {
        constexpr f32 kCell  = 48.0f;
        constexpr f32 kInset = 6.0f;   // the rounded corner, measured off the art
        return ui::Skin{
            .texture = out.buttonTex,
            .uv      = renderer::pixelsToUV({kCell, 16.0f + static_cast<f32>(row) * kCell,
                                             kCell, kCell}, 848, 544),
            .slice   = {.left = kInset, .top = kInset, .right = kInset, .bottom = kInset,
                        .sourcePixels = {kCell, kCell}}};
    };

    out.panelSkin       = panelSkin(2);    // light wood
    out.slotSkin        = panelSkin(0);    // the recess an item sits in
    out.slotPickedSkin  = panelSkin(8);    // that recess, lit
    out.buttonSkin      = panelSkin(1);
    out.buttonGreenSkin = panelSkin(4);

    out.barsTex = app.loadTexture(path("UI/Bars.png").c_str());
    if (!out.barsTex.valid()) return false;
    // The empty frame capped with a lightning bolt, and the green strip that fills it.
    out.barFrameUV = renderer::pixelsToUV({147.0f, 101.0f, 42.0f, 9.0f}, 192, 160);
    out.barFillUV  = renderer::pixelsToUV({54.0f, 101.0f, 27.0f, 9.0f}, 192, 160);
    out.boltUV     = renderer::pixelsToUV({3.0f, 101.0f, 42.0f, 9.0f}, 192, 160);

    out.iconTex = app.loadTexture(path("UI/HUD.png").c_str());
    if (!out.iconTex.valid()) return false;
    out.coinUV = renderer::pixelsToUV({64.0f, 80.0f, 16.0f, 16.0f}, 416, 96);

    // --- Crops -----------------------------------------------------------------
    out.crops.clear();
    for (const CropDef& def : cropDefs()) {
        CropArt art;
        if (!probeCropArt(app, def, art)) return false;
        out.crops.push_back(std::move(art));
    }

    // --- The player ------------------------------------------------------------
    struct ActionSpec {
        const char* action;
        const char* weapon;
        u32         framesPerDir;
        f32         fps;
        bool        loop;
        DirClips*   clips;
        f32*        duration;
    };

    const ActionSpec specs[] = {
        {"1. Idle",                              "",         4,  6.0f, true,  &out.clips.idle,   nullptr},
        {"2. Walk",                              "",         6, 10.0f, true,  &out.clips.walk,   nullptr},
        {"3. Run",                               "",         8, 14.0f, true,  &out.clips.run,    nullptr},
        {"4. Pickaxe, Hoe and Catching insects", "Hoe",      6, 12.0f, false, &out.clips.hoe,    &out.clips.hoeDuration},
        {"7. Watering",                          "Watering", 8, 14.0f, false, &out.clips.water,  &out.clips.waterDuration},
        {"5. Axe and Sickle",                    "Sickle",   6, 12.0f, false, &out.clips.sickle, &out.clips.sickleDuration},
    };

    for (const ActionSpec& spec : specs) {
        u32                      width   = 0;
        const rhi::TextureHandle texture = buildCharacterSheet(app, look, spec.action,
                                                               spec.weapon, width);
        if (!texture.valid()) {
            VORTEX_ERROR("Farm", "Could not build character sheet for '%s'", spec.action);
            return false;
        }
        const renderer::SpriteSheet sheet = charSheet(texture, width);
        if (sheet.columns != spec.framesPerDir * 4) {
            VORTEX_WARN("Farm", "'%s' has %u frames, expected %u", spec.action, sheet.columns,
                        spec.framesPerDir * 4);
        }
        *spec.clips = addDirClips(scene, sheet, spec.framesPerDir, spec.fps, spec.loop);
        if (spec.duration != nullptr)
            *spec.duration = static_cast<f32>(spec.framesPerDir) / spec.fps;
    }

    return true;
}

}
