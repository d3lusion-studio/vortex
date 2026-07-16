// Turning the FarmRPG art pack into things the renderer can use.
//
// Two jobs here are less obvious than "load a png":
//
//   * The player is a paper doll. Every character animation ships as separate skin,
//     clothes, eyes, hair and tool layers that have to be composited before upload,
//     so the look is chosen once here and costs nothing per frame afterwards.
//
//   * Crop sheets are read, not described. The pack lays a crop out as its growth
//     stages, a gap, then the harvested item's icon — but the stage COUNT varies per
//     crop. Scanning the alpha finds the split, which beats hand-copying ten numbers
//     that would silently rot the first time the pack updates.
#pragma once

#include "farm.hpp"

#include "vortex/renderer/sprite_animation.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/renderer/tilemap.hpp"
#include "vortex/rhi/rhi_handle.hpp"
#include "vortex/ui/ui.hpp"

#include <string>
#include <vector>

namespace vortex::app { class App; }
namespace vortex::ecs { class Scene; }

namespace farm {

// Which paper-doll layers to composite. Every value is a directory or file name in
// the pack, so this is the whole character creator.
struct CharacterLook {
    i32         skin      = 1;          // Skins/1..4.png
    std::string clothes   = "Blue";     // Clothers/Farm/*.png
    std::string eyesSex   = "Male";     // Eyes/{Male,Female}
    std::string eyesColor = "Blue";
    std::string hair      = "Standard"; // Hair's/*
    std::string hairColor = "Brown";
    i32         toolTint  = 1;          // Weapons/<tool>/1..10.png
};

// One clip per facing, indexed by Dir.
struct DirClips {
    renderer::AnimationHandle byDir[4]{};
};

struct CharacterClips {
    DirClips idle;
    DirClips walk;
    DirClips run;
    DirClips hoe;
    DirClips water;
    DirClips sickle;

    // Seconds a swing locks the player for, taken from the clip length so the
    // animation and the gameplay lock cannot drift apart.
    f32 hoeDuration    = 0.6f;
    f32 waterDuration  = 0.6f;
    f32 sickleDuration = 0.6f;
};

struct Assets {
    // Ground, one tileset page per season.
    rhi::TextureHandle    groundTex[4]{};
    renderer::SpriteSheet groundSheet[4]{};

    rhi::TextureHandle    soilTex{};
    renderer::SpriteSheet soilSheet{};

    // Grass tufts and flowers, scattered over the lawn. Rows 0..3 are the same props
    // in Spring/Summer/Fall/Winter dress, which is what lets the decor follow the
    // calendar without a second page.
    rhi::TextureHandle    propsTex{};
    renderer::SpriteSheet propsSheet{};

    std::vector<CropArt> crops;

    // Props drawn as plain sprites on top of the ground.
    rhi::TextureHandle treeTex{};
    Vec2               treeSize{};
    rhi::TextureHandle houseTex{};
    Vec2               houseSize{};
    rhi::TextureHandle binTex{};
    Rect               binUV = kFullUV;

    // Inventory icons for the tools, indexed by ItemId (kToolHoe..kToolScythe).
    rhi::TextureHandle toolIcon[kToolLast + 1]{};
    Rect               toolIconUV = kFullUV;

    // --- UI ---------------------------------------------------------------------
    //
    // The pack's own widgets, as 9-patches. `panelSkin` is the light wood the HUD is
    // framed in, `slotSkin` the recess an item sits in, `slotPickedSkin` the same recess
    // lit up. All three are windows onto one page, so the whole HUD is one draw call.
    rhi::TextureHandle buttonTex{};
    ui::Skin           panelSkin;
    ui::Skin           slotSkin;
    ui::Skin           slotPickedSkin;
    ui::Skin           buttonSkin;
    ui::Skin           buttonGreenSkin;

    // Bars.png: the frame the energy bar sits in, its fill, and the bolt that captions it.
    rhi::TextureHandle barsTex{};
    Rect               barFrameUV = kFullUV;
    Rect               barFillUV  = kFullUV;
    Rect               boltUV     = kFullUV;

    // The coin, for the money readout.
    rhi::TextureHandle iconTex{};
    Rect               coinUV = kFullUV;

    CharacterClips clips;

    [[nodiscard]] bool ok() const { return groundTex[0].valid() && soilTex.valid(); }
};

// Loads every texture, composites the character, and registers the animation clips
// into `scene`. Returns false (and logs) if the art pack is not where it should be.
bool loadAssets(vortex::app::App& app, vortex::ecs::Scene& scene, const CharacterLook& look,
                Assets& out);

// The tile id of the solid, fully-grass cell of a season's ground page, and of the
// solid tilled-soil cell. Both were found by scanning the sheets for the only fully
// opaque, uniform cell of their autotile block.
constexpr renderer::TileId kGrassTile = 1 + 57;   // (9, 2) of a 24-wide page

// Tilled soil is a 47-blob autotile: a 12x4 module at the top-left of its page. Its
// mask->cell table is derived from the art in assets.cpp; this is the set that binds
// that table to the page.
[[nodiscard]] renderer::BlobSet soilBlobSet();

// The props page is 22 wide; row = season, and the first six columns of each row are
// the tufts and flowers worth scattering on a lawn.
constexpr u32 kPropsColumns   = 22;
constexpr u32 kDecorVariants  = 6;
[[nodiscard]] constexpr renderer::TileId decorTile(Season season, u32 variant) {
    return 1 + static_cast<renderer::TileId>(static_cast<u32>(season) * kPropsColumns + variant);
}

}
