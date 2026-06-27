#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"

#include <functional>
#include <memory>

namespace vortex::ecs { class Registry; }

namespace vortex::physics {

class PhysicsWorld {
public:
    using ContactCallback = std::function<void(ecs::Entity, ecs::Entity)>;

    explicit PhysicsWorld(Vec2 gravity = {0.0f, -9.81f}, f32 pixelsPerMeter = 50.0f);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void step(ecs::Registry& registry, f32 dt);

    // Invoked once per pair when two shapes begin touching, during step().
    void setContactCallback(ContactCallback cb);
    void setGravity(Vec2 gravity);

    // Gameplay helpers — no-ops if the entity has no backing body yet.
    void applyLinearImpulse(ecs::Entity, Vec2 impulse);
    void setLinearVelocity(ecs::Entity, Vec2 velocity);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
