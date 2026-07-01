#include "vortex/rhi/device.hpp"
#include "vortex/core/log.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>

namespace vortex::rhi {

#if defined(VORTEX_HAVE_VULKAN)
std::unique_ptr<IGraphicsDevice> createVulkanDevice(pf::IWindow& window);
#endif
#if defined(VORTEX_HAVE_WEBGPU)
std::unique_ptr<IGraphicsDevice> createWebGPUDevice(pf::IWindow& window);
#endif

std::unique_ptr<IGraphicsDevice> createDevice(GraphicsAPI api, pf::IWindow& window) {
    switch (api) {
        case GraphicsAPI::Vulkan:
#if defined(VORTEX_HAVE_VULKAN)
            return createVulkanDevice(window);
#else
            VORTEX_ERROR("RHI", "Vulkan backend was not compiled in");
            return nullptr;
#endif
        case GraphicsAPI::WebGPU:
#if defined(VORTEX_HAVE_WEBGPU)
            return createWebGPUDevice(window);
#else
            VORTEX_ERROR("RHI", "WebGPU backend was not compiled in");
            return nullptr;
#endif
    }
    VORTEX_ERROR("RHI", "Unknown GraphicsAPI");
    return nullptr;
}

GraphicsAPI defaultGraphicsAPI() {
    if (const char* env = std::getenv("VORTEX_RHI_API")) {
        if (std::strcmp(env, "webgpu") == 0 || std::strcmp(env, "wgpu") == 0)
            return GraphicsAPI::WebGPU;
        if (std::strcmp(env, "vulkan") == 0 || std::strcmp(env, "vk") == 0)
            return GraphicsAPI::Vulkan;
        VORTEX_WARN("RHI", "Unknown VORTEX_RHI_API='%s' — using build default", env);
    }
#if defined(VORTEX_HAVE_VULKAN)
    return GraphicsAPI::Vulkan;
#else
    return GraphicsAPI::WebGPU;
#endif
}

std::unique_ptr<IGraphicsDevice> createDevice(pf::IWindow& window) {
    const GraphicsAPI api = defaultGraphicsAPI();
    VORTEX_INFO("RHI", "Selected graphics backend: %s",
                api == GraphicsAPI::Vulkan ? "Vulkan" : "WebGPU");
    return createDevice(api, window);
}

}
