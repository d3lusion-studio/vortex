// Farm RPG — a Stardew-shaped farming loop on the Vortex engine.
//
// The loop: hoe a tile, sow a seed, water it, sleep, repeat until it is ripe, pick
// it, drop it in the shipping bin, and wake up richer. The calendar is the pressure:
// a crop caught by the turn of the season dies, so what you plant on day 24 matters.
//
// Controls
//   WASD / arrows  walk            Shift  run
//   1..0           select item     Space / Left click  use held item
//   E              interact (shop, shipping bin, bed)
//   F1             debug overlay   F5 / F9  save / load
//   Esc            quit (autosaves)
//
// Run with VORTEX_FARM_CHECK=1 for a headless-ish self-check: it plays a scripted
// day — till, sow, water, sleep, harvest, ship — and exits non-zero if the loop did
// not actually produce money. Like every App example here it still needs a GPU.

#include "assets.hpp"
#include "farm.hpp"
#include "hud.hpp"
#include "player.hpp"
#include "save.hpp"
#include "world.hpp"

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/debug/debug_plugin.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/camera_controller.hpp"
#include "vortex/renderer/lighting2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/text/font.hpp"
#include "vortex/ui/ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace vortex;
using namespace farm;

namespace {

constexpr const char* kSavePath = "farmrpg_save.txt";

// The map in world units. y is negative downward, so the rect starts at the bottom-left.
const Rect kMapBounds{0.0f, -static_cast<f32>(kMapH) * kTile,
                      static_cast<f32>(kMapW) * kTile, static_cast<f32>(kMapH) * kTile};

// Everything that is not GameState: the art, the map, and the frame-level services.
struct Game {
    Assets                      assets;
    renderer::FollowController2D follow{.halfLife = 0.07f,
                                        // A window the player can shuffle inside without
                                        // dragging the whole farm around behind them.
                                        .deadzone = {10.0f, 8.0f}};
    World                       world;
    GameState                   state;
    std::unique_ptr<text::Font> font;
    std::unique_ptr<ui::UI>     gui;

