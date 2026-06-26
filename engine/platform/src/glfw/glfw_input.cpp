#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "glfw/glfw_window.hpp"  // for GlfwWindow::glfwHandle()
#include <GLFW/glfw3.h>
#include <cassert>
#include <cstring>

namespace vortex::pf {

static constexpr int s_glfwKeyMap[] = {
    // Alphabet A-Z
    GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E,
    GLFW_KEY_F, GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J,
    GLFW_KEY_K, GLFW_KEY_L, GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O,
    GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R, GLFW_KEY_S, GLFW_KEY_T,
    GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X, GLFW_KEY_Y, GLFW_KEY_Z,
    // Number row 0-9
    GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
    GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
    // Function keys F1-F12
    GLFW_KEY_F1,  GLFW_KEY_F2,  GLFW_KEY_F3,  GLFW_KEY_F4,
    GLFW_KEY_F5,  GLFW_KEY_F6,  GLFW_KEY_F7,  GLFW_KEY_F8,
    GLFW_KEY_F9,  GLFW_KEY_F10, GLFW_KEY_F11, GLFW_KEY_F12,
    // Arrow keys
    GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
    // Control keys
    GLFW_KEY_SPACE, GLFW_KEY_ENTER, GLFW_KEY_ESCAPE,
    GLFW_KEY_TAB,   GLFW_KEY_BACKSPACE,
    GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT,
    GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL,
    GLFW_KEY_LEFT_ALT,     GLFW_KEY_RIGHT_ALT,
    // Numpad
    GLFW_KEY_KP_0, GLFW_KEY_KP_1, GLFW_KEY_KP_2, GLFW_KEY_KP_3, GLFW_KEY_KP_4,
    GLFW_KEY_KP_5, GLFW_KEY_KP_6, GLFW_KEY_KP_7, GLFW_KEY_KP_8, GLFW_KEY_KP_9,
    GLFW_KEY_KP_ENTER, GLFW_KEY_KP_ADD, GLFW_KEY_KP_SUBTRACT,
    GLFW_KEY_KP_MULTIPLY, GLFW_KEY_KP_DIVIDE,
    // Navigation
    GLFW_KEY_HOME, GLFW_KEY_END,
    GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN,
    GLFW_KEY_INSERT, GLFW_KEY_DELETE,
    // Punctuation / misc
    GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL,
    GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH,
    GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE,
    GLFW_KEY_COMMA, GLFW_KEY_PERIOD, GLFW_KEY_SLASH,
};
static_assert(std::size(s_glfwKeyMap) == static_cast<std::size_t>(Key::Count),
              "s_glfwKeyMap size does not match Key::Count — update the table");

static constexpr int kKeyCount = static_cast<int>(Key::Count);
static constexpr int kBtnCount = static_cast<int>(MouseButton::Count);


class GlfwInput final : public IInputProvider {
public:
    explicit GlfwInput(GLFWwindow* win) : m_window(win) {
        glfwSetWindowUserPointer(win, this);
        glfwSetScrollCallback(win, &scrollCallback);
        newFrame();
    }

    ~GlfwInput() override {
        glfwSetScrollCallback(m_window, nullptr);
        glfwSetWindowUserPointer(m_window, nullptr);
    }

    bool isKeyDown(Key key) const override {
        int idx = static_cast<int>(key);
        if (idx < 0 || idx >= kKeyCount) return false;
        return glfwGetKey(m_window, s_glfwKeyMap[idx]) == GLFW_PRESS;
    }

    bool isKeyPressed(Key key) const override {
        int idx = static_cast<int>(key);
        if (idx < 0 || idx >= kKeyCount) return false;
        return glfwGetKey(m_window, s_glfwKeyMap[idx]) == GLFW_PRESS && !m_prevKeys[idx];
    }

    bool isKeyReleased(Key key) const override {
        int idx = static_cast<int>(key);
        if (idx < 0 || idx >= kKeyCount) return false;
        return glfwGetKey(m_window, s_glfwKeyMap[idx]) != GLFW_PRESS && m_prevKeys[idx];
    }

    bool isMouseDown(MouseButton btn) const override {
        int idx = static_cast<int>(btn);
        if (idx < 0 || idx >= kBtnCount) return false;
        return glfwGetMouseButton(m_window, idx) == GLFW_PRESS;
    }

    bool isMousePressed(MouseButton btn) const override {
        int idx = static_cast<int>(btn);
        if (idx < 0 || idx >= kBtnCount) return false;
        return glfwGetMouseButton(m_window, idx) == GLFW_PRESS && !m_prevBtns[idx];
    }

    bool isMouseReleased(MouseButton btn) const override {
        int idx = static_cast<int>(btn);
        if (idx < 0 || idx >= kBtnCount) return false;
        return glfwGetMouseButton(m_window, idx) != GLFW_PRESS && m_prevBtns[idx];
    }

    void mousePosition(float& x, float& y) const override {
        double mx, my;
        glfwGetCursorPos(m_window, &mx, &my);
        x = static_cast<float>(mx);
        y = static_cast<float>(my);
    }

    float scrollDelta() const override { return m_scrollDelta; }

    void newFrame() override {
        for (int i = 0; i < kKeyCount; ++i)
            m_prevKeys[i] = (glfwGetKey(m_window, s_glfwKeyMap[i]) == GLFW_PRESS);
        for (int i = 0; i < kBtnCount; ++i)
            m_prevBtns[i] = (glfwGetMouseButton(m_window, i) == GLFW_PRESS);
        m_scrollDelta = 0.0f;
    }

private:
    GLFWwindow* m_window;
    bool  m_prevKeys[kKeyCount] = {};
    bool  m_prevBtns[kBtnCount] = {};
    float m_scrollDelta = 0.0f;

    static void scrollCallback(GLFWwindow* win, double /*dx*/, double dy) {
        auto* self = static_cast<GlfwInput*>(glfwGetWindowUserPointer(win));
        self->m_scrollDelta += static_cast<float>(dy);
    }
};

std::unique_ptr<IInputProvider> createInputProvider(IWindow& window) {
    auto* glfw = static_cast<GlfwWindow*>(&window);
    return std::make_unique<GlfwInput>(glfw->glfwHandle());
}

}
