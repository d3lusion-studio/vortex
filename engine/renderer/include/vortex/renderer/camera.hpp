#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"

namespace vortex::renderer {

struct Camera {
    enum class Mode { Orthographic, Perspective };
    Mode mode = Mode::Orthographic;

    Vec3 position{0.0f, 0.0f, 0.0f};

    f32 viewportWidth  = 1.0f;
    f32 viewportHeight = 1.0f;
    f32 zoom = 1.0f;

    Vec3 target{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    f32  fovYRadians = 1.0471975512f;   // 60 degrees
    f32  aspect      = 1.0f;
    f32  nearZ       = 0.1f;
    f32  farZ        = 1000.0f;

    [[nodiscard]] Mat4 projection() const {
        if (mode == Mode::Perspective)
            return Mat4::perspective(fovYRadians, aspect, nearZ, farZ);
        const f32 hw = viewportWidth * 0.5f, hh = viewportHeight * 0.5f;
        return Mat4::ortho(-hw, hw, -hh, hh, -1.0f, 1.0f);
    }

    [[nodiscard]] Mat4 view() const {
        if (mode == Mode::Perspective)
            return Mat4::lookAt(position, target, up);
        return Mat4::scaling(zoom, zoom, 1.0f) *
               Mat4::translation(-position.x, -position.y, 0.0f);
    }

    [[nodiscard]] Mat4 viewProjection() const { return projection() * view(); }

    [[nodiscard]] Vec2 screenToWorld(f32 sx, f32 sy) const {
        const f32 wx = (sx - viewportWidth  * 0.5f) / zoom + position.x;
        const f32 wy = -(sy - viewportHeight * 0.5f) / zoom + position.y;
        return {wx, wy};
    }
};

}
