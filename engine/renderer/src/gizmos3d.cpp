#include "vortex/renderer/gizmos3d.hpp"

#include "vortex/core/math/scalar.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "line3d_vert_spv.h"
#include "line3d_frag_spv.h"

#include <cmath>
#include <cstring>

namespace vortex::renderer {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

// A unit direction perpendicular to `n`, for laying out a circle in a plane.
Vec3 anyPerp(Vec3 n) {
    return std::fabs(n.x) > 0.9f ? normalize(cross(n, {0.0f, 1.0f, 0.0f}))
                                 : normalize(cross(n, {1.0f, 0.0f, 0.0f}));
}

}

Gizmos3D::Gizmos3D(rhi::IGraphicsDevice& device, rhi::Format colorFormat,
                   rhi::Format depthFormat, u32 maxVertices)
    : m_device(device), m_maxVertices(maxVertices) {

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = toBytes(line3d_vert_spv, line3d_vert_spv_size);
    pd.fragmentSpirv       = toBytes(line3d_frag_spv, line3d_frag_spv_size);
    pd.vertexWgsl          = line3d_vert_spv_wgsl;
    pd.fragmentWgsl        = line3d_frag_spv_wgsl;
    pd.vertexLayout.stride = sizeof(Vertex);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(Vertex, pos)},
        {.location = 1, .format = rhi::VertexFormat::Float4, .offset = offsetof(Vertex, color)},
    };
    pd.topology         = rhi::PrimitiveTopology::LineList;
    pd.cull             = rhi::CullMode::None;
    pd.colorFormat      = colorFormat;
    pd.alphaBlend       = true;
    pd.pushConstantSize = sizeof(Mat4);
    if (depthFormat != rhi::Format::Undefined) {
        pd.depthTest    = true;
        pd.depthWrite   = false;   // gizmos read the scene's depth but do not occlude it
        pd.depthCompare = rhi::CompareOp::LessEqual;
        pd.depthFormat  = depthFormat;
    }
    pd.debugName = "gizmos3d_pipeline";
    m_pipeline = m_device.createGraphicsPipeline(pd);

    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i)
        m_buffers[i] = m_device.createBuffer(
            {.size = static_cast<u64>(maxVertices) * sizeof(Vertex),
             .usage = rhi::BufferUsage::Vertex, .domain = rhi::MemoryDomain::Upload,
             .debugName = "gizmos3d_vertices"});

    m_vertices.reserve(maxVertices);
}

Gizmos3D::~Gizmos3D() {
    m_device.waitIdle();
    for (rhi::BufferHandle b : m_buffers)
        if (b.valid()) m_device.destroyBuffer(b);
    m_device.destroyPipeline(m_pipeline);
}

void Gizmos3D::begin() { m_vertices.clear(); }

void Gizmos3D::line(Vec3 a, Vec3 b, Vec4 color) {
    if (m_vertices.size() + 2 > m_maxVertices) return;   // silently cap; a debug overlay never crashes
    m_vertices.push_back({a, color});
    m_vertices.push_back({b, color});
}

void Gizmos3D::ray(Vec3 origin, Vec3 direction, f32 length, Vec4 color) {
    line(origin, origin + direction * length, color);
}

void Gizmos3D::box(Vec3 center, Vec3 h, Vec4 color) {
    const Vec3 c[8] = {
        {center.x - h.x, center.y - h.y, center.z - h.z},
        {center.x + h.x, center.y - h.y, center.z - h.z},
        {center.x + h.x, center.y - h.y, center.z + h.z},
        {center.x - h.x, center.y - h.y, center.z + h.z},
        {center.x - h.x, center.y + h.y, center.z - h.z},
        {center.x + h.x, center.y + h.y, center.z - h.z},
        {center.x + h.x, center.y + h.y, center.z + h.z},
        {center.x - h.x, center.y + h.y, center.z + h.z}};
    static constexpr int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},   // bottom
        {4, 5}, {5, 6}, {6, 7}, {7, 4},   // top
        {0, 4}, {1, 5}, {2, 6}, {3, 7}};  // sides
    for (const auto& e : edges) line(c[e[0]], c[e[1]], color);
}

