// Load a glTF model into the App's 3D scene as entities the loop draws.
//
// The importer (assets::loadGltf) has always produced meshes, materials, a skeleton and
// animations — but nothing turned that into something an App-based game could USE. Every 3D
// example that wanted a model drove a raw loop and wired the primitives, textures and
// materials to the MeshRenderer by hand. This is that wiring, done once: hand it a path and
// get back entities, each a primitive of the model with its material, that the render3D loop
// already knows how to draw.
//
// Static geometry for now. The model's skeleton and animation clips are parsed and returned
// so a caller can drive them, but this does not itself animate — a skinned entity in the
// loop is the next step, and a bigger one.
#pragma once

#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/renderer/mesh.hpp"

#include <string>
#include <vector>

namespace vortex::app { class App; }

namespace vortex::app {

struct LoadedModel {
    // One entity per glTF primitive (a mesh split by material). Empty on failure.
    std::vector<ecs::Entity> entities;

    // The GPU resources this load created, so a caller that unloads the model can free them:
    // destroy the entities, then destroyMesh/destroyMaterial each of these. Without them the
    // handles are unreachable and reloading leaks a model's worth of buffers every time.
    // (Textures are not here: the AssetManager dedups them by path and owns their lifetime.)
    std::vector<renderer::MeshHandle>     meshes;
    std::vector<renderer::MaterialHandle> materials;

    // How many distinct materials and textures the load created, for a caller that wants to
    // report or verify what it got.
    u32 materialCount = 0;
    u32 textureCount  = 0;
    u32 primitiveCount = 0;

    // The clip names the model shipped, in order. Non-empty for an animated model even
    // though this loader does not play them — a viewer or a game reads this to know what it
    // could drive.
    std::vector<std::string> animations;

    [[nodiscard]] bool ok() const { return !entities.empty(); }
};

// Load `gltfPath` into the active scene, placed at `position` and uniformly scaled by
// `scale`. Requires AppConfig::render3D (there is no MeshRenderer otherwise) — returns an
// empty LoadedModel and logs if it is off or the file will not load.
//
// Main thread only: it uploads textures through App::loadTexture, which owns the device.
// Call it from onStart, or from a hook in the single-threaded loop (render3D forbids the
// threaded one anyway).
//
// Textures are read relative to the model's own directory, the way glTF references them.
// The node's root transform is baked into the meshes, so `position`/`scale` place the whole
// model without having to decompose a matrix into the entity's TRS.
[[nodiscard]] LoadedModel loadModel(App& app, const char* gltfPath, Vec3 position = {0, 0, 0},
                                    f32 scale = 1.0f);

}
