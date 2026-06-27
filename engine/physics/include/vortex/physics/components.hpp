#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

namespace vortex::physics {

enum class BodyType { Static, Kinematic, Dynamic };

struct RigidBody2D {
    BodyType type         = BodyType::Dynamic;
    f32      density      = 1.0f;
    f32      friction     = 0.3f;
    f32      restitution  = 0.0f;   // bounciness, [0,1]
    f32      gravityScale = 1.0f;
    bool     fixedRotation = false;
};

struct BoxCollider2D {
    Vec2 halfExtents{0.5f, 0.5f};
    bool isSensor = false;
};

}
