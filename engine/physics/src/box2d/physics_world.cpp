#include "vortex/physics/physics_world.hpp"

#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/physics/components.hpp"

#include <box2d/box2d.h>
#include <box2d/math_functions.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace vortex::physics {

namespace {

constexpr f32 kFixedStep = 1.0f / 60.0f;
constexpr i32 kSubSteps  = 4;

u64 keyOf(ecs::Entity e) { return (static_cast<u64>(e.generation) << 32) | e.index; }
ecs::Entity entityFromBits(u64 bits) {
    return ecs::Entity{static_cast<u32>(bits & 0xFFFFFFFFu), static_cast<u32>(bits >> 32)};
}

b2BodyType toB2(BodyType t) {
    switch (t) {
        case BodyType::Static:    return b2_staticBody;
        case BodyType::Kinematic: return b2_kinematicBody;
        case BodyType::Dynamic:   return b2_dynamicBody;
    }
    return b2_dynamicBody;
}

} // namespace

struct PhysicsWorld::Impl {
    struct Body {
        b2BodyId id;
        bool     writeBack;   // false for static bodies
    };

    b2WorldId                       world;
    f32                             ppm;
    f32                             invPpm;
    f32                             accumulator = 0.0f;
    ContactCallback                 onContact;
    std::unordered_map<u64, Body>   bodies;

    Body* find(ecs::Entity e) {
        auto it = bodies.find(keyOf(e));
        return it == bodies.end() ? nullptr : &it->second;
    }

    void syncBodies(ecs::Registry& reg) {
        // Create a body for any entity that has the full physics component set but
        // no backing body yet.
        reg.view<RigidBody2D, ecs::Transform2D, BoxCollider2D>(
            [&](ecs::Entity e, RigidBody2D& rb, ecs::Transform2D& tf, BoxCollider2D& col) {
                const u64 k = keyOf(e);
                if (bodies.count(k)) return;

                b2BodyDef bd     = b2DefaultBodyDef();
                bd.type          = toB2(rb.type);
                bd.position      = {tf.position.x * invPpm, tf.position.y * invPpm};
                bd.rotation      = b2MakeRot(tf.rotation);
                bd.fixedRotation = rb.fixedRotation;
                bd.gravityScale  = rb.gravityScale;
                bd.userData      = reinterpret_cast<void*>(static_cast<uintptr_t>(k));
                const b2BodyId body = b2CreateBody(world, &bd);

                b2ShapeDef sd          = b2DefaultShapeDef();
                sd.density             = rb.density;
                sd.material.friction   = rb.friction;
                sd.material.restitution = rb.restitution;
                sd.isSensor            = col.isSensor;
                sd.enableContactEvents = true;
                const b2Polygon box = b2MakeBox(col.halfExtents.x * invPpm,
                                                col.halfExtents.y * invPpm);
                b2CreatePolygonShape(body, &sd, &box);

                bodies.emplace(k, Body{body, rb.type != BodyType::Static});
            });

        // Tear down bodies whose entity died or lost its physics components.
        std::vector<u64> dead;
        for (auto& [k, body] : bodies) {
            const ecs::Entity e = entityFromBits(k);
            if (!reg.alive(e) || !reg.has<RigidBody2D>(e) || !reg.has<BoxCollider2D>(e))
                dead.push_back(k);
        }
        for (u64 k : dead) {
            b2DestroyBody(bodies[k].id);
            bodies.erase(k);
        }
    }

    void collectContacts(ecs::Registry& reg) {
        if (!onContact) return;
        const b2ContactEvents ev = b2World_GetContactEvents(world);
        for (int i = 0; i < ev.beginCount; ++i) {
            const b2BodyId ba = b2Shape_GetBody(ev.beginEvents[i].shapeIdA);
            const b2BodyId bb = b2Shape_GetBody(ev.beginEvents[i].shapeIdB);
            const auto ka = static_cast<u64>(reinterpret_cast<uintptr_t>(b2Body_GetUserData(ba)));
            const auto kb = static_cast<u64>(reinterpret_cast<uintptr_t>(b2Body_GetUserData(bb)));
            const ecs::Entity ea = entityFromBits(ka);
            const ecs::Entity eb = entityFromBits(kb);
            if (reg.alive(ea) && reg.alive(eb)) onContact(ea, eb);
        }
    }

    void writeBack(ecs::Registry& reg) {
        for (auto& [k, body] : bodies) {
            if (!body.writeBack) continue;
            const ecs::Entity e = entityFromBits(k);
            ecs::Transform2D* tf = reg.tryGet<ecs::Transform2D>(e);
            if (!tf) continue;
            const b2Vec2 p = b2Body_GetPosition(body.id);
            const b2Rot  r = b2Body_GetRotation(body.id);
            tf->position = {p.x * ppm, p.y * ppm};
            tf->rotation = b2Rot_GetAngle(r);
        }
    }
};

PhysicsWorld::PhysicsWorld(Vec2 gravity, f32 pixelsPerMeter)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->ppm    = pixelsPerMeter;
    m_impl->invPpm = 1.0f / pixelsPerMeter;

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity    = {gravity.x, gravity.y};
    m_impl->world = b2CreateWorld(&wd);
}

PhysicsWorld::~PhysicsWorld() { b2DestroyWorld(m_impl->world); }

void PhysicsWorld::step(ecs::Registry& registry, f32 dt) {
    m_impl->syncBodies(registry);

    m_impl->accumulator += dt;
    if (m_impl->accumulator > 0.25f) m_impl->accumulator = 0.25f;   // avoid spiral of death
    while (m_impl->accumulator >= kFixedStep) {
        b2World_Step(m_impl->world, kFixedStep, kSubSteps);
        m_impl->accumulator -= kFixedStep;
        m_impl->collectContacts(registry);
    }

    m_impl->writeBack(registry);
}

void PhysicsWorld::setContactCallback(ContactCallback cb) { m_impl->onContact = std::move(cb); }

void PhysicsWorld::setGravity(Vec2 g) { b2World_SetGravity(m_impl->world, {g.x, g.y}); }

void PhysicsWorld::applyLinearImpulse(ecs::Entity e, Vec2 impulse) {
    if (Impl::Body* b = m_impl->find(e))
        b2Body_ApplyLinearImpulseToCenter(b->id, {impulse.x, impulse.y}, true);
}

void PhysicsWorld::setLinearVelocity(ecs::Entity e, Vec2 v) {
    if (Impl::Body* b = m_impl->find(e)) b2Body_SetLinearVelocity(b->id, {v.x, v.y});
}

}
