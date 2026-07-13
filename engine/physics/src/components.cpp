#include "vortex/physics/components.hpp"

#include "vortex/ecs/serialize.hpp"

namespace vortex::physics {

namespace {

// BodyType is written as a name rather than as its integer value, so inserting a
// new kind into the enum cannot silently reinterpret every saved body.
const char* bodyTypeName(BodyType t) {
    switch (t) {
        case BodyType::Static:    return "static";
        case BodyType::Kinematic: return "kinematic";
        case BodyType::Dynamic:   return "dynamic";
    }
    return "dynamic";
}

BodyType bodyTypeFrom(const std::string& name, BodyType fallback) {
    if (name == "static")    return BodyType::Static;
    if (name == "kinematic") return BodyType::Kinematic;
    if (name == "dynamic")   return BodyType::Dynamic;
    return fallback;
}

}

void registerComponents(ecs::ComponentRegistry& types) {
    types.add<RigidBody2D>(
        "RigidBody2D",
        [](const RigidBody2D& rb, const ecs::SerializeContext&) {
            json::Value v = json::Value::object();
            v.set("type", bodyTypeName(rb.type));
            v.set("density", rb.density);
            v.set("friction", rb.friction);
            v.set("restitution", rb.restitution);
            v.set("gravityScale", rb.gravityScale);
            v.set("linearDamping", rb.linearDamping);
            v.set("fixedRotation", rb.fixedRotation);
            v.set("bullet", rb.bullet);
            return v;
        },
        [](RigidBody2D& rb, const json::Value& v, const ecs::SerializeContext&) {
            rb.type          = bodyTypeFrom(v["type"].asString(), rb.type);
            rb.density       = v["density"].asF32(rb.density);
            rb.friction      = v["friction"].asF32(rb.friction);
            rb.restitution   = v["restitution"].asF32(rb.restitution);
            rb.gravityScale  = v["gravityScale"].asF32(rb.gravityScale);
            rb.linearDamping = v["linearDamping"].asF32(rb.linearDamping);
            rb.fixedRotation = v["fixedRotation"].asBool(rb.fixedRotation);
            rb.bullet        = v["bullet"].asBool(rb.bullet);
        });

    types.add<BoxCollider2D>(
        "BoxCollider2D",
        [](const BoxCollider2D& c, const ecs::SerializeContext&) {
            json::Value v = json::Value::object();
            v.set("halfExtents", ecs::toJson(c.halfExtents));
            v.set("offset", ecs::toJson(c.offset));
            v.set("isSensor", c.isSensor);
            return v;
        },
        [](BoxCollider2D& c, const json::Value& v, const ecs::SerializeContext&) {
            c.halfExtents = ecs::vec2From(v["halfExtents"], c.halfExtents);
            c.offset      = ecs::vec2From(v["offset"], c.offset);
            c.isSensor    = v["isSensor"].asBool(c.isSensor);
        });

    types.add<CircleCollider2D>(
        "CircleCollider2D",
        [](const CircleCollider2D& c, const ecs::SerializeContext&) {
            json::Value v = json::Value::object();
            v.set("radius", c.radius);
            v.set("offset", ecs::toJson(c.offset));
            v.set("isSensor", c.isSensor);
            return v;
        },
        [](CircleCollider2D& c, const json::Value& v, const ecs::SerializeContext&) {
            c.radius   = v["radius"].asF32(c.radius);
            c.offset   = ecs::vec2From(v["offset"], c.offset);
            c.isSensor = v["isSensor"].asBool(c.isSensor);
        });
}

}
