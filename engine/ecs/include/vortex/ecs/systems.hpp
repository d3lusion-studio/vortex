#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/renderer/sprite_animation.hpp"

#include <string_view>
#include <vector>

namespace vortex::jobs { class JobSystem; }

namespace vortex::ecs {

class Registry;

void updateTransforms(Registry& registry);

// Advance every SpriteAnimator and write the resolved frame onto its SpriteComp.
// Run this before extraction so the frame drawn is the frame just computed.
// Emitted at the entity whose animation reached a frame carrying an event — the hit frame
// of a swing, the footfall of a walk. Observe it like any other event:
//
//   registry.observe<SpriteAnimationEvent>([](ecs::Trigger<SpriteAnimationEvent>& t) {
//       if (t.event.name == "hit") ...
//   });
//
// `name` points into the clip, which the library owns and never frees, so it is valid for
// the whole call. Copy it if you keep it past the observer.
//
// Observers run inside the animation walk: emplacing or destroying entities from one is
// the usual iterator hazard, so defer that through a CommandBuffer.
struct SpriteAnimationEvent {
    Entity           entity;
    std::string_view name;
    u32              frame = 0;
};

void updateSpriteAnimations(Registry& registry, const renderer::AnimationLibrary& library, f32 dt);

// Build the flat draw list. Passing `visibleBounds` (see Camera2D::visibleBounds)
// rejects off-screen sprites before their world matrix is composed, so a culled
// sprite costs an AABB test rather than a 4x4 multiply.
//
// Every extract* function APPENDS. The caller clears, which is what lets one draw
// list carry a tilemap, the sprites and the particles in a single sorted batch.
void extractSprites(Registry& registry, std::vector<renderer::RenderItem>& out,
                    const Rect* visibleBounds = nullptr);

void extractSpritesParallel(Registry& registry, jobs::JobSystem& jobs,
                            std::vector<renderer::RenderItem>& out,
                            const Rect* visibleBounds = nullptr);

void extractMeshes(Registry& registry, std::vector<renderer::MeshInstance>& out);

}
