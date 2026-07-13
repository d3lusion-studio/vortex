#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::renderer {

enum class SpriteSampler : u8 {
    LinearClamp,
    NearestClamp,
    LinearRepeat,
    NearestRepeat,
};

inline constexpr u32 kSpriteSamplerCount = 4;

struct RenderItem {
    Mat4               transform = Mat4::identity();
    Vec4               color{1.0f, 1.0f, 1.0f, 1.0f};
    Rect               uv = kFullUV;
    rhi::TextureHandle texture;
    i32                layer = 0;
    SpriteSampler      sampler = SpriteSampler::LinearClamp;
};

}
