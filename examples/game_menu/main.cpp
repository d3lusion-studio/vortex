// A game's outer shell: a loading screen that waits on real async asset loads, a
// main menu built with the immediate-mode UI, and a tiny "playing" state you can
// enter and leave. It covers two of the things every game needs before it needs
// anything else — a Loading Screen and a Game Menu — on top of the App loop, the
// AssetManager, and the ui module.
//
// The three states and how they hand off:
//
//   Loading  drives async loads through an AssetManager::Barrier and draws its
//            progress(). Advances to Menu once every asset is in AND a minimum
//            splash time has passed (so the bar is seen, not flashed).
//   Menu     an immediate-mode UI panel: Play enters the game, Quit leaves.
//   Playing  a sprite drawn with a just-loaded texture, bouncing around; Esc
//            returns to the menu.
//
// Controls: mouse for the menu, Esc backs out / quits.
//
// Headless self-check: VORTEX_GAMEMENU_CHECK=1 auto-advances Loading -> Menu ->
// Playing and exits non-zero unless it got there with every asset loaded — a CI
// pass like the other examples (needs a GPU, as any App example does).

#include "vortex/app/app.hpp"
#include "vortex/asset/asset_manager.hpp"
#include "vortex/asset/asset_types.hpp"
#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/text/font.hpp"
#include "vortex/text/text_renderer.hpp"
#include "vortex/ui/ui.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace vortex;

namespace {

enum class Screen { Loading, Menu, Playing };

constexpr f32 kMinSplashTime = 0.8f;   // shortest time the loading screen stays up

struct MenuState {
    Screen screen = Screen::Loading;
    f32    stateTime = 0.0f;   // seconds spent in the current screen

    std::unique_ptr<text::Font> title;
    std::unique_ptr<text::Font> body;
    std::unique_ptr<ui::UI>     gui;

    std::array<assets::TextureHandle, 4> textures{};
    assets::AssetManager::Barrier        barrier;

    ecs::Entity player;
    bool        playStarted = false;

    bool checkMode = false;
    bool reachedPlaying = false;
};

// Bounce a sprite around a fixed box — the whole of the "game".
constexpr f32 kPlayHalfW = 560.0f;
constexpr f32 kPlayHalfH = 320.0f;

void enterPlaying(app::App& a, MenuState& s) {
    a.scene().clear();
    s.player = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(s.player).position = {0.0f, 0.0f};
    a.registry().emplace<ecs::Velocity>(s.player, ecs::Velocity{{260.0f, 200.0f}});
    a.registry().emplace<ecs::SpriteComp>(s.player, ecs::SpriteComp{
        .texture = a.assets().gpuTexture(s.textures[0]),
        .size    = {200.0f, 200.0f}});

    if (!s.playStarted) {   // register the bounce system exactly once
        s.playStarted = true;
        a.scene().addSystem([](ecs::Registry& reg, f32 dt) {
            reg.view<ecs::Transform2D, ecs::Velocity>(
                [dt](ecs::Entity, ecs::Transform2D& t, ecs::Velocity& v) {
                    t.position += v.value * dt;
                    if (t.position.x < -kPlayHalfW || t.position.x > kPlayHalfW) v.value.x = -v.value.x;
                    if (t.position.y < -kPlayHalfH || t.position.y > kPlayHalfH) v.value.y = -v.value.y;
                });
        });
    }
    s.screen         = Screen::Playing;
    s.stateTime      = 0.0f;
    s.reachedPlaying = true;
}

void enterMenu(app::App& a, MenuState& s) {
    a.scene().clear();
    s.screen    = Screen::Menu;
    s.stateTime = 0.0f;
}

// A progress bar drawn from two panels: a track and a fill scaled by `t` in [0,1].
void drawProgressBar(ui::UI& gui, Vec2 center, Vec2 size, f32 t) {
    gui.panel(center, size, Color::fromRgb(0x2A2E3E));
    const f32  w    = size.x * saturate(t);
    const Vec2 fill = {center.x - (size.x - w) * 0.5f, center.y};
    gui.panel(fill, {w, size.y}, Color::fromRgb(0x2A669E));
}

}

