#include "vortex/physics/physics_world.hpp"

#include "vortex/core/log.hpp"
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

// An entity fits in the 64 bits Box2D hands back as user data, generation and all,
// so a body can name its entity without a side table.
u64 keyOf(ecs::Entity e) { return (static_cast<u64>(e.generation) << 32) | e.index; }

ecs::Entity entityFromBits(u64 bits) {
    return ecs::Entity{static_cast<u32>(bits & 0xFFFFFFFFu), static_cast<u32>(bits >> 32)};
}

// An end-touch event can name a shape that was destroyed during the same step, and
// reading the body off a dead shape is undefined. Checking first is not optional.
ecs::Entity entityOfShape(b2ShapeId shape) {
    if (!b2Shape_IsValid(shape)) return {};
    const b2BodyId body = b2Shape_GetBody(shape);
    return entityFromBits(
        static_cast<u64>(reinterpret_cast<uintptr_t>(b2Body_GetUserData(body))));
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
        bool     writeBack;   // static bodies never move, so never read them back
    };

    b2WorldId     world;
    PhysicsConfig config;
    f32           invPpm      = 0.02f;
    f32           accumulator = 0.0f;

    ContactCallback onBegin;
    ContactCallback onEnd;

    std::unordered_map<u64, Body>        bodies;
    std::vector<b2JointId>               joints;   // index = JointHandle::index
    std::vector<u32>                     jointFree;

    [[nodiscard]] Body* find(ecs::Entity e) {
        const auto it = bodies.find(keyOf(e));
        return it == bodies.end() ? nullptr : &it->second;
    }

    [[nodiscard]] const Body* find(ecs::Entity e) const {
        const auto it = bodies.find(keyOf(e));
        return it == bodies.end() ? nullptr : &it->second;
    }

    // Shared by both collider kinds.
    [[nodiscard]] b2ShapeDef shapeDefFor(const RigidBody2D& rb, bool isSensor) const {
        b2ShapeDef sd           = b2DefaultShapeDef();
        sd.density              = rb.density;
        sd.material.friction    = rb.friction;
        sd.material.restitution = rb.restitution;
        sd.isSensor             = isSensor;
        sd.enableContactEvents  = true;
        sd.enableSensorEvents   = true;
        return sd;
    }

    [[nodiscard]] b2BodyId createBody(ecs::Entity e, const RigidBody2D& rb,
                                      const ecs::Transform2D& tf) {
        b2BodyDef bd      = b2DefaultBodyDef();
        bd.type           = toB2(rb.type);
        bd.position       = {tf.position.x * invPpm, tf.position.y * invPpm};
        bd.rotation       = b2MakeRot(tf.rotation);
        bd.fixedRotation  = rb.fixedRotation;
        bd.gravityScale   = rb.gravityScale;
        bd.linearDamping  = rb.linearDamping;
        bd.isBullet       = rb.bullet;
        bd.userData       = reinterpret_cast<void*>(static_cast<uintptr_t>(keyOf(e)));
        return b2CreateBody(world, &bd);
    }

    void syncBodies(ecs::Registry& reg) {
        // Entities that gained a box collider since the last step.
        reg.view<RigidBody2D, ecs::Transform2D, BoxCollider2D>(
            [&](ecs::Entity e, RigidBody2D& rb, ecs::Transform2D& tf, BoxCollider2D& col) {
                const u64 key = keyOf(e);
                if (bodies.count(key) != 0) return;

                if (reg.has<CircleCollider2D>(e)) {
                    VORTEX_ERROR("Physics", "entity %u has both a box and a circle collider; "
                                            "using the box", e.index);
                }

                const b2BodyId body = createBody(e, rb, tf);
                const b2ShapeDef sd = shapeDefFor(rb, col.isSensor);
                const b2Polygon box = b2MakeOffsetBox(
                    col.halfExtents.x * invPpm, col.halfExtents.y * invPpm,
                    {col.offset.x * invPpm, col.offset.y * invPpm}, b2MakeRot(0.0f));
                b2CreatePolygonShape(body, &sd, &box);

                bodies.emplace(key, Body{body, rb.type != BodyType::Static});
            });

        // ...and those that gained a circle collider.
        reg.view<RigidBody2D, ecs::Transform2D, CircleCollider2D>(
            [&](ecs::Entity e, RigidBody2D& rb, ecs::Transform2D& tf, CircleCollider2D& col) {
                const u64 key = keyOf(e);
                if (bodies.count(key) != 0) return;   // the box pass already built it

                const b2BodyId body   = createBody(e, rb, tf);
                const b2ShapeDef sd   = shapeDefFor(rb, col.isSensor);
                const b2Circle circle = {{col.offset.x * invPpm, col.offset.y * invPpm},
                                         col.radius * invPpm};
                b2CreateCircleShape(body, &sd, &circle);

                bodies.emplace(key, Body{body, rb.type != BodyType::Static});
            });

        // Tear down bodies whose entity died or dropped its physics components.
        std::vector<u64> dead;
        for (const auto& [key, body] : bodies) {
            const ecs::Entity e = entityFromBits(key);
            const bool hasCollider = reg.has<BoxCollider2D>(e) || reg.has<CircleCollider2D>(e);
            if (!reg.alive(e) || !reg.has<RigidBody2D>(e) || !hasCollider) dead.push_back(key);
        }
        for (const u64 key : dead) {
            b2DestroyBody(bodies[key].id);
            bodies.erase(key);
        }
    }

    void fire(const ContactCallback& cb, ecs::Registry& reg, ecs::Entity a, ecs::Entity b) {
        // A shape can outlive its entity by an event: the body was destroyed during
        // the step that queued this. The alive check is load-bearing, not a formality.
        if (cb && reg.alive(a) && reg.alive(b)) cb(a, b);
    }

    void collectContacts(ecs::Registry& reg) {
        if (!onBegin && !onEnd) return;

        // Solid touches and sensor overlaps arrive on two different channels in
        // Box2D — a sensor never produces a contact event — so both are drained here
        // and reported through the one pair of callbacks gameplay actually wants.
        const b2ContactEvents contacts = b2World_GetContactEvents(world);
        for (int i = 0; i < contacts.beginCount; ++i)
            fire(onBegin, reg, entityOfShape(contacts.beginEvents[i].shapeIdA),
                 entityOfShape(contacts.beginEvents[i].shapeIdB));
        for (int i = 0; i < contacts.endCount; ++i)
            fire(onEnd, reg, entityOfShape(contacts.endEvents[i].shapeIdA),
                 entityOfShape(contacts.endEvents[i].shapeIdB));

        const b2SensorEvents sensors = b2World_GetSensorEvents(world);
        for (int i = 0; i < sensors.beginCount; ++i)
            fire(onBegin, reg, entityOfShape(sensors.beginEvents[i].sensorShapeId),
                 entityOfShape(sensors.beginEvents[i].visitorShapeId));
        for (int i = 0; i < sensors.endCount; ++i)
            fire(onEnd, reg, entityOfShape(sensors.endEvents[i].sensorShapeId),
                 entityOfShape(sensors.endEvents[i].visitorShapeId));
    }

    void writeBack(ecs::Registry& reg) {
        for (const auto& [key, body] : bodies) {
            if (!body.writeBack) continue;
            ecs::Transform2D* tf = reg.tryGet<ecs::Transform2D>(entityFromBits(key));
            if (tf == nullptr) continue;
            const b2Vec2 p = b2Body_GetPosition(body.id);
            tf->position = {p.x * config.pixelsPerMeter, p.y * config.pixelsPerMeter};
            tf->rotation = b2Rot_GetAngle(b2Body_GetRotation(body.id));
        }
    }
};