void Gizmos3D::orientedBox(const Mat4& transform, Vec3 h, Vec4 color) {
    Vec3 c[8];
    int  i = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2)
            for (int sz = -1; sz <= 1; sz += 2) {
                const Vec4 p = transform * Vec4{static_cast<f32>(sx) * h.x,
                                                static_cast<f32>(sy) * h.y,
                                                static_cast<f32>(sz) * h.z, 1.0f};
                c[i++] = {p.x, p.y, p.z};
            }
    // Indices into the sx,sy,sz lexicographic ordering above.
    static constexpr int edges[12][2] = {
        {0, 1}, {0, 2}, {1, 3}, {2, 3},   // -x face
        {4, 5}, {4, 6}, {5, 7}, {6, 7},   // +x face
        {0, 4}, {1, 5}, {2, 6}, {3, 7}};  // connectors
    for (const auto& e : edges) line(c[e[0]], c[e[1]], color);
}

void Gizmos3D::circle(Vec3 center, Vec3 normal, f32 radius, Vec4 color, u32 segments) {
    const Vec3 n = normalize(normal);
    const Vec3 u = anyPerp(n);
    const Vec3 v = cross(n, u);
    Vec3 prev = center + u * radius;
    for (u32 s = 1; s <= segments; ++s) {
        const f32  a = kTwoPi * static_cast<f32>(s) / static_cast<f32>(segments);
        const Vec3 p = center + (u * std::cos(a) + v * std::sin(a)) * radius;
        line(prev, p, color);
        prev = p;
    }
}

void Gizmos3D::wireSphere(Vec3 center, f32 radius, Vec4 color, u32 segments) {
    circle(center, {1.0f, 0.0f, 0.0f}, radius, color, segments);
    circle(center, {0.0f, 1.0f, 0.0f}, radius, color, segments);
    circle(center, {0.0f, 0.0f, 1.0f}, radius, color, segments);
}

void Gizmos3D::axes(const Mat4& transform, f32 length) {
    const Vec4 o4 = transform * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const Vec3 o{o4.x, o4.y, o4.z};
    const Vec4 x4 = transform * Vec4{length, 0.0f, 0.0f, 1.0f};
    const Vec4 y4 = transform * Vec4{0.0f, length, 0.0f, 1.0f};
    const Vec4 z4 = transform * Vec4{0.0f, 0.0f, length, 1.0f};
    line(o, {x4.x, x4.y, x4.z}, {1.0f, 0.2f, 0.2f, 1.0f});   // X: red
    line(o, {y4.x, y4.y, y4.z}, {0.2f, 1.0f, 0.2f, 1.0f});   // Y: green
    line(o, {z4.x, z4.y, z4.z}, {0.3f, 0.5f, 1.0f, 1.0f});   // Z: blue
}

void Gizmos3D::grid(Vec3 center, f32 size, u32 divisions, Vec4 color) {
    if (divisions == 0) return;
    const f32 half = size * 0.5f;
    const f32 step = size / static_cast<f32>(divisions);
    for (u32 i = 0; i <= divisions; ++i) {
        const f32 d = -half + step * static_cast<f32>(i);
        line({center.x - half, center.y, center.z + d}, {center.x + half, center.y, center.z + d}, color);
        line({center.x + d, center.y, center.z - half}, {center.x + d, center.y, center.z + half}, color);
    }
}

void Gizmos3D::flush(rhi::ICommandList& cmd, const Mat4& viewProjection) {
    if (m_vertices.empty()) return;
    const u32 count = static_cast<u32>(m_vertices.size());

    const rhi::BufferHandle vbo = m_buffers[m_frame];
    m_device.updateBuffer(vbo, m_vertices.data(), static_cast<u64>(count) * sizeof(Vertex));

    Mat4 vp = viewProjection;
    cmd.setPipeline(m_pipeline);
    cmd.pushConstants(&vp, sizeof(Mat4));
    cmd.setVertexBuffer(0, vbo);
    cmd.draw(count);

    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
}

}
