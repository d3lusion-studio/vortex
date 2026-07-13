#pragma once
#include "vortex/core/json.hpp"
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace vortex::ecs {

class Scene;

// GPU handles mean nothing across a save. Everything that points at a resource is
// written as a name instead, and this is what turns one into the other. App wires
// it to the AssetManager; a test can wire it to a stub.
struct SerializeContext {
    std::function<std::string(rhi::TextureHandle)>        textureName;
    std::function<rhi::TextureHandle(const std::string&)> textureByName;

    [[nodiscard]] std::string nameOf(rhi::TextureHandle handle) const {
        return textureName ? textureName(handle) : std::string{};
    }
    [[nodiscard]] rhi::TextureHandle lookup(const std::string& name) const {
        return textureByName && !name.empty() ? textureByName(name) : rhi::TextureHandle{};
    }
};

// ------------------------------------------------------------- math conversions

[[nodiscard]] inline json::Value toJson(Vec2 v) {
    json::Value a = json::Value::array();
    a.push(v.x);
    a.push(v.y);
    return a;
}

[[nodiscard]] inline json::Value toJson(Vec4 v) {
    json::Value a = json::Value::array();
    a.push(v.x);
    a.push(v.y);
    a.push(v.z);
    a.push(v.w);
    return a;
}

[[nodiscard]] inline json::Value toJson(Rect r) {
    json::Value a = json::Value::array();
    a.push(r.x);
    a.push(r.y);
    a.push(r.width);
    a.push(r.height);
    return a;
}

[[nodiscard]] inline Vec2 vec2From(const json::Value& v, Vec2 fallback = {}) {
    if (!v.isArray() || v.size() < 2) return fallback;
    return {v[usize{0}].asF32(fallback.x), v[usize{1}].asF32(fallback.y)};
}

[[nodiscard]] inline Vec4 vec4From(const json::Value& v, Vec4 fallback = {}) {
    if (!v.isArray() || v.size() < 4) return fallback;
    return {v[usize{0}].asF32(fallback.x), v[usize{1}].asF32(fallback.y),
            v[usize{2}].asF32(fallback.z), v[usize{3}].asF32(fallback.w)};
}

[[nodiscard]] inline Rect rectFrom(const json::Value& v, Rect fallback = kFullUV) {
    if (!v.isArray() || v.size() < 4) return fallback;
    return {v[usize{0}].asF32(fallback.x), v[usize{1}].asF32(fallback.y),
            v[usize{2}].asF32(fallback.width), v[usize{3}].asF32(fallback.height)};
}

// -------------------------------------------------------------------- reflection

// The set of component types a scene file may contain. Types not registered here
// are silently skipped on save and ignored on load, which is deliberate: a scene
// authored by a tool that knows about more components than this build does should
// still load, minus what this build cannot represent.
class ComponentRegistry {
public:
    struct Entry {
        std::string name;
        std::function<bool(Registry&, Entity)>                                  has;
        std::function<json::Value(Registry&, Entity, const SerializeContext&)>  save;
        std::function<void(Registry&, Entity, const json::Value&,
                           const SerializeContext&)>                            load;
    };

    // `save` and `load` see the component itself; the plumbing to reach it from an
    // entity is generated here, so a new component type costs two small lambdas.
    template <class T, class SaveFn, class LoadFn>
    void add(std::string name, SaveFn saveFn, LoadFn loadFn) {
        Entry entry;
        entry.name = std::move(name);
        entry.has  = [](Registry& r, Entity e) { return r.has<T>(e); };
        entry.save = [saveFn](Registry& r, Entity e, const SerializeContext& ctx) {
            return saveFn(r.get<T>(e), ctx);
        };
        entry.load = [loadFn](Registry& r, Entity e, const json::Value& v,
                              const SerializeContext& ctx) {
            T& component = r.has<T>(e) ? r.get<T>(e) : r.emplace<T>(e);
            loadFn(component, v, ctx);
        };
        m_entries.push_back(std::move(entry));
    }

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] const Entry*              find(std::string_view name) const;

private:
    std::vector<Entry> m_entries;
};

// Transform2D, SpriteComp, SpriteAnimator and Velocity, registered on first use.
// Modules that own their own components (physics, say) add theirs to this table.
[[nodiscard]] ComponentRegistry& defaultComponents();

// ---------------------------------------------------------------- scene & prefab

// A whole scene: camera, animation clips, tilemap, and every entity with its
// components. Parent links are written as indices into the entity list, so the
// file survives entity ids being handed out differently on the next run.
[[nodiscard]] json::Value saveScene(Scene& scene, const SerializeContext& ctx,
                                    const ComponentRegistry& types = defaultComponents());

// Replaces everything in `scene`. Returns false (leaving the scene cleared) if the
// document is not a scene.
bool loadScene(Scene& scene, const json::Value& doc, const SerializeContext& ctx,
               const ComponentRegistry& types = defaultComponents());

// One entity and its descendants, as a reusable template.
[[nodiscard]] json::Value savePrefab(Scene& scene, Entity root, const SerializeContext& ctx,
                                     const ComponentRegistry& types = defaultComponents());

// Stamp a prefab into a scene at `at`, returning the root. The prefab's own root
// position is treated as an offset from `at`.
Entity instantiate(Scene& scene, const json::Value& prefab, Vec2 at, const SerializeContext& ctx,
                   const ComponentRegistry& types = defaultComponents());

}
