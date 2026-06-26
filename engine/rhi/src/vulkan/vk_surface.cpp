#if defined(VORTEX_HAVE_XLIB)
    #define VK_USE_PLATFORM_XLIB_KHR
#endif
#if defined(VORTEX_HAVE_WAYLAND)
    #define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#include "vk_surface.hpp"
#include "vortex/core/log.hpp"

#include <cstdint>

namespace vortex::rhi::vk {

VkSurfaceKHR createNativeSurface(VkInstance instance,
                                 pf::NativeHandleKind kind,
                                 void* displayHandle,
                                 void* windowHandle) {
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    switch (kind) {
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        case pf::NativeHandleKind::Xlib: {
            VkXlibSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
            ci.dpy    = static_cast<Display*>(displayHandle);
            ci.window = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(windowHandle));
            if (vkCreateXlibSurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS) {
                vortex::log(vortex::LogLevel::Error, "RHI", "vkCreateXlibSurfaceKHR failed");
                return VK_NULL_HANDLE;
            }
            return surface;
        }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
        case pf::NativeHandleKind::Wayland: {
            VkWaylandSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
            ci.display = static_cast<wl_display*>(displayHandle);
            ci.surface = static_cast<wl_surface*>(windowHandle);
            if (vkCreateWaylandSurfaceKHR(instance, &ci, nullptr, &surface) != VK_SUCCESS) {
                vortex::log(vortex::LogLevel::Error, "RHI", "vkCreateWaylandSurfaceKHR failed");
                return VK_NULL_HANDLE;
            }
            return surface;
        }
#endif
        default:
            break;
    }

    vortex::log(vortex::LogLevel::Error, "RHI",
                "Unsupported native window kind for Vulkan surface creation");
    return VK_NULL_HANDLE;
}

const char* surfaceExtensionName(pf::NativeHandleKind kind) {
    switch (kind) {
#if defined(VK_USE_PLATFORM_XLIB_KHR)
        case pf::NativeHandleKind::Xlib:    return VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
        case pf::NativeHandleKind::Wayland: return VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
#endif
        default:                            return nullptr;
    }
}

}
