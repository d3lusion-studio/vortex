#pragma once
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"

namespace vortex::rhi {

class ISwapchain {
public:
    virtual ~ISwapchain() = default;

    [[nodiscard]] virtual Format format() const = 0;
    virtual void getExtent(u32& width, u32& height) const = 0;

    virtual void requestResize(u32 width, u32 height) = 0;
};

}
