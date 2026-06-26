#pragma once
#include <memory>

namespace vortex::pf {

struct WindowDesc {
    int         width     = 1280;
    int         height    = 720;
    const char* title     = "Vortex";
    bool        resizable = true;
};

enum class NativeHandleKind { Unknown, Xlib, Wayland, Win32, Cocoa };

class IWindow {
public:
    virtual ~IWindow() = default;

    [[nodiscard]] virtual bool shouldClose() const = 0;
    virtual void pollEvents() = 0;
    virtual void getFramebufferSize(int& w, int& h) const = 0;

    [[nodiscard]] virtual NativeHandleKind nativeHandleKind() const = 0;
    [[nodiscard]] virtual void* nativeWindowHandle()  const = 0;
    [[nodiscard]] virtual void* nativeDisplayHandle() const = 0;
};

[[nodiscard]] std::unique_ptr<IWindow> createWindow(const WindowDesc& desc);

}
