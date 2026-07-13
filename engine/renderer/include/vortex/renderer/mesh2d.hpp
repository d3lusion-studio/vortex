#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/material.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <array>
#include <unordered_map>
#include <vector>

namespace vortex::rhi {
class IGraphicsDevice;
class ICommandList;
}

namespace vortex::renderer {

// The 2D counterpart to the sprite path. A sprite is always a quad with one texture;
// a Mesh2D is any triangle list, with a colour per vertex. That is what shapes,
// vertex-coloured geometry and hand-built meshes need and a quad cannot express.
struct Vertex2D {
    Vec2 position{0.0f, 0.0f};
    Vec2 uv{0.0f, 0.0f};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct Mesh2DData {
    std::vector<Vertex2D> vertices;
    std::vector<u32>      indices;

    [[nodiscard]] bool empty() const noexcept { return vertices.empty() || indices.empty(); }
};

// How a mesh's fragments combine with what is already in the target. The sprite path
// is always Blend; a mesh gets the choice, because a shape used as a light or a glow
// wants Additive and a solid background wants Opaque (which is also the cheapest).
enum class BlendMode2D : u8 {
    Opaque,
    Blend,
    Additive,
};

inline constexpr u32 kBlendMode2DCount = 3;

// ---------------------------------------------------------------------------
// Shape generators
//
// All of them build meshes centred on the origin, spanning the unit-ish extents given,
// with UVs mapped over the shape's bounding box so a texture applies the obvious way.
// Wind counter-clockwise, matching the sprite quad.
// ---------------------------------------------------------------------------

[[nodiscard]] Mesh2DData makeRectMesh(Vec2 size = {1.0f, 1.0f},
                                      Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

[[nodiscard]] Mesh2DData makeCircleMesh(f32 radius = 0.5f, u32 segments = 32,
                                        Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

// A regular n-gon inscribed in `radius`. `sides` below 3 yields an empty mesh.
[[nodiscard]] Mesh2DData makeRegularPolygonMesh(u32 sides, f32 radius = 0.5f,
                                                Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

// A pie slice: the arc from `startAngle` to `endAngle` closed through the centre.
// Angles are radians, counter-clockwise from +X.
[[nodiscard]] Mesh2DData makeSectorMesh(f32 radius, f32 startAngle, f32 endAngle,
                                        u32 segments = 32,
                                        Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

// A circular segment: the arc closed through its own chord, i.e. the sector minus the
// triangle back to the centre. This is the shape a circle cut by a straight line leaves.
[[nodiscard]] Mesh2DData makeSegmentMesh(f32 radius, f32 startAngle, f32 endAngle,
                                         u32 segments = 32,
                                         Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

// An annulus slice — a stroked arc of the given thickness. `thickness` is measured
// inwards and outwards from `radius`, so the ring spans radius +/- thickness/2.
[[nodiscard]] Mesh2DData makeArcMesh(f32 radius, f32 thickness, f32 startAngle, f32 endAngle,
                                     u32 segments = 32,
                                     Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

// A convex polygon from its points, in order, as a triangle fan about the first point.
// The result is wrong (self-overlapping) for a concave outline — triangulate it
// yourself and hand the triangles to createMesh() if that is what you have.
[[nodiscard]] Mesh2DData makeConvexPolygonMesh(const Vec2* points, usize count,
                                               Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

using Mesh2DHandle = Handle<struct Mesh2DTag>;

// One mesh, drawn once. `material` overrides the renderer's built-in pipeline; leave it
// invalid and the mesh draws with the default shader, textured by `texture` (or flat if
// that is unset too).
struct Mesh2DInstance {
    Mesh2DHandle       mesh;
    Mat4               transform = Mat4::identity();
    Vec4               tint{1.0f, 1.0f, 1.0f, 1.0f};
    rhi::TextureHandle texture;                              // unset => flat vertex colours
    SpriteSampler      sampler = SpriteSampler::LinearClamp;
    BlendMode2D        blend   = BlendMode2D::Blend;
    i32                layer   = 0;
    Material           material;                             // custom shader; see below
};

// Draws Mesh2Ds. Sits alongside SpriteBatch rather than inside it: the two have
// genuinely different vertex data, and folding meshes into the sprite batcher would
// cost every sprite the generality it does not use.
//
// CUSTOM MATERIALS. A Material's pipeline must match the contract this renderer draws
// with, because the renderer, not the material, records the draw:
//
//   vertex layout   Vertex2D  (loc 0 = vec2 pos, 1 = vec2 uv, 2 = vec4 color)
//   push constants  mat4 mvp, vec4 tint      (80 bytes, vertex stage)
//   bind group 0    sampled texture + sampler, if the pipeline declares one
//
// Build it with the same GraphicsPipelineDesc fields mesh2d uses and only the shaders
// differ. A material with its own bind group draws with that instead of `texture`.
class Mesh2DRenderer {
public:
    Mesh2DRenderer(rhi::IGraphicsDevice& device, rhi::Format colorFormat,
                   rhi::Format depthFormat = rhi::Format::Undefined);
    ~Mesh2DRenderer();
    Mesh2DRenderer(const Mesh2DRenderer&)            = delete;
    Mesh2DRenderer& operator=(const Mesh2DRenderer&) = delete;

    // Meshes are immutable and live in device-local memory. Geometry that changes every
    // frame does not belong here — it belongs in the sprite batcher, which re-uploads
    // wholesale by design. Rebuilding a mesh means destroying it and creating another.
    [[nodiscard]] Mesh2DHandle createMesh(const Mesh2DData&);
    void destroyMesh(Mesh2DHandle);

    void begin(const Mat4& viewProjection);
    void submit(const Mesh2DInstance&);
    void end(rhi::ICommandList& cmd);

    // The pipeline the built-in shader uses for a given blend mode. Handy when building
    // a custom Material: take this apart, or just mirror its GraphicsPipelineDesc.
    [[nodiscard]] rhi::PipelineHandle pipeline(BlendMode2D) const;

    // The 1x1 white pixel an untextured mesh samples.
    [[nodiscard]] rhi::TextureHandle whiteTexture() const { return m_white; }

    void releaseTexture(rhi::TextureHandle);

    [[nodiscard]] u32 drawCallCount() const { return m_drawCalls; }

private:
    struct GpuMesh {
        rhi::BufferHandle vbo;
        rhi::BufferHandle ibo;
        u32  indexCount = 0;
        u32  generation = 0;   // bumped on free, so a stale handle cannot resurrect a slot
        bool alive      = false;
    };

    struct Push {
        Mat4 mvp;
        Vec4 tint;
    };

    using SamplerBindGroups = std::array<rhi::BindGroupHandle, kSpriteSamplerCount>;

    rhi::BindGroupHandle bindGroupFor(rhi::TextureHandle, SpriteSampler);

    rhi::IGraphicsDevice& m_device;

    std::array<rhi::PipelineHandle, kBlendMode2DCount>  m_pipelines;
    std::array<rhi::SamplerHandle, kSpriteSamplerCount> m_samplers;
    rhi::TextureHandle m_white;

    std::unordered_map<u64, SamplerBindGroups> m_bindGroupCache;

    std::vector<GpuMesh>        m_meshes;
    std::vector<u32>            m_freeMeshes;
    std::vector<Mesh2DInstance> m_items;
    Mat4                        m_viewProjection;
    u32                         m_drawCalls = 0;
};

}