PhysicsWorld::PhysicsWorld(PhysicsConfig config) : m_impl(std::make_unique<Impl>()) {
    m_impl->config = config;
    m_impl->invPpm = 1.0f / config.pixelsPerMeter;

    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity    = {config.gravity.x, config.gravity.y};
    m_impl->world = b2CreateWorld(&wd);
}

PhysicsWorld::~PhysicsWorld() { b2DestroyWorld(m_impl->world); }

void PhysicsWorld::step(ecs::Registry& registry, f32 dt) {
    Impl& s = *m_impl;
    s.syncBodies(registry);

    // The accumulator makes the simulation independent of how the caller paces it: a
    // fixed dt equal to config.fixedStep runs exactly one step, and a variable dt
    // still advances the world by whole steps and carries the remainder.
    s.accumulator += dt;
    if (s.accumulator > 0.25f) s.accumulator = 0.25f;   // no spiral of death
    while (s.accumulator >= s.config.fixedStep) {
        b2World_Step(s.world, s.config.fixedStep, s.config.subSteps);
        s.accumulator -= s.config.fixedStep;
        s.collectContacts(registry);
    }

    s.writeBack(registry);
}

void PhysicsWorld::setContactBegin(ContactCallback cb) { m_impl->onBegin = std::move(cb); }
void PhysicsWorld::setContactEnd(ContactCallback cb)   { m_impl->onEnd   = std::move(cb); }

