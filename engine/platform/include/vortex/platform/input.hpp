#pragma once
#include <memory>

namespace vortex::pf {

class IWindow; // forward declaration — consumers include window.hpp separately

enum class Key {
    // Alphabet
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // Number row
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    // Arrow keys
    Left, Right, Up, Down,
    // Control keys
    Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    // Numpad
    KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
    KPEnter, KPAdd, KPSubtract, KPMultiply, KPDivide,
    // Navigation
    Home, End, PageUp, PageDown, Insert, Delete,
    // Punctuation / misc
    GraveAccent, Minus, Equal,
    LeftBracket, RightBracket, Backslash,
    Semicolon, Apostrophe, Comma, Period, Slash,

    Count
};

enum class MouseButton { Left, Right, Middle, Count };

// The Xbox-style layout every modern pad reports itself as. A physical pad with a
// different arrangement is remapped to this by the platform layer, so gameplay only
// ever sees one shape of controller.
enum class GamepadButton {
    A, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start, Guide,
    LeftThumb, RightThumb,
    DPadUp, DPadRight, DPadDown, DPadLeft,
    Count
};

// Sticks report in [-1, 1] with +Y up. Triggers report in [0, 1] — the platform
// layer folds away the [-1, 1] resting-at-minus-one convention some backends use.
enum class GamepadAxis {
    LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger,
    Count
};

inline constexpr unsigned kMaxGamepads = 4;

class IInputProvider {
public:
    virtual ~IInputProvider() = default;

    [[nodiscard]] virtual bool isKeyDown(Key key)     const = 0;

    [[nodiscard]] virtual bool isKeyPressed(Key key)  const = 0;

    [[nodiscard]] virtual bool isKeyReleased(Key key) const = 0;

    [[nodiscard]] virtual bool isMouseDown(MouseButton btn)     const = 0;
    [[nodiscard]] virtual bool isMousePressed(MouseButton btn)  const = 0;
    [[nodiscard]] virtual bool isMouseReleased(MouseButton btn) const = 0;

    virtual void mousePosition(float& x, float& y) const = 0;

    [[nodiscard]] virtual float scrollDelta() const = 0;

    // Gamepads. `pad` is in [0, kMaxGamepads); everything below is false or 0 for a
    // pad that is not plugged in, so a game can query pad 0 unconditionally.
    [[nodiscard]] virtual bool isGamepadConnected(unsigned pad) const = 0;
    [[nodiscard]] virtual bool isGamepadButtonDown(unsigned pad, GamepadButton) const = 0;
    [[nodiscard]] virtual bool isGamepadButtonPressed(unsigned pad, GamepadButton) const = 0;
    [[nodiscard]] virtual bool isGamepadButtonReleased(unsigned pad, GamepadButton) const = 0;

    // Raw, with no dead zone applied — that is a game-tuning decision, and InputMap
    // is where it belongs.
    [[nodiscard]] virtual float gamepadAxis(unsigned pad, GamepadAxis) const = 0;

    virtual void newFrame() = 0;
};

[[nodiscard]] std::unique_ptr<IInputProvider> createInputProvider(IWindow& window);

}
