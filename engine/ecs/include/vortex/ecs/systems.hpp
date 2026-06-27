#pragma once
#include "vortex/renderer/render_item.hpp"

#include <vector>

namespace vortex::jobs { class JobSystem; }

namespace vortex::ecs {

class Registry;

void updateTransforms(Registry& registry);

void extractSprites(Registry& registry, std::vector<renderer::RenderItem>& out);

// Same output as extractSprites, but the per-item matrix math is spread across
// the job system. Gathering the matching entities is serial (cheap); only the
// transform composition — the part that scales with entity count — runs in
// parallel. `out` ends up the same size and order as the serial version.
void extractSpritesParallel(Registry& registry, jobs::JobSystem& jobs,
                            std::vector<renderer::RenderItem>& out);

}
