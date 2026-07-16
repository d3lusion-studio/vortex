#include "hud.hpp"

#include "vortex/app/app.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/text/font.hpp"
#include "vortex/text/text_renderer.hpp"
#include "vortex/ui/ui.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace farm {

using namespace vortex;

namespace {

// HUD draw order, above the world's depth-sorted sprites and below the text.
constexpr i32 kLayerPanel = 1000;
constexpr i32 kLayerIcon  = 1010;
constexpr i32 kLayerText  = 1200;

// The night fade has to cover the HUD too, text included — a fade that only swallows
// the panels leaves the clock hanging in the dark.
constexpr i32 kLayerFade     = 1300;
constexpr i32 kLayerFadeText = 1400;

constexpr f32 kSlotSize    = 44.0f;   // screen pixels
constexpr f32 kSlotSpacing = 4.0f;

const Color kTextColor  = Color::fromRgb(0xF4E4C1);
// On the pack's light wood panels, cream-on-cream is unreadable; these sit on the art.
const Color kPanelText  = Color::fromRgb(0x3B2416);
const Color kMoneyColor = Color::fromRgb(0x7A4A12);

// A flat quad. The batch onUi hands us is already in framebuffer pixels with the origin
// at the centre, so a HUD coordinate IS a batch coordinate — there is nothing to convert.
void hudRect(HudContext& ctx, Vec2 center, Vec2 size, Color color, i32 layer) {
    ctx.batch.draw({.position = center,
                    .size     = size,
                    .color    = color,
                    .texture  = ctx.app.whiteTexture(),
                    .layer    = layer});
}

// A panel from the pack's art. Nine quads instead of one, so the rounded corner keeps
// its radius however far the panel is stretched.
void hudPanel(HudContext& ctx, Vec2 center, Vec2 size, const ui::Skin& skin, i32 layer,
              Color tint = Color::white()) {
    ctx.batch.drawNineSlice({.position = center,
                             .size     = size,
                             .color    = tint,
                             .uv       = skin.uv,
                             .texture  = skin.texture,
                             .layer    = layer,
                             .sampler  = renderer::SpriteSampler::NearestClamp},
                            skin.slice);
}

void hudSprite(HudContext& ctx, Vec2 center, Vec2 size, rhi::TextureHandle texture, Rect uv,
               i32 layer, Color color = Color::white()) {
    if (!texture.valid()) return;
    ctx.batch.draw({.position = center,
                    .size     = size,
                    .color    = color,
                    .uv       = uv,
                    .texture  = texture,
                    .layer    = layer,
                    .sampler  = renderer::SpriteSampler::NearestClamp});
}

// Text positioned by its top-left.
void hudText(HudContext& ctx, std::string_view s, Vec2 topLeft, Color color = kTextColor,
             i32 layer = kLayerText) {
    text::drawText(ctx.batch, ctx.font, s, topLeft, color, 1.0f, layer);
}

void hudTextCentered(HudContext& ctx, std::string_view s, Vec2 center, Color color = kTextColor,
                     i32 layer = kLayerText) {
    const Vec2 measured = ctx.font.measure(s);
    hudText(ctx, s, {center.x - measured.x * 0.5f, center.y + measured.y * 0.5f}, color, layer);
}

// The viewport, in the overlay's own units.
[[nodiscard]] Vec2 viewport(HudContext& ctx) {
    return {static_cast<f32>(ctx.app.camera().viewportWidth),
            static_cast<f32>(ctx.app.camera().viewportHeight)};
}

// What an item looks like in a slot. Seeds reuse the crop's first growth frame —
// which is exactly what the pack draws there: the seeds going into the ground.
struct ItemIcon {
    rhi::TextureHandle texture;
    Rect               uv;
};

[[nodiscard]] ItemIcon iconFor(const Assets& assets, ItemId id) {
    if (isTool(id)) return {assets.toolIcon[id], assets.toolIconUV};
    if (isSeed(id)) {
        const CropArt& art = assets.crops[static_cast<usize>(cropOfSeed(id))];
        return {art.texture, art.stageUV.front()};
    }
    if (isProduce(id)) {
        const CropArt& art = assets.crops[static_cast<usize>(cropOfProduce(id))];
        return {art.texture, art.itemUV};
    }
    return {};
}

[[nodiscard]] std::string itemName(ItemId id) {
    if (id == kToolHoe) return "Hoe";
    if (id == kToolCan) return "Watering Can";
    if (id == kToolScythe) return "Sickle";
    if (isSeed(id)) return std::string(cropDefs()[static_cast<usize>(cropOfSeed(id))].name) + " Seeds";
    if (isProduce(id)) return cropDefs()[static_cast<usize>(cropOfProduce(id))].name;
    return "";
}

void drawHotbar(HudContext& ctx) {
    const f32 halfH  = viewport(ctx).y * 0.5f;
    const f32 stride = kSlotSize + kSlotSpacing;
    const f32 startX = -(kHotbarSlots - 1) * stride * 0.5f;
    const f32 y      = -halfH + kSlotSize * 0.5f + 14.0f;

    hudPanel(ctx, {0.0f, y}, {kHotbarSlots * stride + 16.0f, kSlotSize + 16.0f},
             ctx.assets.panelSkin, kLayerPanel);

    for (i32 i = 0; i < kHotbarSlots; ++i) {
        const Vec2 center{startX + static_cast<f32>(i) * stride, y};
        const bool picked = i == ctx.state.inventory.selected;

        hudPanel(ctx, center, {kSlotSize, kSlotSize},
                 picked ? ctx.assets.slotPickedSkin : ctx.assets.slotSkin, kLayerPanel + 2);

        const Slot& slot = ctx.state.inventory.slots[static_cast<usize>(i)];
        if (slot.empty()) continue;

        const ItemIcon icon = iconFor(ctx.assets, slot.id);
        hudSprite(ctx, center, {kSlotSize - 10.0f, kSlotSize - 10.0f}, icon.texture, icon.uv,
                  kLayerIcon);

        if (slot.count > 1) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", slot.count);
            hudText(ctx, buf, {center.x + 6.0f, center.y - 6.0f});
        }
    }

