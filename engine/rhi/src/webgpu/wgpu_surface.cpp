#include "wgpu_common.hpp"
#include "wgpu_device.hpp"

#include "vortex/platform/window.hpp"

#include <cstdint>

namespace vortex::rhi::wgpu {
    WGPUSurface createWGPUSurface(WGPUInstance instance, pf::NativeHandleKind kind,
                              void* display, void* window) {
    switch (kind) {
        case pf::NativeHandleKind::Xlib: {
            WGPUSurfaceDescriptorFromXlibWindow x{};
            x.chain.sType = WGPUSType_SurfaceDescriptorFromXlibWindow;
            x.display     = display;
            x.window      = static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(window));
            WGPUSurfaceDescriptor sd{};
            sd.nextInChain = &x.chain;
            return wgpuInstanceCreateSurface(instance, &sd);
        }
        case pf::NativeHandleKind::Wayland: {
            WGPUSurfaceDescriptorFromWaylandSurface w{};
            w.chain.sType = WGPUSType_SurfaceDescriptorFromWaylandSurface;
            w.display     = display;
            w.surface     = window;
            WGPUSurfaceDescriptor sd{};
            sd.nextInChain = &w.chain;
            return wgpuInstanceCreateSurface(instance, &sd);
        }
        default:
            VORTEX_ERROR("RHI", "WebGPU: unsupported native window kind for surface creation");
            return nullptr;
    }
}

}
