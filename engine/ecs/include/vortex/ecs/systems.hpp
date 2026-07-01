#pragma once
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/render_item.hpp"

#include <vector>

namespace vortex::jobs { class JobSystem; }

namespace vortex::ecs {

class Registry;

void updateTransforms(Registry& registry);

void extractSprites(Registry& registry, std::vector<renderer::RenderItem>& out);

void extractSpritesParallel(Registry& registry, jobs::JobSystem& jobs,
                            std::vector<renderer::RenderItem>& out);

void extractMeshes(Registry& registry, std::vector<renderer::MeshInstance>& out);

}