int main() {
    app::AppConfig config;
    config.title      = "Vortex Game Menu";
    config.clearColor = Color::fromRgb(0x0A0D17);
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App  app(config);
    MenuState state;
    state.checkMode = std::getenv("VORTEX_GAMEMENU_CHECK") != nullptr;

    app.onStart([&state](app::App& a) {
        const std::string fontPath = text::Font::defaultPath(a.fileSystem());
        if (fontPath.empty()) {
            VORTEX_WARN("Menu", "No system font found; set VORTEX_FONT_PATH. Exiting cleanly.");
            a.quit();
            return;
        }
        state.title = text::Font::loadFromFile(a.device(), a.fileSystem(), fontPath.c_str(), 52.0f);
        state.body  = text::Font::loadFromFile(a.device(), a.fileSystem(), fontPath.c_str(), 24.0f);
        state.gui   = std::make_unique<ui::UI>(a.device());

        // Kick off the async loads and collect them in a barrier. load() returns at
        // once; the files are read on the AssetManager's IO threads and uploaded by
        // the main thread in beginFrame(), which App pumps every frame.
        const char* paths[] = {"assets/meme1.png", "assets/meme2.png",
                               "assets/meme3.png", "assets/meme4.png"};
        for (usize i = 0; i < state.textures.size(); ++i) {
            state.textures[i] = a.assets().load<assets::TextureAsset>(paths[i]);
            state.barrier.add(a.assets(), state.textures[i]);
        }
        VORTEX_INFO("Menu", "Loading %zu assets...", state.barrier.size());
    });

    // State logic: advance timers and drive automatic transitions.
    app.onUpdate([&state](app::App& a, f32 dt) {
        if (!state.gui) return;   // font failed; we are quitting
        state.stateTime += dt;

        pf::IInputProvider& in = a.input();

        if (state.screen == Screen::Loading) {
            const bool ready = state.barrier.done(a.assets()) && state.stateTime >= kMinSplashTime;
            if (ready) {
                VORTEX_INFO("Menu", "Assets ready (%zu failed). Showing menu.",
                            state.barrier.failed(a.assets()));
                enterMenu(a, state);
            }
        } else if (state.screen == Screen::Menu) {
            if (in.isKeyPressed(pf::Key::Escape)) a.quit();
            if (state.checkMode && state.stateTime > 0.3f) enterPlaying(a, state);   // auto-play
        } else if (state.screen == Screen::Playing) {
            if (in.isKeyPressed(pf::Key::Escape)) enterMenu(a, state);
            if (state.checkMode && state.stateTime > 0.5f) a.quit();
        }
    });

    // Rendering + immediate-mode widgets. Buttons are polled here, in draw order.
    app.onRender([&state](app::App& a, renderer::SpriteBatch& batch) {
        if (!state.gui) return;

        float mx = 0.0f, my = 0.0f;
        a.input().mousePosition(mx, my);
        ui::InputState in;
        in.mouse    = a.camera().screenToWorld(mx, my);
        in.down     = a.input().isMouseDown(pf::MouseButton::Left);
        in.pressed  = a.input().isMousePressed(pf::MouseButton::Left);
        in.released = a.input().isMouseReleased(pf::MouseButton::Left);

        ui::UI& gui = *state.gui;
        gui.begin(batch, *state.body, in);

        switch (state.screen) {
        case Screen::Loading: {
            text::drawText(batch, *state.title, "Loading",
                           {-90.0f, 40.0f}, Color::fromRgb(0xEBEFFA), 1.0f, 1200);
            drawProgressBar(gui, {0.0f, -30.0f}, {520.0f, 26.0f},
                            state.barrier.progress(a.assets()));
            break;
        }
        case Screen::Menu: {
            text::drawText(batch, *state.title, "Vortex",
                           {-90.0f, 200.0f}, Color::fromRgb(0xEBEFFA), 1.0f, 1200);
            gui.panel({0.0f, -20.0f}, {340.0f, 260.0f});
            gui.beginColumn({0.0f, 70.0f}, {280.0f, 46.0f}, 18.0f);
            if (gui.button("Play")) enterPlaying(a, state);
            gui.label("Settings (todo)");
            if (gui.button("Quit")) a.quit();
            break;
        }
        case Screen::Playing: {
            // The bouncing sprite is in the scene; App draws it. Just an overlay hint.
            text::drawText(batch, *state.body, "Playing — press Esc for menu",
                           {-560.0f, 300.0f}, Color::fromRgb(0xEBEFFA), 1.0f, 1200);
            break;
        }
        }

        gui.end();
    });

    const int rc = app.run();

    if (state.checkMode) {
        const bool ok = state.reachedPlaying && state.barrier.done(app.assets()) &&
                        state.barrier.failed(app.assets()) == 0;
        std::printf("\n[%s] Game-menu self-check: reached Playing=%d, assets loaded=%zu/%zu\n",
                    ok ? "PASS" : "FAIL", state.reachedPlaying ? 1 : 0,
                    state.barrier.size() - state.barrier.pending(app.assets()) -
                        state.barrier.failed(app.assets()),
                    state.barrier.size());
        return ok ? 0 : 1;
    }
    return rc;
}
