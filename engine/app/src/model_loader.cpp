#include "vortex/app/model_loader.hpp"

#include "vortex/app/app.hpp"
#include "vortex/asset/gltf.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/renderer/mesh.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vortex::app {

namespace {

// The directory a model's textures are relative to.
[[nodiscard]] std::string directoryOf(const char* path) {
    const std::filesystem::path p(path);
    return p.has_parent_path() ? p.parent_path().string() + "/" : std::string{};
}

// Bake a transform into a mesh's vertices, so the entity that carries it needs only a plain
// TRS placement. Positions go through the full matrix; the TANGENT (a direction along the
// surface) goes through the 3x3; the NORMAL goes through the 3x3 inverse-transpose, which is
// the only transform that keeps a normal perpendicular to its surface under a non-uniform
// scale. Using the plain 3x3 for the normal, as an earlier version did, skews the lighting of
// any model whose root node scales unevenly.
void bake(renderer::MeshData& data, const Mat4& m) {
    const auto mul3 = [](const f32 r[9], Vec3 v) {
        return Vec3{r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z,
                    r[6] * v.x + r[7] * v.y + r[8] * v.z};
    };

    // The upper-left 3x3, row-major for mul3 above.
    const f32 a[9] = {m.at(0, 0), m.at(0, 1), m.at(0, 2),
                      m.at(1, 0), m.at(1, 1), m.at(1, 2),
                      m.at(2, 0), m.at(2, 1), m.at(2, 2)};

    // The normal matrix is the cofactor (adjugate-transpose) of that 3x3 — it equals
    // det * inverse-transpose, and the det scale washes out when the normal is renormalised.
    const f32 n[9] = {
        a[4] * a[8] - a[5] * a[7], a[5] * a[6] - a[3] * a[8], a[3] * a[7] - a[4] * a[6],
        a[2] * a[7] - a[1] * a[8], a[0] * a[8] - a[2] * a[6], a[1] * a[6] - a[0] * a[7],
        a[1] * a[5] - a[2] * a[4], a[2] * a[3] - a[0] * a[5], a[0] * a[4] - a[1] * a[3]};

    const auto point = [&](Vec3 v) {
        return Vec3{m.at(0, 0) * v.x + m.at(0, 1) * v.y + m.at(0, 2) * v.z + m.at(0, 3),
                    m.at(1, 0) * v.x + m.at(1, 1) * v.y + m.at(1, 2) * v.z + m.at(1, 3),
                    m.at(2, 0) * v.x + m.at(2, 1) * v.y + m.at(2, 2) * v.z + m.at(2, 3)};
    };

    for (renderer::MeshVertex& v : data.vertices) {
        v.position = point(v.position);
        v.normal   = normalize(mul3(n, v.normal));
        const Vec3 t = normalize(mul3(a, {v.tangent.x, v.tangent.y, v.tangent.z}));
        v.tangent = {t.x, t.y, t.z, v.tangent.w};
    }
}

}   // namespace

LoadedModel loadModel(App& app, const char* gltfPath, Vec3 position, f32 scale) {
    LoadedModel out;

    renderer::MeshRenderer* mesh = app.mesh3d();
    if (mesh == nullptr) {
        VORTEX_ERROR("Model", "loadModel needs AppConfig::render3D; ignoring '%s'", gltfPath);
        return out;
    }

    std::string error;
    assets::GltfModel model = assets::loadGltf(gltfPath, &error);
    if (!model.valid()) {
        VORTEX_ERROR("Model", "Could not load '%s': %s", gltfPath,
                     error.empty() ? "no primitives" : error.c_str());
        return out;
    }

    const std::string dir = directoryOf(gltfPath);

    // Upload each material's textures once, then build the renderer materials that point at
    // them. A material with no texture still gets a MaterialDesc — its PBR factors are what
    // the surface then shows.
    std::vector<renderer::MaterialHandle> materials(model.materials.size());
    u32 textureCount = 0;
    for (usize i = 0; i < model.materials.size(); ++i) {
        const assets::GltfMaterial& m = model.materials[i];

        const auto tex = [&](const std::string& rel) -> rhi::TextureHandle {
            if (rel.empty()) return {};
            const rhi::TextureHandle h = app.loadTexture((dir + rel).c_str());
            if (h.valid()) ++textureCount;
            return h;
        };

        renderer::MaterialDesc desc;
        desc.albedo            = tex(m.baseColorTexture);
        desc.normalMap         = tex(m.normalTexture);
        desc.metallicRoughness = tex(m.metallicRoughnessTexture);
        desc.emissive          = tex(m.emissiveTexture);
        desc.baseColor         = m.baseColor;
        desc.metallic          = m.metallic;
        desc.roughness         = m.roughness;
        desc.emissiveColor     = m.emissive;
        desc.doubleSided       = m.doubleSided;
        if (m.blend) desc.blend = rhi::BlendMode::Alpha;

        materials[i] = mesh->createMaterial(desc);
    }

    // One entity per primitive. The root transform is baked into the mesh, so the entity's
    // Transform3D is just where the caller wants the model.
    ecs::Registry& reg = app.registry();
    for (assets::GltfPrimitive& prim : model.primitives) {
        bake(prim.mesh, model.rootTransform);
        const renderer::MeshHandle meshHandle = mesh->createMesh(prim.mesh);

        ecs::MeshComp comp;
        comp.mesh = meshHandle;
        if (prim.material >= 0 && static_cast<usize>(prim.material) < materials.size())
            comp.material = materials[static_cast<usize>(prim.material)];
        else if (prim.material >= 0)
            VORTEX_WARN("Model", "'%s' primitive references material %d of %zu; using default",
                        gltfPath, prim.material, materials.size());

        out.meshes.push_back(meshHandle);

        const ecs::Entity e = reg.create();
        reg.emplace<ecs::Transform3D>(e, ecs::Transform3D{.position = position,
                                                          .scale = {scale, scale, scale}});
        reg.emplace<ecs::MeshComp>(e, comp);
        out.entities.push_back(e);
    }

    out.materials      = materials;
    out.materialCount  = static_cast<u32>(model.materials.size());
    out.textureCount   = textureCount;
    out.primitiveCount = static_cast<u32>(model.primitives.size());
    for (const anim::Clip& clip : model.animations) out.animations.push_back(clip.name);

    VORTEX_INFO("Model", "Loaded '%s': %u primitive(s), %u material(s), %u texture(s), "
                         "%zu animation(s)",
                gltfPath, out.primitiveCount, out.materialCount, out.textureCount,
                out.animations.size());
    return out;
}

}