void PhysicsWorld::setGravity(Vec2 g) {
    m_impl->config.gravity = g;
    b2World_SetGravity(m_impl->world, {g.x, g.y});
}

Vec2 PhysicsWorld::gravity() const { return m_impl->config.gravity; }

usize PhysicsWorld::bodyCount() const { return m_impl->bodies.size(); }

bool PhysicsWorld::hasBody(ecs::Entity e) const { return m_impl->find(e) != nullptr; }

// --------------------------------------------------------------- body control

void PhysicsWorld::applyForce(ecs::Entity e, Vec2 force) {
    if (Impl::Body* b = m_impl->find(e))
        b2Body_ApplyForceToCenter(b->id, {force.x, force.y}, true);
}

void PhysicsWorld::applyLinearImpulse(ecs::Entity e, Vec2 impulse) {
    if (Impl::Body* b = m_impl->find(e))
        b2Body_ApplyLinearImpulseToCenter(b->id, {impulse.x, impulse.y}, true);
}

void PhysicsWorld::applyTorque(ecs::Entity e, f32 torque) {
    if (Impl::Body* b = m_impl->find(e)) b2Body_ApplyTorque(b->id, torque, true);
}

void PhysicsWorld::applyAngularImpulse(ecs::Entity e, f32 impulse) {
    if (Impl::Body* b = m_impl->find(e)) b2Body_ApplyAngularImpulse(b->id, impulse, true);
}

void PhysicsWorld::setLinearVelocity(ecs::Entity e, Vec2 v) {
    if (Impl::Body* b = m_impl->find(e)) b2Body_SetLinearVelocity(b->id, {v.x, v.y});
}

void PhysicsWorld::setAngularVelocity(ecs::Entity e, f32 w) {
    if (Impl::Body* b = m_impl->find(e)) b2Body_SetAngularVelocity(b->id, w);
}

void PhysicsWorld::setTransform(ecs::Entity e, Vec2 position, f32 rotation) {
    if (Impl::Body* b = m_impl->find(e))
        b2Body_SetTransform(b->id,
                            {position.x * m_impl->invPpm, position.y * m_impl->invPpm},
                            b2MakeRot(rotation));
}

void PhysicsWorld::setAwake(ecs::Entity e, bool awake) {
    if (Impl::Body* b = m_impl->find(e)) b2Body_SetAwake(b->id, awake);
}

void PhysicsWorld::setEnabled(ecs::Entity e, bool enabled) {
    Impl::Body* b = m_impl->find(e);
    if (b == nullptr) return;
    if (enabled) b2Body_Enable(b->id);
    else         b2Body_Disable(b->id);
}

Vec2 PhysicsWorld::linearVelocity(ecs::Entity e) const {
    const Impl::Body* b = m_impl->find(e);
    if (b == nullptr) return {};
    const b2Vec2 v = b2Body_GetLinearVelocity(b->id);
    return {v.x * m_impl->config.pixelsPerMeter, v.y * m_impl->config.pixelsPerMeter};
}

f32 PhysicsWorld::angularVelocity(ecs::Entity e) const {
    const Impl::Body* b = m_impl->find(e);
    return b != nullptr ? b2Body_GetAngularVelocity(b->id) : 0.0f;
}

// -------------------------------------------------------------------- queries

RaycastHit PhysicsWorld::raycast(Vec2 from, Vec2 to) const {
    const Impl& s = *m_impl;

    const b2Vec2 origin{from.x * s.invPpm, from.y * s.invPpm};
    const b2Vec2 translation{(to.x - from.x) * s.invPpm, (to.y - from.y) * s.invPpm};

    const b2RayResult result =
        b2World_CastRayClosest(s.world, origin, translation, b2DefaultQueryFilter());
    if (!result.hit) return {};

    return RaycastHit{
        .entity   = entityOfShape(result.shapeId),
        .point    = {result.point.x * s.config.pixelsPerMeter,
                     result.point.y * s.config.pixelsPerMeter},
        .normal   = {result.normal.x, result.normal.y},   // already unit-length
        .fraction = result.fraction,
    };
}

