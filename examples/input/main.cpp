// Actions, not keys. The game below never asks "is W down" — it asks "is `moveY`
// positive", and the same code is driven by a keyboard, a gamepad stick or a d-pad
// without knowing which. Press R to rebind `fire`, and the whole table round-trips
// through JSON, which is all a keybinds screen ever needs.

#include "vortex/app/app.hpp"
#include "vortex/core/json.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input_map.hpp"
#include "vortex/renderer/sprite_batch.hpp"

#include <cstdlib>

using namespace vortex;

namespace {

constexpr const char* kBindingsPath = "keybinds.json";

void defaultBindings(pf::InputMap& map) {
    // Movement: WASD, arrows, the left stick and the d-pad all feed the same two
    // actions. bindAxis() flips the negative half's scale, so one action spans
    // [-1, 1] whichever device drives it.
    map.bindAxis("moveX", pf::Binding::ofKey(pf::Key::D), pf::Binding::ofKey(pf::Key::A));
    map.bindAxis("moveX", pf::Binding::ofKey(pf::Key::Right), pf::Binding::ofKey(pf::Key::Left));
    map.bindAxis("moveX", pf::Binding::ofPad(pf::GamepadButton::DPadRight),
                          pf::Binding::ofPad(pf::GamepadButton::DPadLeft));
    map.bind("moveX", pf::Binding::ofAxis(pf::GamepadAxis::LeftX));

    map.bindAxis("moveY", pf::Binding::ofKey(pf::Key::W), pf::Binding::ofKey(pf::Key::S));
    map.bindAxis("moveY", pf::Binding::ofKey(pf::Key::Up), pf::Binding::ofKey(pf::Key::Down));
    map.bindAxis("moveY", pf::Binding::ofPad(pf::GamepadButton::DPadUp),
                          pf::Binding::ofPad(pf::GamepadButton::DPadDown));
    map.bind("moveY", pf::Binding::ofAxis(pf::GamepadAxis::LeftY));

    map.bind("fire", pf::Binding::ofKey(pf::Key::Space));
    map.bind("fire", pf::Binding::ofMouse(pf::MouseButton::Left));
    map.bind("fire", pf::Binding::ofPad(pf::GamepadButton::A));
    map.bind("fire", pf::Binding::ofAxis(pf::GamepadAxis::RightTrigger));

    map.bind("quit", pf::Binding::ofKey(pf::Key::Escape));
    map.bind("quit", pf::Binding::ofPad(pf::GamepadButton::Start));
}

// Every key the rebind screen will let the player choose from.
constexpr pf::Key kRebindable[] = {
    pf::Key::Space, pf::Key::Enter, pf::Key::LeftShift, pf::Key::LeftCtrl,
    pf::Key::E,     pf::Key::Q,     pf::Key::F,         pf::Key::R,
};

struct State {
    Vec2 player{0.0f, 0.0f};
    int  shots        = 0;
    bool awaitingBind = false;
};

}

int main() {
    app::AppConfig config;
    config.title = "Vortex Input";
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);
    State    state;

    app.onStart([](app::App& a) {
        auto fs = pf::createFileSystem();

        // A saved keybinds file wins over the defaults — this is exactly the flow a
        // real options screen has.
        bool loaded = false;
        if (fs->exists(kBindingsPath)) {
            const std::vector<std::byte> bytes = fs->readFile(kBindingsPath);
            std::string error;
            const json::Value doc = json::parse(
                {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, &error);
            if (error.empty()) loaded = a.actions().load(doc);
            else VORTEX_WARN("Input", "%s: %s", kBindingsPath, error.c_str());
        }
        if (!loaded) defaultBindings(a.actions());

        VORTEX_INFO("Input", "%s bindings. Gamepad %s.",
                    loaded ? "loaded saved" : "using default",
                    a.input().isGamepadConnected(0) ? "connected" : "not connected");
        VORTEX_INFO("Input", "Move with WASD / arrows / stick. `fire` = SPACE / LMB / pad A. "
                             "R rebinds `fire`. ESC quits.");
    });

    app.onUpdate([&state](app::App& a, f32 dt) {
        pf::InputMap& actions = a.actions();

        if (actions.pressed("quit")) a.quit();

        // Rebinding: the next rebindable key the player touches becomes `fire`.
        if (state.awaitingBind) {
            for (const pf::Key key : kRebindable) {
                if (!a.input().isKeyPressed(key)) continue;

                // Replace the keyboard binding but keep the pad ones, which is what a
                // player expects "rebind" to mean.
                actions.rebind("fire", {pf::Binding::ofKey(key),
                                        pf::Binding::ofPad(pf::GamepadButton::A),
                                        pf::Binding::ofAxis(pf::GamepadAxis::RightTrigger)});
                state.awaitingBind = false;

                auto              fs   = pf::createFileSystem();
                const std::string text = json::write(actions.save());
                if (fs->writeFile(kBindingsPath, text.data(), text.size()))
                    VORTEX_INFO("Input", "`fire` rebound; saved to %s", kBindingsPath);
                break;
            }
            return;   // swallow the frame, so the rebind key does not also fire
        }

        if (a.input().isKeyPressed(pf::Key::R)) {
            state.awaitingBind = true;
            VORTEX_INFO("Input", "press SPACE / ENTER / SHIFT / CTRL / E / Q / F / R to rebind");
            return;
        }

        // The whole point: one call, and it does not care what the player is holding.
        // vector() clamps the pair, so diagonal keyboard movement is not faster.
        const Vec2 move = actions.vector("moveX", "moveY");
        state.player += move * (420.0f * dt);

        if (actions.pressed("fire")) ++state.shots;

        a.camera().position = damp(a.camera().position, state.player, 6.0f, dt);

        if (a.frameCount() % 120 == 0 && a.frameCount() > 0)
            VORTEX_INFO("Input", "move [%.2f, %.2f] | fire %s | %d shots | pad %s",
                        static_cast<f64>(move.x), static_cast<f64>(move.y),
                        actions.down("fire") ? "down" : "up ", state.shots,
                        a.input().isGamepadConnected(0) ? "yes" : "no");
    });

    app.onRender([&state](app::App& a, renderer::SpriteBatch& batch) {
        batch.drawSprite(a.whiteTexture(), state.player, {48.0f, 48.0f},
                         state.awaitingBind ? Color::fromRgb(0xFFD166)
                                            : Color::fromRgb(0x5AC8FA));
        // A little cross at the origin, so movement is visible against something.
        batch.drawSprite(a.whiteTexture(), {0.0f, 0.0f}, {400.0f, 2.0f}, Color::gray(0.25f));
        batch.drawSprite(a.whiteTexture(), {0.0f, 0.0f}, {2.0f, 400.0f}, Color::gray(0.25f));
    });

    return app.run();
}
