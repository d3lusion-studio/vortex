#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace vortex::ecs { class Registry; }

namespace vortex::physics {

struct JointTag {};
using JointHandle = Handle<JointTag>;

struct RaycastHit {
    ecs::Entity entity;
    Vec2        point{0.0f, 0.0f};    // world space
    Vec2        normal{0.0f, 0.0f};   // points out of the surface that was hit
    f32         fraction = 0.0f;      // position along the ray, in [0, 1]

    [[nodiscard]] bool hit() const noexcept { return entity.valid(); }
};

struct PhysicsConfig {
    Vec2 gravity{0.0f, -9.81f};

    // The solver runs in metres; gameplay runs in pixels. Everything crossing the
    // boundary is scaled by this, so a 50-pixel box is a 1-metre box and Box2D stays
    // in the range it was tuned for.
    f32 pixelsPerMeter = 50.0f;

    // One simulation step. Keep it equal to AppConfig::fixedTimeStep — which is what
    // App does — or the world accumulates against the game loop and stutters.
    f32 fixedStep = 1.0f / 60.0f;
    i32 subSteps  = 4;
};

class PhysicsWorld {
public:
    // Fired during step(), once per pair, on the step the two started or stopped
    // touching. Sensors report through the same callbacks.
    using ContactCallback = std::function<void(ecs::Entity, ecs::Entity)>;

    explicit PhysicsWorld(PhysicsConfig config = {});
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&)            = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // Creates bodies for entities that have gained a rigid body and a collider,
    // destroys those whose entity died, simulates, and writes the result back onto
    // Transform2D. Feed it a fixed dt; App does.
    void step(ecs::Registry& registry, f32 dt);

    void setContactBegin(ContactCallback cb);
    void setContactEnd(ContactCallback cb);
    void setGravity(Vec2 gravity);
    [[nodiscard]] Vec2 gravity() const;

    // --- Per-body control. Every one is a no-op on an entity with no backing body,
    // which is the case until the first step() after its components were added. ---

    void applyForce(ecs::Entity, Vec2 force);
    void applyLinearImpulse(ecs::Entity, Vec2 impulse);
    void applyTorque(ecs::Entity, f32 torque);
    void applyAngularImpulse(ecs::Entity, f32 impulse);

    void setLinearVelocity(ecs::Entity, Vec2 velocity);
    void setAngularVelocity(ecs::Entity, f32 radiansPerSecond);
    void setTransform(ecs::Entity, Vec2 position, f32 rotation);
    void setAwake(ecs::Entity, bool awake);
    void setEnabled(ecs::Entity, bool enabled);

    [[nodiscard]] Vec2 linearVelocity(ecs::Entity) const;
    [[nodiscard]] f32  angularVelocity(ecs::Entity) const;
    [[nodiscard]] bool hasBody(ecs::Entity) const;

    // --- Queries. These read the broadphase and step nothing. ---

    // Nearest hit along the segment, or a RaycastHit whose hit() is false.
    [[nodiscard]] RaycastHit raycast(Vec2 from, Vec2 to) const;

    // Every body whose shape overlaps the world-space rect, sensors included.
    [[nodiscard]] std::vector<ecs::Entity> overlapRect(const Rect& area) const;

    // The first body containing the point — the "what did I just click on" query.
    [[nodiscard]] ecs::Entity pointQuery(Vec2 world) const;

    // --- Joints. Both bodies must already exist, so create a joint after the step()
    // that brought its entities in, not in the same breath as spawning them. ---

    // Holds two bodies a fixed distance apart. `length` is in pixels. Leave `hertz`
    // at 0 for a rigid rod; raise it to make the link springy.
    JointHandle createDistanceJoint(ecs::Entity a, ecs::Entity b, f32 length,
                                    f32 hertz = 0.0f, f32 damping = 0.0f);

    // Pins two bodies at one world point and lets them rotate about it.
    JointHandle createRevoluteJoint(ecs::Entity a, ecs::Entity b, Vec2 worldAnchor,
                                    bool enableLimit = false,
                                    f32 lowerAngle = 0.0f, f32 upperAngle = 0.0f);

    void destroyJoint(JointHandle);

    [[nodiscard]] usize bodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
