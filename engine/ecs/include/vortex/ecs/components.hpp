#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::ecs {


struct Transform2D {
    Vec2 position{0.0f, 0.0f};
    f32  rotation = 0.0f;   // radians, +Z (counter-clockwise)
    Vec2 scale{1.0f, 1.0f};
};

struct WorldTransform2D {
    Mat4 matrix = Mat4::identity();
};

struct Parent {
    Entity value;
};

struct SpriteComp {
    rhi::TextureHandle texture;
    Vec4               color{1.0f, 1.0f, 1.0f, 1.0f};
    Rect               uv = kFullUV;
    Vec2               size{1.0f, 1.0f};   // local quad size before transform
    i32                layer = 0;          // painter order
};

struct Velocity {
    Vec2 value{0.0f, 0.0f};
};

}
