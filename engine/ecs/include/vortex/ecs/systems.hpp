#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/renderer/sprite_animation.hpp"

#include <vector>

namespace vortex::jobs { class JobSystem; }

namespace vortex::ecs {

class Registry;

void updateTransforms(Registry& registry);

// Advance every SpriteAnimator and write the resolved frame onto its SpriteComp.
// Run this before extraction so the frame drawn is the frame just computed.
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
