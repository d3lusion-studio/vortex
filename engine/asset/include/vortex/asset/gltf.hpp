#pragma once
#include "vortex/anim/clip.hpp"
#include "vortex/anim/skeleton.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/mesh.hpp"

#include <string>
#include <vector>

namespace vortex::assets {

// One drawable chunk of a glTF: a primitive, already in the engine's vertex layout, plus the
// material it asked for. glTF splits a mesh by material — a character's face and body are two
// primitives — and so do we, because a draw call can only bind one material anyway.
struct GltfPrimitive {
    renderer::MeshData mesh;
    i32                material = -1;   // index into GltfModel::materials, or -1
};

// A glTF material, reduced to what the renderer actually implements. Textures are named by
// their file, not loaded: the importer does not own a graphics device, and deciding when an
// image becomes a GPU texture is the caller's business, not the parser's.
struct GltfMaterial {
    std::string name;
    std::string baseColorTexture;   // path, relative to the .gltf
    std::string normalTexture;
    std::string metallicRoughnessTexture;
    std::string emissiveTexture;

    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    f32  metallic  = 1.0f;
    f32  roughness = 1.0f;
    Vec3 emissive{0.0f, 0.0f, 0.0f};
    bool doubleSided = false;
    bool blend       = false;
};

struct GltfModel {
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfMaterial>  materials;

    // Empty for a static model. When present, every primitive's vertices carry joint indices
    // into it and the weights to blend them by.
    anim::Skeleton            skeleton;
    std::vector<anim::Clip>   animations;

    // The transform the mesh node sits at in the glTF's scene. A model authored a metre off
    // the origin, or in centimetres, comes in wrong without it.
    Mat4 rootTransform = Mat4::identity();

    [[nodiscard]] bool valid() const { return !primitives.empty(); }
    [[nodiscard]] bool skinned() const { return !skeleton.empty(); }

    [[nodiscard]] const anim::Clip* findClip(std::string_view name) const;
};

// Load a .gltf (JSON + external .bin + external images). `error` receives a message on
// failure, and the returned model is empty.
//
// Binary .glb is not read. It is the same data in one file with a header, and adding it is a
// day's work — but nothing here needs it yet, and an importer that half-reads two formats is
// worse than one that fully reads one.
[[nodiscard]] GltfModel loadGltf(const char* path, std::string* error = nullptr);

}
