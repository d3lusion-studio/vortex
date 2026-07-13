#include "vortex/platform/input_map.hpp"

#include "vortex/core/log.hpp"
#include "vortex/core/math/scalar.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace vortex::pf {

namespace {

[[nodiscard]] StringId hashOf(std::string_view name) {
    u64 hash = 1469598103934665603ull;   // FNV offset basis, as in string_id.hpp
    for (const char c : name) {
        hash ^= static_cast<u64>(static_cast<unsigned char>(c));
        hash *= 1099511628211ull;
    }
    return StringId{hash};
}

// Names, not indices, on the wire. A keybinds file is user data and outlives the
// build that wrote it; reordering an enum must not silently rebind their controls.
constexpr const char* kKeyNames[] = {
    "A","B","C","D","E","F","G","H","I","J","K","L","M",
    "N","O","P","Q","R","S","T","U","V","W","X","Y","Z",
    "0","1","2","3","4","5","6","7","8","9",
    "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12",
    "Left","Right","Up","Down",
    "Space","Enter","Escape","Tab","Backspace",
    "LeftShift","RightShift","LeftCtrl","RightCtrl","LeftAlt","RightAlt",
    "KP0","KP1","KP2","KP3","KP4","KP5","KP6","KP7","KP8","KP9",
    "KPEnter","KPAdd","KPSubtract","KPMultiply","KPDivide",
    "Home","End","PageUp","PageDown","Insert","Delete",
    "GraveAccent","Minus","Equal",
    "LeftBracket","RightBracket","Backslash",
    "Semicolon","Apostrophe","Comma","Period","Slash",
};
static_assert(std::size(kKeyNames) == static_cast<usize>(Key::Count),
              "kKeyNames does not match Key::Count — update the table");

constexpr const char* kMouseNames[] = {"Left", "Right", "Middle"};
static_assert(std::size(kMouseNames) == static_cast<usize>(MouseButton::Count),
              "kMouseNames does not match MouseButton::Count");

constexpr const char* kPadButtonNames[] = {
    "A","B","X","Y","LeftBumper","RightBumper","Back","Start","Guide",
    "LeftThumb","RightThumb","DPadUp","DPadRight","DPadDown","DPadLeft",
};
static_assert(std::size(kPadButtonNames) == static_cast<usize>(GamepadButton::Count),
              "kPadButtonNames does not match GamepadButton::Count");

constexpr const char* kPadAxisNames[] = {
    "LeftX","LeftY","RightX","RightY","LeftTrigger","RightTrigger",
};
static_assert(std::size(kPadAxisNames) == static_cast<usize>(GamepadAxis::Count),
              "kPadAxisNames does not match GamepadAxis::Count");

template <class Enum, usize N>
[[nodiscard]] bool enumFromName(const char* const (&names)[N], const std::string& name,
                                Enum& out) {
    for (usize i = 0; i < N; ++i) {
        if (name == names[i]) {
            out = static_cast<Enum>(i);
            return true;
        }
    }
    return false;
}

} // namespace

// ------------------------------------------------------------------- bindings

InputMap::Action* InputMap::find(std::string_view name) {
    const StringId id = hashOf(name);
    for (Action& action : m_actions)
        if (action.id == id) return &action;
    return nullptr;
}

const InputMap::Action* InputMap::find(std::string_view name) const {
    return const_cast<InputMap*>(this)->find(name);
}

InputMap::Action& InputMap::findOrAdd(std::string_view name) {
    if (Action* existing = find(name)) return *existing;

    Action action;
    action.id   = hashOf(name);
    action.name = std::string(name);
    m_actions.push_back(std::move(action));
    return m_actions.back();
}

void InputMap::bind(std::string_view action, Binding binding) {
    findOrAdd(action).bindings.push_back(binding);
}

void InputMap::bindAxis(std::string_view action, Binding positive, Binding negative) {
    Action& a = findOrAdd(action);
    a.bindings.push_back(positive);

    // The negative half is the same binding read backwards, so one action ends up
    // spanning [-1, 1] whichever device drives it.
    negative.scale = -std::fabs(negative.scale);
    a.bindings.push_back(negative);
}

void InputMap::clear(std::string_view action) {
    if (Action* a = find(action)) a->bindings.clear();
}

void InputMap::clearAll() { m_actions.clear(); }

void InputMap::rebind(std::string_view action, std::vector<Binding> bindings) {
    findOrAdd(action).bindings = std::move(bindings);
}

const std::vector<Binding>* InputMap::bindings(std::string_view action) const {
    const Action* a = find(action);
    return a != nullptr ? &a->bindings : nullptr;
}

// -------------------------------------------------------------------- sampling

