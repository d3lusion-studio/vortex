#include "vortex/ecs/systems.hpp"

#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/jobs/job_system.hpp"

#include <functional>

namespace vortex::ecs {

namespace {

Mat4 localMatrix(const Transform2D& t) {
    return Mat4::translation(t.position.x, t.position.y, 0.0f) *
           Mat4::rotationZ(t.rotation) *
           Mat4::scaling(t.scale.x, t.scale.y, 1.0f);
}

} // namespace

void updateTransforms(Registry& registry) {
    // done[index] marks entities whose world matrix is finalised this frame, so a
    // shared parent is computed once even when many children reference it.
    std::vector<u8> done(registry.capacity(), 0u);

    std::function<Mat4(Entity)> resolve = [&](Entity e) -> Mat4 {
        WorldTransform2D* world = registry.tryGet<WorldTransform2D>(e);
        if (e.index < done.size() && done[e.index] && world)
            return world->matrix;

        Transform2D* local = registry.tryGet<Transform2D>(e);
        Mat4 matrix = local ? localMatrix(*local) : Mat4::identity();

        if (Parent* parent = registry.tryGet<Parent>(e);
            parent && registry.alive(parent->value))
            matrix = resolve(parent->value) * matrix;

        if (world) world->matrix = matrix;
        if (e.index < done.size()) done[e.index] = 1u;
        return matrix;
    };

    registry.view<Transform2D, WorldTransform2D>(
        [&](Entity e, Transform2D&, WorldTransform2D&) { resolve(e); });
}

void extractSprites(Registry& registry, std::vector<renderer::RenderItem>& out) {
    out.clear();
    registry.view<WorldTransform2D, SpriteComp>(
        [&](Entity, WorldTransform2D& world, SpriteComp& sprite) {
            if (!sprite.texture.valid()) return;
            out.push_back({
                .transform = world.matrix * Mat4::scaling(sprite.size.x, sprite.size.y, 1.0f),
                .color     = sprite.color,
                .uv        = sprite.uv,
                .texture   = sprite.texture,
                .layer     = sprite.layer,
            });
        });
}

void extractSpritesParallel(Registry& registry, jobs::JobSystem& jobs,
                            std::vector<renderer::RenderItem>& out) {
    struct Pair { const WorldTransform2D* world; const SpriteComp* sprite; };
    std::vector<Pair> pairs;
    registry.view<WorldTransform2D, SpriteComp>(
        [&](Entity, WorldTransform2D& world, SpriteComp& sprite) {
            if (!sprite.texture.valid()) return;   // nothing to bind/draw
            pairs.push_back({&world, &sprite});
        });

    out.resize(pairs.size());
    jobs.parallelFor(pairs.size(), [&](usize i) {
        const WorldTransform2D& world  = *pairs[i].world;
        const SpriteComp&       sprite = *pairs[i].sprite;
        out[i] = {
            .transform = world.matrix * Mat4::scaling(sprite.size.x, sprite.size.y, 1.0f),
            .color     = sprite.color,
            .uv        = sprite.uv,
            .texture   = sprite.texture,
            .layer     = sprite.layer,
        };
    });
}

void extractMeshes(Registry& registry, std::vector<renderer::MeshInstance>& out) {
    out.clear();
    registry.view<Transform3D, MeshComp>(
        [&](Entity, Transform3D& t, MeshComp& mesh) {
            if (!mesh.mesh.valid()) return;
            const Mat4 model = Mat4::translation(t.position.x, t.position.y, t.position.z) *
                               t.rotation.toMat4() *
                               Mat4::scaling(t.scale.x, t.scale.y, t.scale.z);
            out.push_back({.mesh = mesh.mesh, .model = model, .color = mesh.color});
        });
}

}