    // The held item's name, so the hotbar does not depend on reading 16px icons.
    const Slot& held = ctx.state.inventory.slots[static_cast<usize>(ctx.state.inventory.selected)];
    if (!held.empty()) {
        const std::string name  = itemName(held.id);
        const Vec2        where{0.0f, y + kSlotSize * 0.5f + 22.0f};
        hudPanel(ctx, where, {ctx.font.measure(name).x + 28.0f, 26.0f}, ctx.assets.panelSkin,
                 kLayerPanel);
        hudTextCentered(ctx, name, where, kPanelText);
    }
}

void drawStatus(HudContext& ctx) {
    const f32 halfW = viewport(ctx).x * 0.5f;
    const f32 halfH = viewport(ctx).y * 0.5f;

    const Vec2 panelCenter{halfW - 96.0f, halfH - 46.0f};
    hudPanel(ctx, panelCenter, {176.0f, 82.0f}, ctx.assets.panelSkin, kLayerPanel);

    char line[64];
    std::snprintf(line, sizeof(line), "%s %d, Year %d", seasonName(ctx.state.season),
                  ctx.state.day, ctx.state.year);
    hudTextCentered(ctx, line, {panelCenter.x, panelCenter.y + 24.0f}, kPanelText);
    hudTextCentered(ctx, ctx.state.clockText(), {panelCenter.x, panelCenter.y + 2.0f}, kPanelText);

    // Money, with the pack's coin rather than the letter G.
    std::snprintf(line, sizeof(line), "%d", ctx.state.money);
    const Vec2 moneyExtent = ctx.font.measure(line);
    const f32  moneyY      = panelCenter.y - 22.0f;
    hudSprite(ctx, {panelCenter.x - moneyExtent.x * 0.5f - 12.0f, moneyY}, {16.0f, 16.0f},
              ctx.assets.iconTex, ctx.assets.coinUV, kLayerIcon);
    hudTextCentered(ctx, line, {panelCenter.x + 8.0f, moneyY}, kMoneyColor);

    // Energy: the pack's own bar, bolt cap and all. Horizontal, above the hotbar, because
    // that is where the eye already is when deciding whether to swing again.
    const f32  fraction = std::clamp(ctx.state.player.energy / ctx.state.player.maxEnergy,
                                     0.0f, 1.0f);
    const f32  scale    = 3.0f;
    const Vec2 barSize{42.0f * scale, 9.0f * scale};
    const Vec2 barCenter{halfW - barSize.x * 0.5f - 20.0f, -halfH + 90.0f};

    hudSprite(ctx, barCenter, barSize, ctx.assets.barsTex, ctx.assets.barFrameUV, kLayerPanel);

    // The track is the frame minus its border and its bolt cap, measured off the art.
    constexpr f32 kTrackX = 3.0f, kTrackW = 28.0f;
    const f32 fillW = kTrackW * scale * fraction;
    if (fillW > 0.5f) {
        const f32 trackLeft = barCenter.x - barSize.x * 0.5f + kTrackX * scale;
        hudSprite(ctx, {trackLeft + fillW * 0.5f, barCenter.y}, {fillW, 7.0f * scale},
                  ctx.assets.barsTex, ctx.assets.barFillUV, kLayerPanel + 1,
                  fraction > 0.25f ? Color::white() : Color::fromRgb(0xFF6060));
    }
}

