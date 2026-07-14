#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/mesh.hpp"

#include <memory>

namespace vortex::renderer {

// How a posed mesh gets from its bind pose to its animated one.
//
// The two do exactly the same arithmetic — blend four bone matrices by four weights, apply to
// the vertex — and differ only in *where*. Which is precisely why they can be swapped: any
// visible difference between them is a bug in one of them, and that makes them each other's
// test. (Run the same scene both ways and diff the images; the answer should be zero.)
//
//   Gpu — the bone matrices go up in a storage buffer and the vertex shader does the blend.
//         Vertices are uploaded once, ever. This is what you ship.
//   Cpu — the vertices are posed on the CPU and re-uploaded every frame. It costs bandwidth
//         proportional to the mesh, every frame, forever. It exists because it is the simple,
//         obviously-correct implementation to check the fast one against, and because a
//         backend without storage buffers (WebGPU, today) has no other option.
enum class SkinningBackend { Gpu, Cpu };

// Mirrors rhi::defaultGraphicsAPI(): the environment picks, the code does not care.
// VORTEX_SKINNING=cpu|gpu. Defaults to Gpu.
[[nodiscard]] SkinningBackend defaultSkinningBackend();
[[nodiscard]] const char*     skinningBackendName(SkinningBackend);

using SkinHandle = Handle<struct SkinTag>;

// The interface both backends satisfy. Deliberately narrow: register a mesh, hand it a pose,
// fill in an instance. Nothing here mentions buffers or shaders, so nothing above it has to
// know which backend it got — which is the entire point of the split.
class ISkinner {
public:
    virtual ~ISkinner() = default;

    ISkinner(const ISkinner&)            = delete;
    ISkinner& operator=(const ISkinner&) = delete;

    // Register a skinned mesh. `data` must carry joint indices and weights.
    [[nodiscard]] virtual SkinHandle addMesh(const MeshData& data) = 0;

    // This frame's pose: one skinning matrix per joint, with the inverse bind already folded
    // in (anim::Skeleton::computeSkinningMatrices produces exactly this).
    virtual void setPose(SkinHandle, const Mat4* bones, u32 boneCount) = 0;

    // How much of each of the mesh's morph targets to apply. Morphing changes the SHAPE, posing
    // puts that shape into a POSE — and in that order, on both backends, because the deltas were
    // authored in the mesh's rest space.
    virtual void setMorphWeights(SkinHandle, const f32* weights, u32 count) = 0;

    // Fill in whatever `inst` needs to be drawn in that pose. What it fills differs by
    // backend — a mesh handle here, a bone pointer there — and that is the difference the
    // caller is spared.
    virtual void apply(SkinHandle, MeshInstance& inst) const = 0;

    [[nodiscard]] virtual const char* name() const = 0;

protected:
    ISkinner() = default;
};

[[nodiscard]] std::unique_ptr<ISkinner> createSkinner(SkinningBackend, MeshRenderer&);
[[nodiscard]] std::unique_ptr<ISkinner> createSkinner(MeshRenderer&);   // the default backend

}
