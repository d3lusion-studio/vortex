#include "vortex/ecs/serialize.hpp"

#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/scene.hpp"

#include <unordered_map>

namespace vortex::ecs {

namespace {

constexpr i32 kSceneVersion = 1;
constexpr i32 kNoParent     = -1;

// Samplers are written by name. A scene outlives the enum's ordering, and a file
// that says "nearest_clamp" survives someone adding a sampler in the middle of the
// list in a way that a bare index would not.
constexpr const char* kSamplerNames[renderer::kSpriteSamplerCount] = {
    "linear_clamp", "nearest_clamp", "linear_repeat", "nearest_repeat",
};

const char* samplerName(renderer::SpriteSampler s) {
    return kSamplerNames[static_cast<usize>(s)];
}

renderer::SpriteSampler samplerFrom(const json::Value& v, renderer::SpriteSampler fallback) {
    if (!v.isString()) return fallback;
    const std::string name = v.asString();
    for (u32 i = 0; i < renderer::kSpriteSamplerCount; ++i)
        if (name == kSamplerNames[i]) return static_cast<renderer::SpriteSampler>(i);
    return fallback;
}

}

const ComponentRegistry::Entry* ComponentRegistry::find(std::string_view name) const {
    for (const Entry& entry : m_entries)
        if (entry.name == name) return &entry;
    return nullptr;
}

ComponentRegistry& defaultComponents() {
    static ComponentRegistry registry = [] {
        ComponentRegistry r;

        r.add<Transform2D>(
            "Transform2D",
            [](const Transform2D& t, const SerializeContext&) {
                json::Value v = json::Value::object();
                v.set("position", toJson(t.position));
                v.set("rotation", t.rotation);
                v.set("scale", toJson(t.scale));
                return v;
            },
            [](Transform2D& t, const json::Value& v, const SerializeContext&) {
                t.position = vec2From(v["position"], t.position);
                t.rotation = v["rotation"].asF32(t.rotation);
                t.scale    = vec2From(v["scale"], t.scale);
            });

        r.add<SpriteComp>(
            "SpriteComp",
            [](const SpriteComp& s, const SerializeContext& ctx) {
                json::Value v = json::Value::object();
                v.set("texture", ctx.nameOf(s.texture));
                v.set("color", toJson(s.color));
                v.set("uv", toJson(s.uv));
                v.set("size", toJson(s.size));
                v.set("layer", s.layer);
                v.set("anchor", toJson(s.anchor));
                v.set("flipX", s.flipX);
                v.set("flipY", s.flipY);
                v.set("sampler", samplerName(s.sampler));
                return v;
            },
            [](SpriteComp& s, const json::Value& v, const SerializeContext& ctx) {
                s.texture = ctx.lookup(v["texture"].asString());
                s.color   = vec4From(v["color"], s.color);
                s.uv      = rectFrom(v["uv"], s.uv);
                s.size    = vec2From(v["size"], s.size);
                s.layer   = v["layer"].asI32(s.layer);
                s.anchor  = vec2From(v["anchor"], s.anchor);
                s.flipX   = v["flipX"].asBool(s.flipX);
                s.flipY   = v["flipY"].asBool(s.flipY);
                s.sampler = samplerFrom(v["sampler"], s.sampler);
            });

        // `frame` and `finished` are outputs of the animation system, so they are
        // left out: the first update after a load recomputes both from `time`.
        r.add<SpriteAnimator>(
            "SpriteAnimator",
            [](const SpriteAnimator& a, const SerializeContext&) {
                json::Value v = json::Value::object();
                v.set("clip", a.clip.valid() ? json::Value(a.clip.index) : json::Value());
                v.set("time", a.time);
                v.set("speed", a.speed);
                v.set("playing", a.playing);
                return v;
            },
            [](SpriteAnimator& a, const json::Value& v, const SerializeContext&) {
                const json::Value& clip = v["clip"];
                a.clip    = clip.isNumber() ? renderer::AnimationHandle{clip.asU32(), 0u}
                                            : renderer::AnimationHandle{};
                a.time    = v["time"].asF32(a.time);
                a.speed   = v["speed"].asF32(a.speed);
                a.playing = v["playing"].asBool(a.playing);
            });

        r.add<Velocity>(
            "Velocity",
            [](const Velocity& vel, const SerializeContext&) {
                json::Value v = json::Value::object();
                v.set("value", toJson(vel.value));
                return v;
            },
            [](Velocity& vel, const json::Value& v, const SerializeContext&) {
                vel.value = vec2From(v["value"], vel.value);
            });

        return r;
    }();
    return registry;
}

// -------------------------------------------------------------------- entity I/O

namespace {

// Components, plus the parent link written as an index into the entity list rather
// than as a raw Entity — ids are handed out afresh on every run.
json::Value saveEntity(Registry& reg, Entity e, const ComponentRegistry& types,
                       const SerializeContext& ctx,
                       const std::unordered_map<u32, i32>& serialOf) {
    json::Value out = json::Value::object();

    i32 parent = kNoParent;
    if (const Parent* p = reg.tryGet<Parent>(e); p != nullptr && reg.alive(p->value)) {
        if (const auto it = serialOf.find(p->value.index); it != serialOf.end())
            parent = it->second;
    }
    out.set("parent", parent);

    json::Value components = json::Value::object();
    for (const ComponentRegistry::Entry& type : types.entries())
        if (type.has(reg, e)) components.set(type.name, type.save(reg, e, ctx));
    out.set("components", std::move(components));

    return out;
}

void loadEntityComponents(Registry& reg, Entity e, const json::Value& node,
                          const ComponentRegistry& types, const SerializeContext& ctx) {
    const json::Value& components = node["components"];
    for (const auto& [name, value] : components.fields()) {
        const ComponentRegistry::Entry* type = types.find(name);
        if (type == nullptr) {
            VORTEX_WARN("Scene", "unknown component '%s', skipped", name.c_str());
            continue;
        }
        type->load(reg, e, value, ctx);
    }
}

// ------------------------------------------------------------------- animations

json::Value saveAnimations(const renderer::AnimationLibrary& library) {
    json::Value out = json::Value::array();
    for (const renderer::AnimationClip& clip : library.clips()) {
        json::Value c = json::Value::object();
        c.set("fps", clip.fps);
        c.set("loop", clip.loop);

        json::Value frames = json::Value::array();
        for (const Rect& uv : clip.frames) frames.push(toJson(uv));
        c.set("frames", std::move(frames));
        out.push(std::move(c));
    }
    return out;
}

// The texture a clip plays from is not written: it is whatever the sprite already
// points at, and the animation system overwrites SpriteComp::texture only when the
// clip carries one. Restoring clips in file order rebuilds every handle by index.
void loadAnimations(renderer::AnimationLibrary& library, const json::Value& list) {
    for (const json::Value& c : list.items()) {
        renderer::AnimationClip clip;
        clip.fps  = c["fps"].asF32(12.0f);
        clip.loop = c["loop"].asBool(true);
        for (const json::Value& uv : c["frames"].items())
            clip.frames.push_back(rectFrom(uv));
        library.add(std::move(clip));
    }
}

// ---------------------------------------------------------------------- tilemap

json::Value saveSheet(const renderer::SpriteSheet& sheet, const SerializeContext& ctx) {
    json::Value v = json::Value::object();
    v.set("texture", ctx.nameOf(sheet.texture));
    v.set("textureWidth", sheet.textureWidth);
    v.set("textureHeight", sheet.textureHeight);
    v.set("columns", sheet.columns);
    v.set("rows", sheet.rows);
    v.set("margin", sheet.margin);
    v.set("spacing", sheet.spacing);
    return v;
}

renderer::SpriteSheet loadSheet(const json::Value& v, const SerializeContext& ctx) {
    renderer::SpriteSheet sheet;
    sheet.texture       = ctx.lookup(v["texture"].asString());
    sheet.textureWidth  = v["textureWidth"].asU32();
    sheet.textureHeight = v["textureHeight"].asU32();
    sheet.columns       = v["columns"].asU32(1);
    sheet.rows          = v["rows"].asU32(1);
    sheet.margin        = v["margin"].asU32();
    sheet.spacing       = v["spacing"].asU32();
    return sheet;
}

json::Value saveTilemap(const renderer::Tilemap& map, const SerializeContext& ctx) {
    json::Value out = json::Value::object();

    json::Value layers = json::Value::array();
    for (const renderer::TileLayer& l : map.layers()) {
        json::Value v = json::Value::object();
        v.set("name", l.name);
        v.set("tileset", saveSheet(l.tileset, ctx));
        v.set("firstTileId", l.firstTileId);
        v.set("tileSize", toJson(l.tileSize));
        v.set("origin", toJson(l.origin));
        v.set("parallax", toJson(l.parallax));
        v.set("sampler", samplerName(l.sampler));
        v.set("tint", toJson(static_cast<Vec4>(l.tint)));
        v.set("layer", l.layer);
        v.set("visible", l.visible);
        v.set("width", l.width());
        v.set("height", l.height());

        json::Value tiles = json::Value::array();
        for (const renderer::TileId id : l.tiles()) tiles.push(id);
        v.set("tiles", std::move(tiles));

        layers.push(std::move(v));
    }
    out.set("layers", std::move(layers));

    // Flags are per tile id. Written as [id, flags] pairs, so a tileset that flags
    // only a couple of its ids does not pad the file out to the highest id in use.
    json::Value flags = json::Value::array();
    const std::vector<u8>& table = map.flags();
    for (usize id = 0; id < table.size(); ++id) {
        if (table[id] == renderer::TileNone) continue;
        json::Value pair = json::Value::array();
        pair.push(static_cast<u32>(id));
        pair.push(static_cast<u32>(table[id]));
        flags.push(std::move(pair));
    }
    out.set("flags", std::move(flags));

    return out;
}

void loadTilemap(renderer::Tilemap& map, const json::Value& v, const SerializeContext& ctx) {
    for (const json::Value& pair : v["flags"].items()) {
        if (pair.size() < 2) continue;
        map.setTileFlags(pair[usize{0}].asU32(), static_cast<u8>(pair[usize{1}].asU32()));
    }

    for (const json::Value& lv : v["layers"].items()) {
        renderer::TileLayer l(lv["width"].asU32(), lv["height"].asU32(),
                              vec2From(lv["tileSize"], {32.0f, 32.0f}));
        l.name        = lv["name"].asString();
        l.tileset     = loadSheet(lv["tileset"], ctx);
        l.firstTileId = lv["firstTileId"].asU32(1);
        l.origin      = vec2From(lv["origin"]);
        l.sampler  = samplerFrom(lv["sampler"], l.sampler);
        l.parallax = vec2From(lv["parallax"], {1.0f, 1.0f});
        l.layer    = lv["layer"].asI32();
        l.visible  = lv["visible"].asBool(true);

        const Vec4 tint = vec4From(lv["tint"], Vec4{1.0f, 1.0f, 1.0f, 1.0f});
        l.tint = Color{tint.x, tint.y, tint.z, tint.w};

        const json::Value& tiles = lv["tiles"];
        const usize w = l.width();
        if (w > 0) {
            for (usize i = 0; i < tiles.size() && i < l.tileCount(); ++i) {
                const renderer::TileId id = tiles[i].asU32();
                if (id == renderer::kEmptyTile) continue;
                l.setTile(static_cast<i32>(i % w), static_cast<i32>(i / w), id);
            }
        }

        map.addLayer(std::move(l));
    }
}

}

// ------------------------------------------------------------------------ scene

json::Value saveScene(Scene& scene, const SerializeContext& ctx, const ComponentRegistry& types) {
    Registry& reg = scene.registry();

    // Two passes: the first fixes each entity's index in the file, so the second can
    // write a parent link that points at a slot the loader will already have created.
    std::vector<Entity>          order;
    std::unordered_map<u32, i32> serialOf;
    reg.each([&](Entity e) {
        if (!reg.alive(e)) return;
        serialOf.emplace(e.index, static_cast<i32>(order.size()));
        order.push_back(e);
    });

    json::Value doc = json::Value::object();
    doc.set("version", kSceneVersion);

    json::Value camera = json::Value::object();
    camera.set("position", toJson(scene.camera.position));
    camera.set("zoom", scene.camera.zoom);
    doc.set("camera", std::move(camera));

    doc.set("animations", saveAnimations(scene.animations));
    doc.set("tilemap", saveTilemap(scene.tilemap, ctx));

    json::Value entities = json::Value::array();
    for (const Entity e : order) entities.push(saveEntity(reg, e, types, ctx, serialOf));
    doc.set("entities", std::move(entities));

    return doc;
}

bool loadScene(Scene& scene, const json::Value& doc, const SerializeContext& ctx,
               const ComponentRegistry& types) {
    scene.clear();

    if (!doc.isObject() || !doc.contains("entities")) {
        VORTEX_ERROR("Scene", "not a scene document");
        return false;
    }
    if (const i32 version = doc["version"].asI32(); version != kSceneVersion) {
        VORTEX_ERROR("Scene", "unsupported scene version %d (expected %d)",
                     version, kSceneVersion);
        return false;
    }

    scene.camera.position = vec2From(doc["camera"]["position"]);
    scene.camera.zoom     = doc["camera"]["zoom"].asF32(1.0f);

    loadAnimations(scene.animations, doc["animations"]);
    loadTilemap(scene.tilemap, doc["tilemap"], ctx);

    Registry&           reg      = scene.registry();
    const json::Value&  entities = doc["entities"];

    // Create every entity first, so a parent link can be resolved no matter which
    // order the file lists them in.
    std::vector<Entity> created;
    created.reserve(entities.size());
    for (usize i = 0; i < entities.size(); ++i) created.push_back(scene.spawn());

    for (usize i = 0; i < entities.size(); ++i) {
        const json::Value& node = entities[i];
        loadEntityComponents(reg, created[i], node, types, ctx);

        const i32 parent = node["parent"].asI32(kNoParent);
        if (parent >= 0 && static_cast<usize>(parent) < created.size())
            reg.emplace<Parent>(created[i], Parent{created[static_cast<usize>(parent)]});
    }

    updateTransforms(reg);   // world matrices are derived, never stored
    return true;
}

// ----------------------------------------------------------------------- prefab

namespace {

void collectDescendants(Registry& reg, Entity root, std::vector<Entity>& out) {
    out.push_back(root);
    // Children are not indexed, so this walks the whole registry per level. A prefab
    // is authored once and stamped many times, so the cost lands in the wrong place
    // to matter; instantiate() does no such walk.
    for (usize i = 0; i < out.size(); ++i) {
        const Entity parent = out[i];
        reg.view<Parent>([&](Entity child, Parent& link) {
            if (link.value == parent) out.push_back(child);
        });
    }
}

}

json::Value savePrefab(Scene& scene, Entity root, const SerializeContext& ctx,
                       const ComponentRegistry& types) {
    Registry& reg = scene.registry();

    json::Value doc = json::Value::object();
    doc.set("version", kSceneVersion);

    if (!reg.alive(root)) {
        doc.set("entities", json::Value::array());
        return doc;
    }

    std::vector<Entity> members;
    collectDescendants(reg, root, members);

    std::unordered_map<u32, i32> serialOf;
    for (usize i = 0; i < members.size(); ++i)
        serialOf.emplace(members[i].index, static_cast<i32>(i));

    json::Value entities = json::Value::array();
    for (const Entity e : members) entities.push(saveEntity(reg, e, types, ctx, serialOf));
    doc.set("entities", std::move(entities));

    return doc;
}

Entity instantiate(Scene& scene, const json::Value& prefab, Vec2 at, const SerializeContext& ctx,
                   const ComponentRegistry& types) {
    const json::Value& entities = prefab["entities"];
    if (!entities.isArray() || entities.size() == 0) return {};

    Registry& reg = scene.registry();

    std::vector<Entity> created;
    created.reserve(entities.size());
    for (usize i = 0; i < entities.size(); ++i) created.push_back(scene.spawn());

    for (usize i = 0; i < entities.size(); ++i) {
        const json::Value& node = entities[i];
        loadEntityComponents(reg, created[i], node, types, ctx);

        const i32 parent = node["parent"].asI32(kNoParent);
        if (parent >= 0 && static_cast<usize>(parent) < created.size())
            reg.emplace<Parent>(created[i], Parent{created[static_cast<usize>(parent)]});
    }

    // Index 0 is the root by construction (collectDescendants pushes it first). Its
    // saved position is an offset, so a prefab authored around the origin lands
    // exactly on `at`, and one authored off-origin keeps its own framing.
    const Entity root = created[0];
    reg.get<Transform2D>(root).position += at;

    return root;
}

}
