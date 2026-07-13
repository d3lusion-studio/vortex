#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

namespace vortex::ecs { class ComponentRegistry; }

namespace vortex::physics {

enum class BodyType { Static, Kinematic, Dynamic };

// These are read once, when PhysicsWorld first sees the entity, and editing them
// afterwards does not retune the body. Use the PhysicsWorld setters for anything
// that has to change at runtime.
struct RigidBody2D {
    BodyType type          = BodyType::Dynamic;
    f32      density       = 1.0f;
    f32      friction      = 0.3f;
    f32      restitution   = 0.0f;   // bounciness, [0,1]
    f32      gravityScale  = 1.0f;
    f32      linearDamping = 0.0f;
    bool     fixedRotation = false;
    bool     bullet        = false;  // continuous collision, for fast thin things
};

// An entity takes one collider. Both on the same entity is a mistake, and the world
// says so rather than silently picking one.
struct BoxCollider2D {
    Vec2 halfExtents{0.5f, 0.5f};
    Vec2 offset{0.0f, 0.0f};   // local, from the entity's origin
    bool isSensor = false;
};

struct CircleCollider2D {
    f32  radius = 0.5f;
    Vec2 offset{0.0f, 0.0f};
    bool isSensor = false;
};

// Teach the scene serializer about the components above. Call once, before saving
// or loading a scene that contains any of them.
void registerComponents(ecs::ComponentRegistry& types);

}
