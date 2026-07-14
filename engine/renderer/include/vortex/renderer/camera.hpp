#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"

namespace vortex::renderer {

// A ray in world space, as produced by Camera::viewportToWorld().
struct Ray {
    Vec3 origin;
    Vec3 direction;   // normalised

    [[nodiscard]] Vec3 at(f32 t) const { return origin + direction * t; }
};

struct Camera {
    // Orthographic is the 2D camera: it ignores `target`/`up` and maps world units
    // straight onto the viewport. Orthographic3D is the CAD/isometric camera — it
    // looks from `position` at `target` like a perspective camera, but with a box
    // frustum, so parallel lines stay parallel and distance does not shrink things.
    enum class Mode { Orthographic, Perspective, Orthographic3D };
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

    // Orthographic3D only: half-height of the view box, in world units. The width
    // follows from `aspect`.
    f32  orthoHeight = 5.0f;

    [[nodiscard]] Mat4 projection() const {
        switch (mode) {
            case Mode::Perspective:
                return Mat4::perspective(fovYRadians, aspect, nearZ, farZ);
            case Mode::Orthographic3D: {
                const f32 hh = orthoHeight, hw = orthoHeight * aspect;
                return Mat4::orthoRH(-hw, hw, -hh, hh, nearZ, farZ);
            }
            case Mode::Orthographic:
            default: {
                const f32 hw = viewportWidth * 0.5f, hh = viewportHeight * 0.5f;
                return Mat4::ortho(-hw, hw, -hh, hh, -1.0f, 1.0f);
            }
        }
    }

    [[nodiscard]] Mat4 view() const {
        if (mode == Mode::Perspective || mode == Mode::Orthographic3D)
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

    // The world-space ray through a viewport pixel — (0,0) is the top-left corner,
    // (viewportWidth, viewportHeight) the bottom-right. This is what turns a mouse
    // position into something you can intersect the scene with (see MeshRayCast).
    //
    // It unprojects two points on the pixel's line of sight (near and far plane)
    // rather than special-casing each projection, so it holds for perspective and
    // orthographic alike.
    [[nodiscard]] Ray viewportToWorld(f32 sx, f32 sy) const {
        // Pixel -> normalised device coordinates. Y is not flipped here: the engine's
        // projection matrices already carry the Vulkan Y-down flip, so an NDC built
        // the same way round-trips through the inverse correctly.
        const f32 ndcX = (sx / viewportWidth)  * 2.0f - 1.0f;
        const f32 ndcY = (sy / viewportHeight) * 2.0f - 1.0f;

        const Mat4 invVP = viewProjection().inverse();

        const Vec4 nearH = invVP * Vec4{ndcX, ndcY, 0.0f, 1.0f};
        const Vec4 farH  = invVP * Vec4{ndcX, ndcY, 1.0f, 1.0f};

        const Vec3 nearP{nearH.x / nearH.w, nearH.y / nearH.w, nearH.z / nearH.w};
        const Vec3 farP{farH.x / farH.w, farH.y / farH.w, farH.z / farH.w};

        return {nearP, normalize(farP - nearP)};
    }
};

}
