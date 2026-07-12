#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

namespace vortex::renderer {

struct Camera2D {
    Vec2 position{0.0f, 0.0f};
    f32  zoom = 1.0f;
    f32  viewportWidth  = 1.0f;
    f32  viewportHeight = 1.0f;

    [[nodiscard]] Mat4 projection() const {
        const f32 halfW = viewportWidth  * 0.5f;
        const f32 halfH = viewportHeight * 0.5f;
        return Mat4::ortho(-halfW, halfW, -halfH, halfH, -1.0f, 1.0f);
    }

    [[nodiscard]] Mat4 view() const {
        return Mat4::scaling(zoom, zoom, 1.0f) *
               Mat4::translation(-position.x, -position.y, 0.0f);
    }

    [[nodiscard]] Mat4 viewProjection() const {
        return projection() * view();
    }

    [[nodiscard]] Vec2 screenToWorld(f32 sx, f32 sy) const {
        const f32 wx = (sx - viewportWidth  * 0.5f) / zoom + position.x;
        const f32 wy = -(sy - viewportHeight * 0.5f) / zoom + position.y;
        return {wx, wy};
    }

    [[nodiscard]] Vec2 worldToScreen(Vec2 world) const {
        return {(world.x - position.x) * zoom + viewportWidth  * 0.5f,
                -(world.y - position.y) * zoom + viewportHeight * 0.5f};
    }

    // World-space AABB of everything the camera can see. Feed this to sprite
    // culling; `padding` grows it, which keeps sprites whose pivot has left the
    // frame but whose body has not from popping out.
    [[nodiscard]] Rect visibleBounds(f32 padding = 0.0f) const {
        const f32 halfW = viewportWidth  * 0.5f / zoom;
        const f32 halfH = viewportHeight * 0.5f / zoom;
        return Rect::fromCenter(position, {halfW * 2.0f, halfH * 2.0f}).expanded(padding);
    }
};

}