void drawToast(HudContext& ctx) {
    if (ctx.state.toastTimer <= 0.0f || ctx.state.toast.empty()) return;

    const f32  halfH = viewport(ctx).y * 0.5f;
    const Vec2 measured = ctx.font.measure(ctx.state.toast);
    const Vec2 center{0.0f, -halfH + 110.0f};

    const f32 alpha = std::min(1.0f, ctx.state.toastTimer * 2.0f);
    hudPanel(ctx, center, {measured.x + 32.0f, measured.y + 20.0f}, ctx.assets.panelSkin,
             kLayerPanel, Color::white().withAlpha(alpha));
    hudTextCentered(ctx, ctx.state.toast, center, kPanelText.withAlpha(alpha));
}

// A prompt over whatever the player is standing next to. This is the whole discovery
// mechanism for the bin, the shop and the bed, so it has to be unmissable.
void drawInteractPrompt(HudContext& ctx) {
    if (ctx.state.screen != Screen::Playing) return;

    const Vec2 feet = ctx.state.player.position;
    std::string prompt;

    const Vec2 door = World::tileCenter(ctx.world.house().doorTx, ctx.world.house().doorTy);
    const Vec2 shop = World::tileCenter(ctx.world.store().doorTx, ctx.world.store().doorTy);
    const Vec2 bin  = ctx.world.binTileCenter();

    if (lengthSquared(feet - bin) < kTile * kTile * 2.5f)        prompt = "[E] Ship item";
    else if (lengthSquared(feet - shop) < kTile * kTile * 2.5f)  prompt = "[E] General Store";
    else if (lengthSquared(feet - door) < kTile * kTile * 2.5f)  prompt = "[E] Sleep";
    if (prompt.empty()) return;

    // World -> overlay: both have +y up, so this is just the camera offset scaled by zoom.
    const Vec2 screen = (feet - ctx.app.camera().position) * ctx.app.camera().zoom +
                        Vec2{0.0f, 52.0f};
    const Vec2 measured = ctx.font.measure(prompt);
    hudPanel(ctx, screen, {measured.x + 26.0f, measured.y + 18.0f}, ctx.assets.panelSkin,
             kLayerPanel);
    hudTextCentered(ctx, prompt, screen, kPanelText);
}

}   // namespace

void applyUiTheme(ui::UI& gui, const Assets& assets) {
    gui.style.buttonSkin = assets.buttonGreenSkin;
    gui.style.panelSkin  = assets.panelSkin;
    gui.style.sampler    = renderer::SpriteSampler::NearestClamp;
    gui.style.text       = kPanelText;
    gui.style.baseLayer  = kLayerIcon + 10;
}

void drawHud(HudContext& ctx) {
    drawHotbar(ctx);
    drawStatus(ctx);
    drawToast(ctx);
    drawInteractPrompt(ctx);
}