std::vector<ecs::Entity> PhysicsWorld::overlapRect(const Rect& area) const {
    const Impl& s = *m_impl;

    std::vector<ecs::Entity> out;
    const b2AABB aabb{{area.left() * s.invPpm,  area.top() * s.invPpm},
                      {area.right() * s.invPpm, area.bottom() * s.invPpm}};

    // Box2D calls back per overlapping shape; one body can own several, so the same
    // entity could arrive twice. Bodies here carry one shape each, so it cannot —
    // but if that ever changes, this is where the de-duplication belongs.
    b2World_OverlapAABB(
        s.world, aabb, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* context) -> bool {
            static_cast<std::vector<ecs::Entity>*>(context)->push_back(entityOfShape(shape));
            return true;   // keep going
        },
        &out);

    return out;
}

ecs::Entity PhysicsWorld::pointQuery(Vec2 world) const {
    const Impl& s = *m_impl;

    // The broadphase only answers in boxes, so probe a degenerate one and then ask
    // each candidate shape whether it really contains the point.
    struct Query {
        b2Vec2      point;
        ecs::Entity found;
    } query{{world.x * s.invPpm, world.y * s.invPpm}, {}};

    const b2AABB aabb{{query.point.x - 0.001f, query.point.y - 0.001f},
                      {query.point.x + 0.001f, query.point.y + 0.001f}};

    b2World_OverlapAABB(
        s.world, aabb, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* context) -> bool {
            auto* q = static_cast<Query*>(context);
            if (!b2Shape_TestPoint(shape, q->point)) return true;
            q->found = entityOfShape(shape);
            return false;   // stop at the first real hit
        },
        &query);

    return query.found;
}

// --------------------------------------------------------------------- joints

namespace {

JointHandle storeJoint(std::vector<b2JointId>& joints, std::vector<u32>& free, b2JointId id) {
    u32 index;
    if (!free.empty()) {
        index = free.back();
        free.pop_back();
        joints[index] = id;
    } else {
        index = static_cast<u32>(joints.size());
        joints.push_back(id);
    }
    return JointHandle{index, 0u};
}

}

JointHandle PhysicsWorld::createDistanceJoint(ecs::Entity a, ecs::Entity b, f32 length,
                                              f32 hertz, f32 damping) {
    Impl::Body* ba = m_impl->find(a);
    Impl::Body* bb = m_impl->find(b);
    if (ba == nullptr || bb == nullptr) {
        VORTEX_ERROR("Physics", "distance joint needs two bodies; step() once after "
                                "spawning the entities before joining them");
        return {};
    }

    b2DistanceJointDef def = b2DefaultDistanceJointDef();
    def.bodyIdA      = ba->id;
    def.bodyIdB      = bb->id;
    def.localAnchorA = b2Vec2_zero;
    def.localAnchorB = b2Vec2_zero;
    def.length       = length * m_impl->invPpm;
    def.enableSpring = hertz > 0.0f;
    def.hertz        = hertz;
    def.dampingRatio = damping;

    return storeJoint(m_impl->joints, m_impl->jointFree,
                      b2CreateDistanceJoint(m_impl->world, &def));
}

JointHandle PhysicsWorld::createRevoluteJoint(ecs::Entity a, ecs::Entity b, Vec2 worldAnchor,
                                              bool enableLimit, f32 lowerAngle, f32 upperAngle) {
    Impl::Body* ba = m_impl->find(a);
    Impl::Body* bb = m_impl->find(b);
    if (ba == nullptr || bb == nullptr) {
        VORTEX_ERROR("Physics", "revolute joint needs two bodies; step() once after "
                                "spawning the entities before joining them");
        return {};
    }

    const b2Vec2 anchor{worldAnchor.x * m_impl->invPpm, worldAnchor.y * m_impl->invPpm};

    b2RevoluteJointDef def = b2DefaultRevoluteJointDef();
    def.bodyIdA      = ba->id;
    def.bodyIdB      = bb->id;
    def.localAnchorA = b2Body_GetLocalPoint(ba->id, anchor);
    def.localAnchorB = b2Body_GetLocalPoint(bb->id, anchor);
    def.enableLimit  = enableLimit;
    def.lowerAngle   = lowerAngle;
    def.upperAngle   = upperAngle;

    return storeJoint(m_impl->joints, m_impl->jointFree,
                      b2CreateRevoluteJoint(m_impl->world, &def));
}

void PhysicsWorld::destroyJoint(JointHandle handle) {
    if (!handle.valid() || handle.index >= m_impl->joints.size()) return;

    b2JointId& id = m_impl->joints[handle.index];
    if (!b2Joint_IsValid(id)) return;   // already gone, or its bodies were destroyed

    b2DestroyJoint(id);
    id = b2_nullJointId;
    m_impl->jointFree.push_back(handle.index);
}

}
