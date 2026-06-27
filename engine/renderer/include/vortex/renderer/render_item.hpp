#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::renderer {

// Flat, renderer-facing draw record. The transform maps a unit quad centred at
// the origin (corners at +/-0.5) into world space, so it carries position,
// rotation, scale and sprite size together. Keeping a full Mat4 here (rather
// than 2D-only fields) is what lets a mesh RenderItem reuse the same path later.
struct RenderItem {
    Mat4               transform = Mat4::identity();
    Vec4               color{1.0f, 1.0f, 1.0f, 1.0f};
    Rect               uv = kFullUV;
    rhi::TextureHandle texture;
    i32                layer = 0;
};

}