void drawShop(HudContext& ctx) {
    // Scrim, then an opaque panel. A translucent panel over a bright field lets the
    // crops read straight through the price list, which is unreadable rather than
    // atmospheric.
    hudRect(ctx, {0.0f, 0.0f}, viewport(ctx), Color::fromRgb(0x000000).withAlpha(0.6f),
            kLayerPanel - 1);

    ctx.gui.style.buttonSkin = ctx.assets.buttonGreenSkin;
    ctx.gui.style.sampler    = renderer::SpriteSampler::NearestClamp;
    ctx.gui.style.text       = kPanelText;

    const Vec2 panel{0.0f, 0.0f};
    hudPanel(ctx, panel, {520.0f, 420.0f}, ctx.assets.panelSkin, kLayerPanel);
    hudTextCentered(ctx, "General Store", {0.0f, 176.0f}, kPanelText);

    char header[64];
    std::snprintf(header, sizeof(header), "%s seeds  -  you have %d",
                  seasonName(ctx.state.season), ctx.state.money);
    hudTextCentered(ctx, header, {0.0f, 150.0f}, kMoneyColor);
    hudSprite(ctx, {ctx.font.measure(header).x * 0.5f + 14.0f, 150.0f}, {16.0f, 16.0f},
              ctx.assets.iconTex, ctx.assets.coinUV, kLayerIcon);

    // Only the season's seeds are stocked: selling a player seeds that cannot grow
    // today is a trap, and the shop refusing to do it teaches the calendar for free.
    f32  y     = 96.0f;
    i32  shown = 0;
    for (usize i = 0; i < cropDefs().size(); ++i) {
        const CropDef& def = cropDefs()[i];
        if (def.season != ctx.state.season) continue;
        ++shown;

        const ItemIcon icon = iconFor(ctx.assets, seedOfCrop(static_cast<i32>(i)));
        hudSprite(ctx, {-210.0f, y}, {32.0f, 32.0f}, icon.texture, icon.uv, kLayerIcon);

        char label[96];
        std::snprintf(label, sizeof(label), "%-12s  %3d   (sells for %d)", def.name,
                      def.seedPrice, def.sellPrice);
        hudText(ctx, label, {-180.0f, y + 8.0f}, kPanelText);

        const bool affordable = ctx.state.money >= def.seedPrice;
        const Vec2 buttonPos{186.0f, y};
        const Vec2 buttonSize{80.0f, 34.0f};

        // The skin is one texture; affordability is a tint on it.
        ctx.gui.style.button = affordable ? Color::white() : Color::fromRgb(0x808080);
        ctx.gui.style.hovered = affordable ? Color::fromRgb(0xD8FFD8) : Color::fromRgb(0x808080);
        if (ctx.gui.button(buttonPos, buttonSize, "Buy") && affordable) {
            if (ctx.state.inventory.add(seedOfCrop(static_cast<i32>(i)), 1) == 0) {
                ctx.state.money -= def.seedPrice;
                ctx.state.note(std::string("Bought ") + def.name + " seeds");
            } else {
                ctx.state.note("Your bag is full");
            }
        }
        y -= 56.0f;
    }

    if (shown == 0)
        hudTextCentered(ctx, "Nothing grows in winter. Come back in spring.", {0.0f, 40.0f},
                        kPanelText);

    hudTextCentered(ctx, "[E] or [Esc] to leave", {0.0f, -176.0f}, kPanelText);
}

void drawSleepOverlay(HudContext& ctx) {
    if (ctx.state.fadeAlpha <= 0.0f) return;

    hudRect(ctx, {0.0f, 0.0f}, viewport(ctx),
            Color::fromRgb(0x000000).withAlpha(ctx.state.fadeAlpha), kLayerFade);

    if (ctx.state.screen != Screen::DaySummary) return;

    char line[96];
    hudTextCentered(ctx, "You slept well.", {0.0f, 70.0f}, kTextColor, kLayerFadeText);
    std::snprintf(line, sizeof(line), "Shipped %d item(s) for %d G", ctx.state.lastNightShipped,
                  ctx.state.lastNightEarnings);
    hudTextCentered(ctx, line, {0.0f, 30.0f}, kMoneyColor, kLayerFadeText);
    std::snprintf(line, sizeof(line), "%s %d, Year %d", seasonName(ctx.state.season),
                  ctx.state.day, ctx.state.year);
    hudTextCentered(ctx, line, {0.0f, -10.0f}, kTextColor, kLayerFadeText);
    hudTextCentered(ctx, "[Space] to start the day", {0.0f, -60.0f}, kTextColor, kLayerFadeText);
}

}
