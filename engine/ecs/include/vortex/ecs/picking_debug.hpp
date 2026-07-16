#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/ecs/components.hpp"   // Transform3D
#include "vortex/ecs/picking3d.hpp"    // MeshPickable, MeshHit, pickMeshes
#include "vortex/ecs/registry.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/gizmos3d.hpp"

// Picking debug tools: draw what the picker sees. Given the camera and the pointer,
// it casts the pick ray, marks where it lands, and outlines the entity under it —
// the overlay you turn on to understand why a click did (or did not) select what you
// expected. It draws through Gizmos3D and reuses the very ray/pick path production
// picking uses, so the overlay can never disagree with the real result.

namespace vortex::ecs {

struct PickDebugStyle {
    Vec4 rayColor    = {0.55f, 0.60f, 0.70f, 1.0f};
    Vec4 hitColor    = {1.0f, 0.85f, 0.2f, 1.0f};
    Vec4 boundsColor = {0.25f, 1.0f, 0.45f, 1.0f};
    f32  rayLength   = 50.0f;    // how far to draw the ray when it hits nothing
    f32  hitSize     = 0.12f;    // half-size of the box drawn at the hit point
};

// Draw the pick debug for `pointerScreen` (viewport pixels) and return the hit. The
// ray is drawn to the hit (or out to rayLength on a miss); on a hit, a box marks the
// point and the hovered entity's pickable bounds are outlined.
inline MeshHit drawMeshPickDebug(renderer::Gizmos3D& giz, Registry& reg,
                                 const renderer::Camera& camera, Vec2 pointerScreen,
                                 const PickDebugStyle& style = {}) {
    const renderer::Ray r = camera.viewportToWorld(pointerScreen.x, pointerScreen.y);
    const MeshHit hit = pickMeshes(reg, Ray3D{r.origin, r.direction});

    if (!hit.valid()) {
        giz.ray(r.origin, r.direction, style.rayLength, style.rayColor);
        return hit;
    }

    giz.line(r.origin, hit.point, style.rayColor);
    giz.box(hit.point, {style.hitSize, style.hitSize, style.hitSize}, style.hitColor);

    // Outline the hovered entity's pickable box in its own oriented frame.
    if (const MeshPickable* mp = reg.tryGet<MeshPickable>(hit.entity)) {
        const Transform3D* t = reg.tryGet<Transform3D>(hit.entity);
        Mat4 model = Mat4::identity();
        if (t) model = Mat4::translation(t->position.x, t->position.y, t->position.z) *
                       t->rotation.toMat4() *
                       Mat4::scaling(t->scale.x, t->scale.y, t->scale.z);
        const Vec3 c = mp->bounds.center();
        const Vec3 h = mp->bounds.halfSize();
        giz.orientedBox(model * Mat4::translation(c.x, c.y, c.z), h, style.boundsColor);
    }
    return hit;
}

}
