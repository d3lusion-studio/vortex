#pragma once
#include "vortex/core/math/bounds3d.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/ecs/components.hpp"   // Transform3D
#include "vortex/ecs/entity.hpp"
#include "vortex/ecs/picking.hpp"      // HitResult, PickingSystem
#include "vortex/ecs/registry.hpp"
#include "vortex/renderer/camera.hpp"  // Camera, Ray (viewportToWorld)

#include <limits>
#include <vector>

// 3D mesh picking: cast a ray through the scene and find the entity it hits. It is
// the 3D backend for the same PickingSystem that drives 2D picking — plug it in with
// setBackend(), and hover/click/drag events fire for meshes exactly as they do for
// sprites.
//
// Each pickable carries its own collision proxy in LOCAL space: an AABB for cheap
// bounds picking, or a triangle soup for pixel-tight picking through concave shapes
// and holes. The proxy is deliberately independent of the GPU mesh, so picking works
// headlessly and a low-poly proxy can stand in for a dense render mesh.

namespace vortex::ecs {

// The pickable proxy of a 3D entity, in its local space. If `triangles` is non-empty
// (3 vertices per triangle) it is tested precisely; otherwise `bounds` is used.
struct MeshPickable {
    Aabb3D            bounds;      // local-space AABB
    std::vector<Vec3> triangles;  // optional: local-space triangle soup (v0,v1,v2, ...)
};

struct MeshHit {
    Entity entity;
    f32    distance = 0.0f;   // world units along the ray
    Vec3   point{0.0f, 0.0f, 0.0f};
    Vec3   normal{0.0f, 0.0f, 0.0f};

    [[nodiscard]] bool valid() const { return entity.valid(); }
};

// Nearest MeshPickable the world-space ray hits, skipping `ignore`. The ray is
// transformed into each entity's local space (so rotation and scale are respected),
// where the un-normalized local direction keeps the returned distance in world units.
[[nodiscard]] inline MeshHit pickMeshes(Registry& reg, const Ray3D& worldRay, Entity ignore = {}) {
    MeshHit best;
    f32     bestT = std::numeric_limits<f32>::max();

    reg.view<Transform3D, MeshPickable>([&](Entity e, Transform3D& t, MeshPickable& mp) {
        if (e == ignore) return;

        const Mat4 model = Mat4::translation(t.position.x, t.position.y, t.position.z) *
                           t.rotation.toMat4() *
                           Mat4::scaling(t.scale.x, t.scale.y, t.scale.z);
        const Mat4 inv = model.inverse();

        const Vec4  lo = inv * Vec4{worldRay.origin.x, worldRay.origin.y, worldRay.origin.z, 1.0f};
        const Vec4  ld = inv * Vec4{worldRay.direction.x, worldRay.direction.y, worldRay.direction.z, 0.0f};
        const Ray3D localRay{{lo.x, lo.y, lo.z}, {ld.x, ld.y, ld.z}};

        f32  tHit = -1.0f;
        Vec3 localNormal{0.0f, 0.0f, 0.0f};
        if (!mp.triangles.empty()) {
            for (usize i = 0; i + 3 <= mp.triangles.size(); i += 3) {
                Vec3      n;
                const f32 tt = rayTriangle(localRay, mp.triangles[i], mp.triangles[i + 1],
                                           mp.triangles[i + 2], &n);
                if (tt >= 0.0f && (tHit < 0.0f || tt < tHit)) { tHit = tt; localNormal = n; }
            }
        } else {
            tHit = raycast(localRay, mp.bounds);
        }

        if (tHit >= 0.0f && tHit < bestT) {
            bestT         = tHit;
            best.entity   = e;
            best.distance = tHit;
            best.point    = worldRay.at(tHit);
            const Vec4 wn = model * Vec4{localNormal.x, localNormal.y, localNormal.z, 0.0f};
            best.normal   = normalize(Vec3{wn.x, wn.y, wn.z});
        }
    });
    return best;
}

// A PickingSystem backend that picks 3D meshes. It reads the pointer's Vec2 as a
// viewport pixel, casts the camera ray through it, and reports the nearest mesh.
// `camera` must outlive the backend. Drop it into a PickingSystem with setBackend()
// and the 2D event flow (Over/Out/Down/Up/Click/Drag) now works on 3D meshes.
[[nodiscard]] inline PickingSystem::Backend meshPickBackend(const renderer::Camera& camera) {
    return [&camera](Registry& reg, Vec2 screen, Entity ignore) -> HitResult {
        const renderer::Ray r = camera.viewportToWorld(screen.x, screen.y);
        const MeshHit hit = pickMeshes(reg, Ray3D{r.origin, r.direction}, ignore);
        HitResult out;
        out.entity   = hit.entity;
        out.position = screen;   // for 2D events, "position" is where the pointer is
        return out;
    };
}

}