    f32         sleepTimer = 0.0f;
    bool        checkMode  = false;
    // Shown once the player is back on their feet — see the 2am collapse.
    std::string morningNote;
    // VORTEX_SCREENSHOT=<path> grabs one frame once the world has settled, so a
    // capture never catches the first frame with the camera still snapping into place.
    // VORTEX_SHOT_FRAME picks which frame, for capturing a state that takes a while
    // to reach (a grown crop, a lit window at dusk).
    std::string shotPath;
    i32         shotFrame = 0;
    i32         shotAt    = 20;
    i32  scriptStep = 0;
    f32  scriptTime = 0.0f;
    i32  daysSlept  = 0;
    bool ready      = false;
};

// --- Day/night ---------------------------------------------------------------
//
// The ambient colour the world is multiplied by. White is noon; the evening slides it
// toward a cold blue, which is what makes a warm lamp read as warm without the lamp
// changing at all.
[[nodiscard]] Color ambientFor(f32 hour) {
    f32 darkness = 0.0f;
    if (hour >= 17.0f && hour < 22.0f)      darkness = (hour - 17.0f) / 5.0f;
    else if (hour >= 22.0f)                 darkness = 1.0f;

    // Never quite black: a farm at midnight you cannot see at all is not atmospheric, it
    // is a bug report.
    const Color night = Color::fromRgb(0x2A3560);
    return {lerp(1.0f, night.r, darkness), lerp(1.0f, night.g, darkness),
            lerp(1.0f, night.b, darkness), 1.0f};
}

// The lights the farm puts in the world each frame.
void submitLights(app::App& app, Game& game) {
    renderer::Lighting2D* lights = app.lights();
    if (lights == nullptr) return;

    lights->begin();
    lights->ambient = ambientFor(game.state.hour());

    // Daylight needs no lamps, and skipping them keeps the buffer free of quads that
    // could only wash out a scene that is already white.
    if (game.state.hour() < 16.5f) return;

    // The windows of both buildings.
    for (const Building* b : {&game.world.house(), &game.world.store()}) {
        const Vec2 door = World::tileCenter(b->doorTx, b->doorTy);
        lights->add({.position  = door + Vec2{0.0f, 8.0f},
                     .radius    = 90.0f,
                     .color     = Color::fromRgb(0xFFC97A),
                     .intensity = 1.1f});
    }

    // The player carries one, so the walk home is playable.
    lights->add({.position  = game.state.player.position + Vec2{0.0f, 10.0f},
                 .radius    = 78.0f,
                 .color     = Color::fromRgb(0xFFE0A0),
                 .intensity = 1.0f});
}

void giveStartingKit(GameState& state) {
    state.inventory.add(kToolHoe, 1);
    state.inventory.add(kToolCan, 1);
    state.inventory.add(kToolScythe, 1);
    state.inventory.add(seedOfCrop(0), 15);   // parsnips: the classic first crop
}

// --- Sleep -------------------------------------------------------------------

void beginSleep(Game& game) {
    if (game.state.screen != Screen::Playing) return;
    game.state.screen     = Screen::Sleeping;
    game.sleepTimer       = 0.0f;
}

void finishSleep(app::App& app, Game& game) {
    advanceDay(app.scene(), game.assets, game.world, game.state);
    game.state.player.energy   = game.state.player.maxEnergy;
    game.state.player.position = game.world.spawnPoint();
    game.state.screen          = Screen::DaySummary;
    ++game.daysSlept;
}

void updateSleep(app::App& app, Game& game, f32 dt) {
    GameState& state = game.state;

    if (state.screen == Screen::Sleeping) {
        game.sleepTimer  += dt;
        state.fadeAlpha   = std::min(1.0f, game.sleepTimer / 0.9f);
        if (game.sleepTimer >= 1.1f) finishSleep(app, game);
    } else if (state.screen == Screen::DaySummary) {
        state.fadeAlpha = 1.0f;
        const bool go   = app.input().isKeyPressed(pf::Key::Space) ||
                          app.input().isKeyPressed(pf::Key::Enter) ||
                          (game.checkMode && game.sleepTimer > 1.4f);
        game.sleepTimer += dt;
        if (go) {
            state.screen    = Screen::Playing;
            state.fadeAlpha = 0.0f;
            if (!game.morningNote.empty()) {
                state.note(game.morningNote, 4.0f);
                game.morningNote.clear();
            }
        }
    } else {
        state.fadeAlpha = std::max(0.0f, state.fadeAlpha - dt * 2.0f);
    }
}

// --- Interaction --------------------------------------------------------------

[[nodiscard]] bool nearTile(const GameState& state, Vec2 target) {
    return lengthSquared(state.player.position - target) < kTile * kTile * 2.5f;
}

// Drop the held produce in the bin. Selling happens overnight, so this is a decision
// you commit to before bed rather than a click that pays instantly.
void shipHeld(GameState& state) {
    Slot& slot = state.inventory.selectedSlot();
    if (slot.empty() || !isProduce(slot.id)) {
        state.note("Only harvested crops can be shipped");
        return;
    }

    const CropDef& def   = cropDefs()[static_cast<usize>(cropOfProduce(slot.id))];
    const i32      count = slot.count;
    state.shipped.push_back(Slot{slot.id, count});
    state.note("Shipped " + std::to_string(count) + "x " + def.name + " (paid at dawn)");
    slot = Slot{};
}

void interact(app::App& app, Game& game) {
    GameState& state = game.state;

    if (state.screen == Screen::Shop) {
        state.screen = Screen::Playing;
        return;
    }
    if (state.screen != Screen::Playing) return;

    if (nearTile(state, game.world.binTileCenter())) {
        shipHeld(state);
        return;
    }
    if (nearTile(state, World::tileCenter(game.world.store().doorTx, game.world.store().doorTy))) {
        state.screen = Screen::Shop;
        return;
    }
    if (nearTile(state, World::tileCenter(game.world.house().doorTx, game.world.house().doorTy))) {
        beginSleep(game);
        return;
    }
    state.note("Nothing to interact with here");
}

// --- The scripted self-check --------------------------------------------------
//
// Plays the core loop with no hands: walk to a tile, hoe it, sow, water, sleep four
// nights, pick the parsnip, ship it, sleep again. If money did not move, the loop is
// broken and the process says so.
void runScript(app::App& app, Game& game, f32 dt) {
    GameState& state = game.state;
    game.scriptTime += dt;

    const auto pick = [&](ItemId id) {
        for (i32 i = 0; i < kHotbarSlots; ++i)
            if (state.inventory.slots[static_cast<usize>(i)].id == id) state.inventory.selected = i;
    };

    const i32 tx = 12, ty = 14;
    const Vec2 stand = World::tileCenter(tx, ty + 1);

    switch (game.scriptStep) {
        case 0:   // teleport next to a field tile and face it
            state.player.position = stand;
            state.player.facing   = Dir::Up;
            pick(kToolHoe);
            ++game.scriptStep;
            break;

        case 1:
            if (state.tile(tx, ty).tilled) { pick(seedOfCrop(0)); ++game.scriptStep; break; }
            beginToolUse(game.assets, game.world, state);
            break;

        case 2:
            if (state.tile(tx, ty).crop >= 0) { pick(kToolCan); ++game.scriptStep; break; }
            beginToolUse(game.assets, game.world, state);
            break;

        case 3:
            if (state.tile(tx, ty).watered) { ++game.scriptStep; break; }
            beginToolUse(game.assets, game.world, state);
            break;

        case 4:   // sleep until the parsnip is ripe, watering each morning
            if (state.screen != Screen::Playing) break;
            if (cropReady(state.tile(tx, ty))) { ++game.scriptStep; break; }
            if (game.daysSlept >= 8) { ++game.scriptStep; break; }   // bail out, do not hang
            if (!state.tile(tx, ty).watered) {
                state.player.position = stand;
                state.player.facing   = Dir::Up;
                pick(kToolCan);
                beginToolUse(game.assets, game.world, state);
            } else {
                beginSleep(game);
            }
            break;

        case 5:   // pick it
            if (state.inventory.countOf(produceOfCrop(0)) > 0) { ++game.scriptStep; break; }
            state.player.position = stand;
            state.player.facing   = Dir::Up;
            beginToolUse(game.assets, game.world, state);
            break;

        case 6:   // ship it and sleep on it
            pick(produceOfCrop(0));
            shipHeld(state);
            beginSleep(game);
            ++game.scriptStep;
            break;

        case 7:
            if (state.screen == Screen::Playing && state.lastNightEarnings > 0) {
                ++game.scriptStep;
                app.quit();
            }
            break;

        default: break;
    }

    if (game.scriptTime > 90.0f) app.quit();   // bound the run whatever happens
}

// --- Rendering ----------------------------------------------------------------
//
// The world draws itself: every prop, crop and building is an entity with
// SpriteComp::ySort, so the scene extraction sorts them by depth and the only thing
// left to draw by hand is the cursor, which is not part of the world.

void drawTileCursor(app::App& app, Game& game, renderer::SpriteBatch& batch) {
    if (game.state.screen != Screen::Playing) return;

    i32 fx = 0, fy = 0;
    facingTile(game.state, fx, fy);
    if (!GameState::inBounds(fx, fy)) return;

    batch.draw({.position = World::tileCenter(fx, fy),
                .size     = {kTile, kTile},
                .color    = Color::fromRgb(0xFFFFFF).withAlpha(0.18f),
                .texture  = app.whiteTexture(),
                .layer    = 900});
}

}   // namespace

