// Everything drawn on top of the world: the hotbar, the clock, the energy bar, the
// shop, and the cards that bracket a night's sleep.
#pragma once

#include "assets.hpp"
#include "farm.hpp"
#include "world.hpp"

namespace vortex::app       { class App; }
namespace vortex::renderer  { class SpriteBatch; }
namespace vortex::text      { class Font; }
namespace vortex::ui        { class UI; }

namespace farm {

// Everything here is drawn from App::onUi, whose batch is already bound to the
// framebuffer in pixels with the origin at the centre and +y up. So a HUD coordinate is
// a batch coordinate: no camera, no zoom, nothing to convert.
struct HudContext {
    vortex::app::App&              app;
    const Assets&                  assets;
    const World&                   world;
    GameState&                     state;
    vortex::renderer::SpriteBatch& batch;
    const vortex::text::Font&      font;
    vortex::ui::UI&                gui;
};

// Point the immediate-mode UI at the pack's art. Called once at startup rather than per
// frame: a screen that re-themes the shared gui on every draw leaves its theme behind for
// whatever draws next, and the bug surfaces on a screen that never touched it.
void applyUiTheme(vortex::ui::UI& gui, const Assets& assets);

void drawHud(HudContext& ctx);

// The shop panel. Returns true while it wants to keep the input focus.
void drawShop(HudContext& ctx);

// Night fade and the morning card.
void drawSleepOverlay(HudContext& ctx);

}
