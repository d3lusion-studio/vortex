#pragma once
#include "vortex/core/json.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/string_id.hpp"
#include "vortex/core/types.hpp"
#include "vortex/platform/input.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vortex::pf {

// One physical thing an action can be triggered by. A binding is analog: a key
// reads 0 or 1, a stick reads its deflection. Digital queries threshold it, so
// "is the player moving right" works the same whether they used D or a thumbstick.
struct Binding {
    enum class Source : u8 { None, Key, Mouse, PadButton, PadAxis };

    Source        source = Source::None;
    Key           key    = Key::A;
    MouseButton   mouse  = MouseButton::Left;
    GamepadButton button = GamepadButton::A;
    GamepadAxis   axis   = GamepadAxis::LeftX;

    // Applied to an axis before anything else. Use -1 to bind "left" to the same
    // stick axis as "right", or 1 to leave it alone.
    f32 scale = 1.0f;

    unsigned pad = 0;

    [[nodiscard]] static Binding ofKey(Key k)               { return {.source = Source::Key,   .key = k}; }
    [[nodiscard]] static Binding ofMouse(MouseButton b)     { return {.source = Source::Mouse, .mouse = b}; }
    [[nodiscard]] static Binding ofPad(GamepadButton b, unsigned pad = 0) {
        return {.source = Source::PadButton, .button = b, .pad = pad};
    }
    [[nodiscard]] static Binding ofAxis(GamepadAxis a, f32 scale = 1.0f, unsigned pad = 0) {
        return {.source = Source::PadAxis, .axis = a, .scale = scale, .pad = pad};
    }
};

// Named actions, each fed by any number of bindings. Gameplay asks "is `jump` down",
// never "is Space down", so rebinding a control — or adding a gamepad — touches this
// table and nothing else.
//
// Sample it once a frame with update(), then query as often as you like: every query
// reads the sampled state, so two calls in the same frame cannot disagree.
class InputMap {
public:
    // Several bindings on one action are OR'd: any of them fires it, and the analog
    // value is the largest magnitude among them. Binding both D and a stick's +X to
    // `right` is the normal case, not a special one.
    void bind(std::string_view action, Binding binding);

    // Sugar for the overwhelmingly common shape: an axis built from two opposing
    // buttons, plus (optionally) a real analog axis that already spans both.
    void bindAxis(std::string_view action, Binding positive, Binding negative);

    void clear(std::string_view action);   // drops every binding on it
    void clearAll();

    // Replace the bindings on an action outright — this is what a "press a key to
    // rebind" screen calls once the player has chosen.
    void rebind(std::string_view action, std::vector<Binding> bindings);

    [[nodiscard]] const std::vector<Binding>* bindings(std::string_view action) const;

    // Reads the device once and latches everything. Call it after the window has
    // pumped its events and before any gameplay runs; App does.
    void update(const IInputProvider& input);

    // Digital. An analog binding counts as down once it passes `threshold`.
    [[nodiscard]] bool down(std::string_view action) const;
    [[nodiscard]] bool pressed(std::string_view action) const;    // went down this frame
    [[nodiscard]] bool released(std::string_view action) const;   // went up this frame

    // Analog, in [-1, 1]. A digital binding reads 0 or 1.
    [[nodiscard]] f32 value(std::string_view action) const;

    // Two actions as a vector, length-clamped to 1. This is what a movement stick
    // should feed into — taking value(x) and value(y) separately lets a keyboard
    // player move faster diagonally, which is the oldest bug in the genre.
    [[nodiscard]] Vec2 vector(std::string_view xAction, std::string_view yAction) const;

    // Anything below this on a stick is treated as zero. Sticks do not rest at
    // exactly zero, and without this every idle pad drifts the player.
    f32 deadZone = 0.2f;

    // How far an analog binding must travel to count as "down".
    f32 threshold = 0.5f;

    // Round-trips the whole table, so a keybinds screen is a save away from working.
    [[nodiscard]] json::Value save() const;
    bool                      load(const json::Value& doc);

private:
    struct Action {
        StringId             id;
        std::string          name;   // kept for save(); lookups go through the hash
        std::vector<Binding> bindings;
        f32                  value    = 0.0f;
        bool                 down     = false;
        bool                 prevDown = false;
    };

    [[nodiscard]] Action*       find(std::string_view name);
    [[nodiscard]] const Action* find(std::string_view name) const;
    Action&                     findOrAdd(std::string_view name);

    [[nodiscard]] f32 sample(const Binding&, const IInputProvider&) const;

    std::vector<Action> m_actions;
};

}
