#pragma once
#include "vortex/core/types.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/ecs/systems.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/particles.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/renderer/sprite_animation.hpp"
#include "vortex/renderer/tilemap.hpp"

#include <functional>
#include <vector>

namespace vortex::jobs { class JobSystem; }

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

    // Run gameplay systems, then animation, then refresh the transform hierarchy,
    // then step the particle emitters.
    void update(f32 dt) {
        updateCommon(dt);
        particles.update(dt);
    }

    // Same, but particle integration is spread across the job system.
    void update(f32 dt, jobs::JobSystem& jobs) {
        updateCommon(dt);
        particles.update(dt, jobs);
    }

    // Produce the flat draw list for the renderer, culled to the camera unless
    // `cullingEnabled` is turned off. Records how many sprites survived so callers
    // can report it without walking the list again.
    // Tilemap, then sprites, then particles — all into one list, so the batcher
    // sorts and merges them together instead of taking three passes at the GPU.
    void extract(std::vector<renderer::RenderItem>& out) {
        const Rect  bounds = camera.visibleBounds(cullPadding);
        const Rect* cull   = cullingEnabled ? &bounds : nullptr;

        out.clear();
        tilemap.extract(out, cull);
        const usize beforeSprites = out.size();
        extractSprites(m_registry, out, cull);
        m_visibleSprites = out.size() - beforeSprites;
        particles.extract(out, cull);
    }

    // Same, but the world-matrix composition is spread across the job system.
    // Worth it once the visible set runs into the thousands.
    void extract(std::vector<renderer::RenderItem>& out, jobs::JobSystem& jobs) {
        const Rect  bounds = camera.visibleBounds(cullPadding);
        const Rect* cull   = cullingEnabled ? &bounds : nullptr;

        out.clear();
        tilemap.extract(out, cull);
        const usize beforeSprites = out.size();
        extractSpritesParallel(m_registry, jobs, out, cull);
        m_visibleSprites = out.size() - beforeSprites;
        particles.extract(out, cull);
    }

    [[nodiscard]] usize visibleSprites() const { return m_visibleSprites; }

    renderer::Camera2D         camera;
    renderer::AnimationLibrary animations;
    renderer::ParticleWorld    particles;
    renderer::Tilemap          tilemap;

    bool cullingEnabled = true;
    f32  cullPadding    = 0.0f;   // world units added to the camera bounds

private:
    void updateCommon(f32 dt) {
        for (auto& system : m_systems) system(m_registry, dt);
        updateSpriteAnimations(m_registry, animations, dt);
        updateTransforms(m_registry);
    }

    Registry            m_registry;
    std::vector<System> m_systems;
    usize               m_visibleSprites = 0;
};

}
