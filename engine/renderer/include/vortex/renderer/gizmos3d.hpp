#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <array>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; class ICommandList; }

namespace vortex::renderer {

// Immediate-mode 3D debug drawing: world-space lines, boxes, spheres, axes and grids
// for visualising bounds, rays, transforms and anything else that has no geometry of
// its own. It is the 3D counterpart of debug::DebugDraw (which is 2D and screen-space)
// and renders through its own line pipeline rather than the sprite batch.
//
// Per frame: begin() to clear, accumulate primitives, then flush(cmd, viewProjection)
// inside a render pass whose colour/depth formats match the ones given at construction.
// Pass a depth format to have the gizmos depth-test against the scene; leave it
// Undefined to draw them always-on-top.
class Gizmos3D {
public:
    Gizmos3D(rhi::IGraphicsDevice& device, rhi::Format colorFormat,
             rhi::Format depthFormat = rhi::Format::Undefined, u32 maxVertices = 200000);
    ~Gizmos3D();
    Gizmos3D(const Gizmos3D&)            = delete;
    Gizmos3D& operator=(const Gizmos3D&) = delete;

    void begin();   // drop last frame's primitives

    void line(Vec3 a, Vec3 b, Vec4 color);
    void ray(Vec3 origin, Vec3 direction, f32 length, Vec4 color);

    // Axis-aligned wire box from its centre and half-extents (12 edges).
    void box(Vec3 center, Vec3 halfExtents, Vec4 color);

    // An oriented wire box: the unit box +/-halfExtents put through `transform`.
    void orientedBox(const Mat4& transform, Vec3 halfExtents, Vec4 color);

    // Three great circles (XY, XZ, YZ), which reads as a sphere from any angle.
    void wireSphere(Vec3 center, f32 radius, Vec4 color, u32 segments = 24);

    void circle(Vec3 center, Vec3 normal, f32 radius, Vec4 color, u32 segments = 24);

    // R/G/B lines along the local X/Y/Z of `transform` — the classic gizmo for
    // seeing where an entity's frame points.
    void axes(const Mat4& transform, f32 length = 1.0f);

    // A grid in the XZ plane, `divisions` cells across `size` world units.
    void grid(Vec3 center, f32 size, u32 divisions, Vec4 color);

    // Upload the accumulated lines and draw them under `viewProjection`.
    void flush(rhi::ICommandList& cmd, const Mat4& viewProjection);

    [[nodiscard]] usize vertexCount() const { return m_vertices.size(); }

private:
    struct Vertex {
        Vec3 pos;
        Vec4 color;
    };

    rhi::IGraphicsDevice&                                 m_device;
    rhi::PipelineHandle                                   m_pipeline;
    std::array<rhi::BufferHandle, rhi::kMaxFramesInFlight> m_buffers{};   // one per frame in flight
    u32                                                   m_frame = 0;
    u32                                                   m_maxVertices;
    std::vector<Vertex>                                   m_vertices;
};

}