f32 InputMap::sample(const Binding& b, const IInputProvider& input) const {
    switch (b.source) {
        case Binding::Source::Key:
            return input.isKeyDown(b.key) ? b.scale : 0.0f;

        case Binding::Source::Mouse:
            return input.isMouseDown(b.mouse) ? b.scale : 0.0f;

        case Binding::Source::PadButton:
            return input.isGamepadButtonDown(b.pad, b.button) ? b.scale : 0.0f;

        case Binding::Source::PadAxis: {
            const f32 raw = input.gamepadAxis(b.pad, b.axis) * b.scale;
            if (std::fabs(raw) < deadZone) return 0.0f;

            // Rescale what is left so the value still reaches 1 at full deflection —
            // otherwise the dead zone quietly costs the player their top speed.
            const f32 magnitude = (std::fabs(raw) - deadZone) / (1.0f - deadZone);
            return std::copysign(saturate(magnitude), raw);
        }

        case Binding::Source::None:
            return 0.0f;
    }
    return 0.0f;
}

void InputMap::update(const IInputProvider& input) {
    for (Action& action : m_actions) {
        // Strongest binding wins, so a stick pushed halfway does not get overridden
        // by an unpressed key reading 0 — nor the other way round.
        f32 value = 0.0f;
        for (const Binding& b : action.bindings) {
            const f32 sampled = sample(b, input);
            if (std::fabs(sampled) > std::fabs(value)) value = sampled;
        }

        action.prevDown = action.down;
        action.value    = clamp(value, -1.0f, 1.0f);
        action.down     = std::fabs(action.value) >= threshold;
    }
}

// -------------------------------------------------------------------- queries

bool InputMap::down(std::string_view action) const {
    const Action* a = find(action);
    return a != nullptr && a->down;
}

bool InputMap::pressed(std::string_view action) const {
    const Action* a = find(action);
    return a != nullptr && a->down && !a->prevDown;
}

bool InputMap::released(std::string_view action) const {
    const Action* a = find(action);
    return a != nullptr && !a->down && a->prevDown;
}

f32 InputMap::value(std::string_view action) const {
    const Action* a = find(action);
    return a != nullptr ? a->value : 0.0f;
}

Vec2 InputMap::vector(std::string_view xAction, std::string_view yAction) const {
    const Vec2 v{value(xAction), value(yAction)};

    // Clamp rather than normalise: normalising would snap a half-pushed stick to full
    // speed. Only the diagonal-is-faster case needs correcting.
    const f32 lengthSq = v.x * v.x + v.y * v.y;
    if (lengthSq <= 1.0f) return v;

    const f32 inv = 1.0f / std::sqrt(lengthSq);
    return {v.x * inv, v.y * inv};
}

// ------------------------------------------------------------- serialization

json::Value InputMap::save() const {
    json::Value out = json::Value::object();

    for (const Action& action : m_actions) {
        json::Value list = json::Value::array();
        for (const Binding& b : action.bindings) {
            json::Value v = json::Value::object();
            switch (b.source) {
                case Binding::Source::Key:
                    v.set("key", kKeyNames[static_cast<usize>(b.key)]);
                    break;
                case Binding::Source::Mouse:
                    v.set("mouse", kMouseNames[static_cast<usize>(b.mouse)]);
                    break;
                case Binding::Source::PadButton:
                    v.set("padButton", kPadButtonNames[static_cast<usize>(b.button)]);
                    v.set("pad", b.pad);
                    break;
                case Binding::Source::PadAxis:
                    v.set("padAxis", kPadAxisNames[static_cast<usize>(b.axis)]);
                    v.set("pad", b.pad);
                    break;
                case Binding::Source::None:
                    continue;
            }
            if (b.scale != 1.0f) v.set("scale", b.scale);
            list.push(std::move(v));
        }
        out.set(action.name, std::move(list));
    }

    return out;
}

bool InputMap::load(const json::Value& doc) {
    if (!doc.isObject()) {
        VORTEX_ERROR("Input", "not an input map document");
        return false;
    }

    clearAll();

    for (const auto& [name, list] : doc.fields()) {
        std::vector<Binding> bindings;
        for (const json::Value& v : list.items()) {
            Binding b;
            b.scale    = v["scale"].asF32(1.0f);
            b.pad      = v["pad"].asU32(0);

            if (v.contains("key")) {
                if (!enumFromName(kKeyNames, v["key"].asString(), b.key)) {
                    VORTEX_WARN("Input", "unknown key '%s' on action '%s', skipped",
                                v["key"].asString().c_str(), name.c_str());
                    continue;
                }
                b.source = Binding::Source::Key;
            } else if (v.contains("mouse")) {
                if (!enumFromName(kMouseNames, v["mouse"].asString(), b.mouse)) continue;
                b.source = Binding::Source::Mouse;
            } else if (v.contains("padButton")) {
                if (!enumFromName(kPadButtonNames, v["padButton"].asString(), b.button)) continue;
                b.source = Binding::Source::PadButton;
            } else if (v.contains("padAxis")) {
                if (!enumFromName(kPadAxisNames, v["padAxis"].asString(), b.axis)) continue;
                b.source = Binding::Source::PadAxis;
            } else {
                continue;   // no recognisable source; drop it rather than bind nothing
            }

            bindings.push_back(b);
        }
        rebind(name, std::move(bindings));
    }

    return true;
}

}
