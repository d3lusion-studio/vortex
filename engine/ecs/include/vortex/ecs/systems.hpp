#pragma once
#include "vortex/renderer/render_item.hpp"

#include <vector>

namespace vortex::ecs {

class Registry;

// Compose each entity's local Transform2D with its parents and write the result
// into WorldTransform2D. Parents are resolved before children (memoised per
// call), so update order within the view does not matter.
void updateTransforms(Registry& registry);

// Read-only pass: gather (WorldTransform2D + SpriteComp) into a flat RenderItem
// array the renderer can consume directly. This is the ECS -> renderer bridge;
// the renderer never touches the registry.
void extractSprites(Registry& registry, std::vector<renderer::RenderItem>& out);

}
