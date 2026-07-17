#include "vortex/ecs/systems.hpp"

#include "vortex/core/math/scalar.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/jobs/job_system.hpp"
#include "vortex/renderer/culling.hpp"
#include "vortex/renderer/sprite_batch.hpp"

#include <cmath>
#include <functional>

namespace vortex::ecs {

namespace {

Mat4 localMatrix(const Transform2D& t) {
    return Mat4::translation(t.position.x, t.position.y, 0.0f) *
           Mat4::rotationZ(t.rotation) *
           Mat4::scaling(t.scale.x, t.scale.y, 1.0f);
}

// A sprite is drawn only if it has a texture, and only if it survives culling.
bool spriteDrawn(const WorldTransform2D& world, const SpriteComp& sprite, const Rect* bounds) {
    if (!sprite.texture.valid()) return false;
    return bounds == nullptr ||
           renderer::quadVisible(world.matrix, sprite.size, sprite.anchorOffset(), *bounds);
}

// Fire every event on the frames playback stepped THROUGH, not just the one it landed on.
// A slow frame, or a clip whose fps is higher than the tick rate, advances several frames
// at once — and an event that only fires when you happen to land on its frame is an event
// that goes missing exactly when the machine is struggling.
void emitFramesEntered(Registry& registry, Entity e, const renderer::AnimationClip& clip,
                       u32 from, u32 to) {
    const auto count = static_cast<u32>(clip.frames.size());
    if (count == 0) return;

    u32 frame = from;
    for (u32 guard = 0; guard < count; ++guard) {   // bounded: a lap at most, wrap included
        frame = (frame + 1) % count;
        for (const renderer::AnimationEvent& event : clip.events)
            if (event.frame == frame)
                registry.emit(e, SpriteAnimationEvent{.entity = e, .name = event.name, .frame = frame});
        if (frame == to) break;
    }
}

renderer::RenderItem makeItem(const WorldTransform2D& world, const SpriteComp& sprite) {
    // Scale the unit quad to the sprite's size and slide it so the anchor lands on
    // the entity's origin. Both are diagonal-plus-translation, so they are written
    // straight into one matrix rather than composed with two 4x4 multiplies — this
    // runs once per visible sprite per frame.
    const Vec2 offset = sprite.anchorOffset();
    Mat4 local;
    local.at(0, 0) = sprite.size.x;
    local.at(1, 1) = sprite.size.y;
    local.at(0, 3) = offset.x;
    local.at(1, 3) = offset.y;

    // Y-sorting reads the entity's world position — element (1, 3) of its matrix, which
    // is where updateTransforms has just put it — rather than its local Transform2D, so
    // a sprite parented to something else still sorts where it actually ends up.
    i32 layer = sprite.layer;
    if (sprite.ySort) {
        const f32 worldY = world.matrix.at(1, 3) + sprite.ySortOffset;
        layer -= static_cast<i32>(std::lround(worldY));
    }

    return {
        .transform = world.matrix * local,
        .color     = sprite.color,
        .uv        = renderer::flippedUV(sprite.uv, sprite.flipX, sprite.flipY),
        .texture   = sprite.texture,
        .layer     = layer,
        .sampler   = sprite.sampler,
    };
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

void updateSpriteAnimations(Registry& registry, const renderer::AnimationLibrary& library, f32 dt) {
    registry.view<SpriteAnimator, SpriteComp>(
        [&](Entity e, SpriteAnimator& anim, SpriteComp& sprite) {
            const renderer::AnimationClip* clip = library.get(anim.clip);
            if (clip == nullptr || clip->frames.empty() || clip->fps <= 0.0f) return;

            const f32 duration = clip->duration();
            if (anim.playing && !anim.finished) anim.time += dt * anim.speed;

            if (clip->loop) {
                anim.time = wrap(anim.time, 0.0f, duration);
            } else if (anim.time >= duration) {
                anim.time     = duration;
                anim.finished = true;
            } else if (anim.time < 0.0f) {
                anim.time     = 0.0f;
                anim.finished = true;
            }

            const auto last  = static_cast<u32>(clip->frames.size() - 1);
            const auto index = static_cast<u32>(anim.time * clip->fps);
            const u32  next  = index < last ? index : last;

            if (!clip->events.empty()) {
                if (anim.freshClip) {
                    // The clip's first shown frame is entered, not stepped into.
                    for (const renderer::AnimationEvent& event : clip->events)
                        if (event.frame == next)
                            registry.emit(e, SpriteAnimationEvent{.entity = e,
                                                                  .name = event.name,
                                                                  .frame = next});
                } else if (next != anim.frame) {
                    emitFramesEntered(registry, e, *clip, anim.frame, next);
                }
            }
            anim.frame     = next;
            anim.freshClip = false;

            sprite.uv = clip->frames[anim.frame];
            if (clip->texture.valid()) sprite.texture = clip->texture;
        });
}

void extractSprites(Registry& registry, std::vector<renderer::RenderItem>& out,
                    const Rect* visibleBounds) {
    registry.view<WorldTransform2D, SpriteComp>(
        [&](Entity, WorldTransform2D& world, SpriteComp& sprite) {
            if (!spriteDrawn(world, sprite, visibleBounds)) return;
            out.push_back(makeItem(world, sprite));
        });
}

void extractSpritesParallel(Registry& registry, jobs::JobSystem& jobs,
                            std::vector<renderer::RenderItem>& out,
                            const Rect* visibleBounds) {
    // Cull on the gather walk, so the parallel pass only pays the matrix multiply
    // for sprites that actually reach the batcher. Gathering (rather than writing
    // straight into `out`) also keeps the result order-stable, which matters
    // because equal sort keys would otherwise shuffle between frames.
    struct Pair { const WorldTransform2D* world; const SpriteComp* sprite; };
    std::vector<Pair> pairs;
    registry.view<WorldTransform2D, SpriteComp>(
        [&](Entity, WorldTransform2D& world, SpriteComp& sprite) {
            if (!spriteDrawn(world, sprite, visibleBounds)) return;
            pairs.push_back({&world, &sprite});
        });

    // Append: whatever the caller already put in `out` (a tilemap, say) stays put.
    const usize base = out.size();
    out.resize(base + pairs.size());
    jobs.parallelFor(pairs.size(), [&](usize i) {
        out[base + i] = makeItem(*pairs[i].world, *pairs[i].sprite);
    });
}

void extractMeshes(Registry& registry, std::vector<renderer::MeshInstance>& out) {
    registry.view<Transform3D, MeshComp>(
        [&](Entity, Transform3D& t, MeshComp& mesh) {
            if (!mesh.mesh.valid()) return;
            const Mat4 model = Mat4::translation(t.position.x, t.position.y, t.position.z) *
                               t.rotation.toMat4() *
                               Mat4::scaling(t.scale.x, t.scale.y, t.scale.z);
            out.push_back({.mesh = mesh.mesh, .model = model,
                           .prevModel = mesh.hasPrevModel ? mesh.prevModel : model,
                           .hasPrevModel = true,
                           .color = mesh.color,
                           .metallic = mesh.metallic, .roughness = mesh.roughness,
                           .material = mesh.material,
                           .castsShadow = mesh.castsShadow,
                           .receivesShadow = mesh.receivesShadow});

            // Remember it for next frame. This is the one place extraction writes back,
            // and it writes nothing the game logic can see.
            mesh.prevModel    = model;
            mesh.hasPrevModel = true;
        });
}

}
