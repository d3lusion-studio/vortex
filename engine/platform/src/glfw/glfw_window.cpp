#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "glfw/glfw_window.hpp"
#include "vortex/core/assert.hpp"
#include "vortex/core/log.hpp"
#include <GLFW/glfw3native.h>
#include <cstdlib>

#if defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
    #include <windows.h>
#endif

namespace vortex::pf {

GlfwWindow::GlfwWindow(const WindowDesc& desc) {

    if (std::getenv("WAYLAND_DISPLAY") && !std::getenv("GLFW_PLATFORM")) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
        VORTEX_INFO("Platform", "Wayland detected — using X11/XWayland (no swapchain yet)");
    }

    if (!glfwInit()) {
        VORTEX_ERROR("Platform", "glfwInit() failed");
        return;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  desc.resizable ? GLFW_TRUE : GLFW_FALSE);

    m_window = glfwCreateWindow(desc.width, desc.height, desc.title, nullptr, nullptr);
    VORTEX_ASSERT(m_window != nullptr, "glfwCreateWindow failed");

    VORTEX_INFO("Platform", "Window created: %dx%d \"%s\"", desc.width, desc.height, desc.title);
}

GlfwWindow::~GlfwWindow() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    glfwTerminate();
    VORTEX_INFO("Platform", "Window destroyed");
}

bool GlfwWindow::shouldClose() const {
    return glfwWindowShouldClose(m_window) != 0;
}

void GlfwWindow::pollEvents() {
    glfwPollEvents();
}

void GlfwWindow::getFramebufferSize(int& w, int& h) const {
    glfwGetFramebufferSize(m_window, &w, &h);
}

void* GlfwWindow::nativeWindowHandle() const {
    switch (glfwGetPlatform()) {
#if defined(GLFW_EXPOSE_NATIVE_X11)
        case GLFW_PLATFORM_X11:
            return reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(m_window)));
#endif
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        case GLFW_PLATFORM_WAYLAND:
            return static_cast<void*>(glfwGetWaylandWindow(m_window));
#endif
#if defined(GLFW_EXPOSE_NATIVE_WIN32)
        case GLFW_PLATFORM_WIN32:
            return static_cast<void*>(glfwGetWin32Window(m_window));
#endif
        default:
            return nullptr;
    }
}

void* GlfwWindow::nativeDisplayHandle() const {
    switch (glfwGetPlatform()) {
#if defined(GLFW_EXPOSE_NATIVE_X11)
        case GLFW_PLATFORM_X11:
            return static_cast<void*>(glfwGetX11Display());
#endif
#if defined(GLFW_EXPOSE_NATIVE_WAYLAND)
        case GLFW_PLATFORM_WAYLAND:
            return static_cast<void*>(glfwGetWaylandDisplay());
#endif
#if defined(GLFW_EXPOSE_NATIVE_WIN32)
        case GLFW_PLATFORM_WIN32:
            return static_cast<void*>(GetModuleHandleA(nullptr));
#endif
        default:
            return nullptr;
    }
}

std::unique_ptr<IWindow> createWindow(const WindowDesc& desc) {
    return std::make_unique<GlfwWindow>(desc);
}

} // namespace vortex::pf