int main() {
    app::AppConfig config;
    config.title      = "Vortex Farm RPG";
    config.width      = 1280;
    config.height     = 720;
    config.clearColor = Color::fromRgb(0x1B2A16);
    config.maxSprites = 40000;   // App honours VORTEX_MAX_FRAMES itself when maxFrames is 0
    config.lighting2D = true;    // dusk, lit windows, and a lantern to get home by
    // Save a crop sprite in Aseprite and it is in the running game a quarter-second later.
    config.hotReloadAssets = true;

    app::App app(config);
    // F1: entity inspector, perf graphs, live counters. One line, because the plugin owns
    // the wiring.
    app.addPlugin<debug::OverlayPlugin>();

    Game     game;
    game.checkMode = std::getenv("VORTEX_FARM_CHECK") != nullptr;
    if (const char* shot = std::getenv("VORTEX_SCREENSHOT")) game.shotPath = shot;
    if (const char* at = std::getenv("VORTEX_SHOT_FRAME")) game.shotAt = std::atoi(at);

    app.onStart([&game](app::App& a) {
        // Rasterised at the size it is drawn at: the overlay is in screen pixels, so a
        // 16px face lands on 16 screen pixels and stays crisp.
        game.font = text::Font::loadDefault(a.device(), a.fileSystem(), 16.0f);
        if (!game.font) {
            VORTEX_ERROR("Farm", "No system font found; set VORTEX_FONT_PATH.");
            a.quit();
            return;
        }
        game.gui = std::make_unique<ui::UI>(a.device());

        if (!loadAssets(a, a.scene(), CharacterLook{}, game.assets)) {
            VORTEX_ERROR("Farm", "Art pack missing — expected it under %s",
                         VORTEX_FARM_ASSET_DIR);
            a.quit();
            return;
        }

        game.world.build(a.scene(), game.assets, game.state);
        spawnPlayer(a.scene(), game.assets, game.state, game.world);

        if (!game.checkMode && loadGame(a.fileSystem(), kSavePath, game.state)) {
            game.world.syncFarm(a.scene(), game.assets, game.state);
            game.world.applySeason(a.scene(), game.assets, game.state.season);
        } else {
            giveStartingKit(game.state);
            game.state.player.position = game.world.spawnPoint();
        }

        applyUiTheme(*game.gui, game.assets);

        a.camera().zoom = kZoom;
        game.ready      = true;

        VORTEX_INFO("Farm", "WASD move, Shift run, 1-0 select, Space/click use, E interact, "
                            "F5 save, Esc quit.");
    });

    app.onFixedUpdate([&game](app::App& a, f32 dt) {
        if (!game.ready) return;
        GameState& state = game.state;

        if (state.toastTimer > 0.0f) state.toastTimer -= dt;
        updateSleep(a, game, dt);

        if (state.screen == Screen::Playing) {
            state.clock += dt;
            // 2am: you collapse where you stand. The day is a resource, and running out
            // of it has to cost something.
            if (state.clock >= kDayLengthSeconds) {
                // Held until morning: beginSleep leaves Screen::Playing at once and only
                // that screen draws the HUD, so a toast set here would fade out behind
                // the sleep fade and never be read.
                game.morningNote = "You passed out at 2am. The day got away from you.";
                beginSleep(game);
            }
            updatePlayer(a, game.assets, game.world, state, dt);
        }

        if (game.checkMode) runScript(a, game, dt);

        submitLights(a, game);

        // Follow, keep the view on the map, then land on the pixel grid. All three are the
        // engine's; the order matters, because snapping is only true until something else
        // moves the camera.
        game.follow.update(a.camera(), state.player.position + Vec2{0.0f, 8.0f}, dt);
        renderer::clampToBounds(a.camera(), kMapBounds);
        renderer::snapToPixelGrid(a.camera(), 1.0f / kZoom);
    });

    app.onUpdate([&game](app::App& a, f32) {
        if (game.ready && !game.shotPath.empty() && ++game.shotFrame >= game.shotAt) {
            a.requestScreenshot(game.shotPath);
            game.shotPath.clear();
        }
        if (!game.ready || game.checkMode) return;
        pf::IInputProvider& in    = a.input();
        GameState&          state = game.state;

        if (in.isKeyPressed(pf::Key::Escape)) {
            if (state.screen == Screen::Shop) state.screen = Screen::Playing;
            else                              a.quit();
        }

        if (in.isKeyPressed(pf::Key::E)) interact(a, game);

        if (in.isKeyPressed(pf::Key::F5)) (void)saveGame(a.fileSystem(), kSavePath, state);
        if (in.isKeyPressed(pf::Key::F9) && loadGame(a.fileSystem(), kSavePath, state)) {
            game.world.syncFarm(a.scene(), game.assets, state);
            game.world.applySeason(a.scene(), game.assets, state.season);
        }

        if (state.screen != Screen::Playing) return;

        const pf::Key digits[kHotbarSlots] = {
            pf::Key::Num1, pf::Key::Num2, pf::Key::Num3, pf::Key::Num4, pf::Key::Num5,
            pf::Key::Num6, pf::Key::Num7, pf::Key::Num8, pf::Key::Num9, pf::Key::Num0};
        for (i32 i = 0; i < kHotbarSlots; ++i)
            if (in.isKeyPressed(digits[i])) state.inventory.selected = i;

        if (in.isKeyPressed(pf::Key::Space) || in.isMousePressed(pf::MouseButton::Left))
            beginToolUse(game.assets, game.world, state);
    });

    // The world: batched with the scene, in world units. Night is no longer drawn here —
    // it is the lighting pass, submitted below and composited by the loop.
    app.onRender([&game](app::App& a, renderer::SpriteBatch& batch) {
        if (!game.ready) return;
        drawTileCursor(a, game, batch);
    });

    // The HUD: framebuffer pixels, origin at the centre, drawn over everything above.
    app.onUi([&game](app::App& a, renderer::SpriteBatch& batch) {
        if (!game.ready || !game.font) return;

        // The overlay's origin is the centre of the screen with +y up; the mouse arrives
        // top-left with +y down.
        float mx = 0.0f, my = 0.0f;
        a.input().mousePosition(mx, my);
        ui::InputState input;
        input.mouse    = {mx - static_cast<f32>(a.camera().viewportWidth) * 0.5f,
                          static_cast<f32>(a.camera().viewportHeight) * 0.5f - my};
        input.down     = a.input().isMouseDown(pf::MouseButton::Left);
        input.pressed  = a.input().isMousePressed(pf::MouseButton::Left);
        input.released = a.input().isMouseReleased(pf::MouseButton::Left);

        game.gui->begin(batch, *game.font, input);

        HudContext ctx{a, game.assets, game.world, game.state, batch, *game.font, *game.gui};
        switch (game.state.screen) {
            case Screen::Shop:    drawShop(ctx); break;
            case Screen::Playing: drawHud(ctx);  break;
            // Asleep: the fade owns the screen. Drawing the hotbar under it would only
            // show through as the text poking out of the dark.
            default: break;
        }
        drawSleepOverlay(ctx);

        game.gui->end();
    });

    app.onShutdown([&game](app::App& a) {
        if (game.ready && !game.checkMode) (void)saveGame(a.fileSystem(), kSavePath, game.state);
    });

    const int rc = app.run();

    if (game.checkMode) {
        const bool ok = game.state.lastNightEarnings > 0 && game.state.money > 500;
        std::printf("\n[%s] Farm self-check: slept %d night(s), shipped %d for %d G, "
                    "balance %d G\n",
                    ok ? "PASS" : "FAIL", game.daysSlept, game.state.lastNightShipped,
                    game.state.lastNightEarnings, game.state.money);
        return ok ? 0 : 1;
    }
    return rc;
}
