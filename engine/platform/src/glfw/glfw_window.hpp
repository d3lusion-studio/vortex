#pragma once
// Internal header — only included by other files inside platform/src/glfw/.
#include "vortex/platform/window.hpp"
#include <GLFW/glfw3.h>

namespace vortex::pf {

class GlfwWindow final : public IWindow {
public:
    explicit GlfwWindow(const WindowDesc& desc);
    ~GlfwWindow() override;

    [[nodiscard]] bool shouldClose() const override;
    void pollEvents() override;
    void getFramebufferSize(int& w, int& h) const override;
    [[nodiscard]] void* nativeWindowHandle()  const override;
    [[nodiscard]] void* nativeDisplayHandle() const override;

    [[nodiscard]] GLFWwindow* glfwHandle() const noexcept { return m_window; }

private:
    GLFWwindow* m_window = nullptr;
};

}
