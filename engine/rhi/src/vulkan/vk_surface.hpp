#pragma once
#include "vortex/platform/window.hpp"
#include <vulkan/vulkan.h>

namespace vortex::rhi::vk {

[[nodiscard]] VkSurfaceKHR createNativeSurface(VkInstance instance,
                                               pf::NativeHandleKind kind,
                                               void* displayHandle,
                                               void* windowHandle);

[[nodiscard]] const char* surfaceExtensionName(pf::NativeHandleKind kind);

}
