#pragma once
#include "vortex/core/types.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/ecs/systems.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/render_item.hpp"

#include <functional>
#include <vector>

namespace vortex::ecs {

// Thin gameplay facade over a Registry: holds the camera, a list of update
// systems, and the entry points that drive a frame. Keeps example/game code
// down to spawn() + add components + register systems.
class Scene {
public:
    using System = std::function<void(Registry&, f32)>;

    // Spawn an entity pre-fitted with a local + world transform.
    [[nodiscard]] Entity spawn() {
        const Entity e = m_registry.create();
        m_registry.emplace<Transform2D>(e);
        m_registry.emplace<WorldTransform2D>(e);
        return e;
    }

    void destroy(Entity e) { m_registry.destroy(e); }

    [[nodiscard]] Registry&       registry()       { return m_registry; }
    [[nodiscard]] const Registry& registry() const { return m_registry; }

    void addSystem(System system) { m_systems.push_back(std::move(system)); }

    // Run gameplay systems, then refresh the transform hierarchy.
    void update(f32 dt) {
        for (auto& system : m_systems) system(m_registry, dt);
        updateTransforms(m_registry);
    }

    // Produce the flat draw list for the renderer.
    void extract(std::vector<renderer::RenderItem>& out) {
        extractSprites(m_registry, out);
    }

    renderer::Camera2D camera;

private:
    Registry            m_registry;
    std::vector<System> m_systems;
};

}
