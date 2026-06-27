#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::assets {

struct TextureTag {};
using TextureHandle = Handle<TextureTag>;

struct TextureAsset {
    rhi::TextureHandle gpu;
    u32                width  = 0;
    u32                height = 0;
};

}
