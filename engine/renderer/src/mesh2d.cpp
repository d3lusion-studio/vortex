#include "vortex/renderer/mesh2d.hpp"

#include "vortex/core/math/math.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "mesh2d_vert_spv.h"
#include "mesh2d_frag_spv.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vortex::renderer {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

u64 textureKey(rhi::TextureHandle h) {
    return (static_cast<u64>(h.generation) << 32) | h.index;
}

rhi::SamplerDesc samplerDescFor(SpriteSampler s) {
    const bool nearest = s == SpriteSampler::NearestClamp || s == SpriteSampler::NearestRepeat;
    const bool repeat  = s == SpriteSampler::LinearRepeat || s == SpriteSampler::NearestRepeat;
    const rhi::Filter      filter  = nearest ? rhi::Filter::Nearest : rhi::Filter::Linear;
    const rhi::AddressMode address = repeat ? rhi::AddressMode::Repeat
                                            : rhi::AddressMode::ClampToEdge;
    return {.minFilter = filter, .magFilter = filter, .addressU = address, .addressV = address};
}

// UVs are the position mapped over the shape's bounding box, so (0,0) is the box's
// top-left and (1,1) its bottom-right — the same convention a sprite's quad uses, which
// means a texture drops onto a shape the way you expect.
Vec2 boxUV(Vec2 p, Vec2 halfExtents) {
    if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f) return {0.5f, 0.5f};
    return {(p.x + halfExtents.x) / (halfExtents.x * 2.0f),
            (halfExtents.y - p.y) / (halfExtents.y * 2.0f)};
}

// Append a fan of triangles around `center` following `rim`, which is already in order.
void fan(Mesh2DData& out, Vec2 center, const std::vector<Vec2>& rim, Vec2 half, Vec4 color) {
    if (rim.size() < 2) return;
    const auto base = static_cast<u32>(out.vertices.size());
    out.vertices.push_back({center, boxUV(center, half), color});
    for (Vec2 p : rim) out.vertices.push_back({p, boxUV(p, half), color});

    for (u32 i = 0; i + 1 < static_cast<u32>(rim.size()); ++i) {
        out.indices.push_back(base);
        out.indices.push_back(base + 1 + i);
        out.indices.push_back(base + 2 + i);
    }
}

// The arc's sample points, inclusive of both ends.
std::vector<Vec2> arcPoints(f32 radius, f32 startAngle, f32 endAngle, u32 segments) {
    const u32 steps = std::max(1u, segments);
    std::vector<Vec2> pts;
    pts.reserve(steps + 1u);
    for (u32 i = 0; i <= steps; ++i) {
        const f32 t = static_cast<f32>(i) / static_cast<f32>(steps);
        const f32 a = startAngle + (endAngle - startAngle) * t;
        pts.push_back({std::cos(a) * radius, std::sin(a) * radius});
    }
    return pts;
}

} // namespace

// ---------------------------------------------------------------------------
// Shape generators
// ---------------------------------------------------------------------------

Mesh2DData makeRectMesh(Vec2 size, Vec4 color) {
    const Vec2 h{size.x * 0.5f, size.y * 0.5f};
    Mesh2DData m;
    m.vertices = {
        {{-h.x,  h.y}, {0.0f, 0.0f}, color},
        {{ h.x,  h.y}, {1.0f, 0.0f}, color},
        {{ h.x, -h.y}, {1.0f, 1.0f}, color},
        {{-h.x, -h.y}, {0.0f, 1.0f}, color},
    };
    m.indices = {0, 1, 2, 2, 3, 0};
    return m;
}

Mesh2DData makeCircleMesh(f32 radius, u32 segments, Vec4 color) {
    return makeRegularPolygonMesh(std::max(3u, segments), radius, color);
}

Mesh2DData makeRegularPolygonMesh(u32 sides, f32 radius, Vec4 color) {
    Mesh2DData m;
    if (sides < 3u || radius <= 0.0f) return m;

    const Vec2 half{radius, radius};
    std::vector<Vec2> rim;
    rim.reserve(sides + 1u);
    for (u32 i = 0; i <= sides; ++i) {   // repeat the first point to close the fan
        const f32 a = kTwoPi * static_cast<f32>(i % sides) / static_cast<f32>(sides);
        rim.push_back({std::cos(a) * radius, std::sin(a) * radius});
    }
    fan(m, {0.0f, 0.0f}, rim, half, color);
    return m;
}

Mesh2DData makeSectorMesh(f32 radius, f32 startAngle, f32 endAngle, u32 segments, Vec4 color) {
    Mesh2DData m;
    if (radius <= 0.0f || startAngle == endAngle) return m;
    fan(m, {0.0f, 0.0f}, arcPoints(radius, startAngle, endAngle, segments), {radius, radius}, color);
    return m;
}

Mesh2DData makeSegmentMesh(f32 radius, f32 startAngle, f32 endAngle, u32 segments, Vec4 color) {
    Mesh2DData m;
    if (radius <= 0.0f || startAngle == endAngle) return m;

    // Closed through the chord rather than the centre: fan from the arc's own first
    // point, so the triangle back to the origin that a sector would add is left out.
    const std::vector<Vec2> pts = arcPoints(radius, startAngle, endAngle, segments);
    if (pts.size() < 3u) return m;

    const Vec2 half{radius, radius};
    const auto base = static_cast<u32>(m.vertices.size());
    for (Vec2 p : pts) m.vertices.push_back({p, boxUV(p, half), color});
    for (u32 i = 1; i + 1 < static_cast<u32>(pts.size()); ++i) {
        m.indices.push_back(base);
        m.indices.push_back(base + i);
        m.indices.push_back(base + i + 1u);
    }
    return m;
}

Mesh2DData makeArcMesh(f32 radius, f32 thickness, f32 startAngle, f32 endAngle,
                       u32 segments, Vec4 color) {
    Mesh2DData m;
    if (radius <= 0.0f || thickness <= 0.0f || startAngle == endAngle) return m;

    const f32 inner = std::max(0.0f, radius - thickness * 0.5f);
    const f32 outer = radius + thickness * 0.5f;
    const Vec2 half{outer, outer};

    const std::vector<Vec2> outerPts = arcPoints(outer, startAngle, endAngle, segments);
    const std::vector<Vec2> innerPts = arcPoints(inner, startAngle, endAngle, segments);

    // Two rims zipped into a strip of quads.
    const auto base = static_cast<u32>(m.vertices.size());
    for (usize i = 0; i < outerPts.size(); ++i) {
        m.vertices.push_back({outerPts[i], boxUV(outerPts[i], half), color});
        m.vertices.push_back({innerPts[i], boxUV(innerPts[i], half), color});
    }
    for (u32 i = 0; i + 1 < static_cast<u32>(outerPts.size()); ++i) {
        const u32 o0 = base + i * 2u, i0 = o0 + 1u;
        const u32 o1 = o0 + 2u,       i1 = o0 + 3u;
        m.indices.push_back(o0); m.indices.push_back(i0); m.indices.push_back(i1);
        m.indices.push_back(i1); m.indices.push_back(o1); m.indices.push_back(o0);
    }
    return m;
}

Mesh2DData makeConvexPolygonMesh(const Vec2* points, usize count, Vec4 color) {
    Mesh2DData m;
    if (points == nullptr || count < 3u) return m;

    Vec2 lo = points[0], hi = points[0];
    for (usize i = 1; i < count; ++i) {
        lo = {std::min(lo.x, points[i].x), std::min(lo.y, points[i].y)};
        hi = {std::max(hi.x, points[i].x), std::max(hi.y, points[i].y)};
    }
    const Vec2 half{std::max(std::fabs(lo.x), std::fabs(hi.x)),
                    std::max(std::fabs(lo.y), std::fabs(hi.y))};

    for (usize i = 0; i < count; ++i)
        m.vertices.push_back({points[i], boxUV(points[i], half), color});
    for (u32 i = 1; i + 1 < static_cast<u32>(count); ++i) {
        m.indices.push_back(0);
        m.indices.push_back(i);
        m.indices.push_back(i + 1u);
    }
    return m;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

Mesh2DRenderer::Mesh2DRenderer(rhi::IGraphicsDevice& device, rhi::Format colorFormat,
                               rhi::Format depthFormat)
    : m_device(device) {

    for (u32 i = 0; i < kSpriteSamplerCount; ++i)
        m_samplers[i] = m_device.createSampler(samplerDescFor(static_cast<SpriteSampler>(i)));

    const u8 whitePixel[4] = {255, 255, 255, 255};
    m_white = m_device.createTexture({.width = 1, .height = 1, .debugName = "mesh2d_white"},
                                     whitePixel);

    // One pipeline per blend mode. They differ in nothing else, so they are built from
    // one description with the two blend flags flipped.
    for (u32 i = 0; i < kBlendMode2DCount; ++i) {
        const auto mode = static_cast<BlendMode2D>(i);

        rhi::GraphicsPipelineDesc pd;
        pd.vertexSpirv   = toBytes(mesh2d_vert_spv, mesh2d_vert_spv_size);
        pd.fragmentSpirv = toBytes(mesh2d_frag_spv, mesh2d_frag_spv_size);
        pd.vertexWgsl    = mesh2d_vert_spv_wgsl;
        pd.fragmentWgsl  = mesh2d_frag_spv_wgsl;

        pd.vertexLayout.stride = sizeof(Vertex2D);
        pd.vertexLayout.attributes = {
            {.location = 0, .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex2D, position)},
            {.location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex2D, uv)},
            {.location = 2, .format = rhi::VertexFormat::Float4, .offset = offsetof(Vertex2D, color)},
        };

        pd.topology           = rhi::PrimitiveTopology::TriangleList;
        pd.cull               = rhi::CullMode::None;
        pd.colorFormat        = colorFormat;
        pd.alphaBlend         = mode == BlendMode2D::Blend;
        pd.additiveBlend      = mode == BlendMode2D::Additive;
        pd.hasMaterialTexture = true;
        pd.pushConstantSize   = sizeof(Push);
        if (depthFormat != rhi::Format::Undefined) {
            pd.depthTest    = true;
            pd.depthWrite   = mode == BlendMode2D::Opaque;
            pd.depthCompare = rhi::CompareOp::LessEqual;
            pd.depthFormat  = depthFormat;
        }
        pd.debugName = "mesh2d_pipeline";

        m_pipelines[i] = m_device.createGraphicsPipeline(pd);
    }
}

Mesh2DRenderer::~Mesh2DRenderer() {
    m_device.waitIdle();
    for (GpuMesh& m : m_meshes) {
        if (!m.alive) continue;
        m_device.destroyBuffer(m.vbo);
        m_device.destroyBuffer(m.ibo);
    }
    for (auto& [key, groups] : m_bindGroupCache)
        for (rhi::BindGroupHandle bg : groups)
            if (bg.valid()) m_device.destroyBindGroup(bg);
    m_device.destroyTexture(m_white);
    for (rhi::SamplerHandle s : m_samplers) m_device.destroySampler(s);
    for (rhi::PipelineHandle p : m_pipelines) m_device.destroyPipeline(p);
}

rhi::PipelineHandle Mesh2DRenderer::pipeline(BlendMode2D mode) const {
    return m_pipelines[static_cast<usize>(mode)];
}

Mesh2DHandle Mesh2DRenderer::createMesh(const Mesh2DData& data) {
    if (data.empty()) return {};

    GpuMesh gm;
    gm.indexCount = static_cast<u32>(data.indices.size());
    gm.alive      = true;
    gm.vbo = m_device.createBuffer(
        {.size = data.vertices.size() * sizeof(Vertex2D), .usage = rhi::BufferUsage::Vertex,
         .domain = rhi::MemoryDomain::Device, .debugName = "mesh2d_vertices"},
        data.vertices.data());
    gm.ibo = m_device.createBuffer(
        {.size = data.indices.size() * sizeof(u32), .usage = rhi::BufferUsage::Index,
         .domain = rhi::MemoryDomain::Device, .debugName = "mesh2d_indices"},
        data.indices.data());

    if (!m_freeMeshes.empty()) {
        const u32 index = m_freeMeshes.back();
        m_freeMeshes.pop_back();
        gm.generation = m_meshes[index].generation;
        m_meshes[index] = gm;
        return {.index = index, .generation = gm.generation};
    }

    const auto index = static_cast<u32>(m_meshes.size());
    m_meshes.push_back(gm);
    return {.index = index, .generation = 0};
}

void Mesh2DRenderer::destroyMesh(Mesh2DHandle h) {
    if (!h.valid() || h.index >= m_meshes.size()) return;
    GpuMesh& gm = m_meshes[h.index];
    if (!gm.alive || gm.generation != h.generation) return;

    m_device.waitIdle();
    m_device.destroyBuffer(gm.vbo);
    m_device.destroyBuffer(gm.ibo);
    gm.alive = false;
    ++gm.generation;   // any handle still pointing here is now stale
    m_freeMeshes.push_back(h.index);
}

rhi::BindGroupHandle Mesh2DRenderer::bindGroupFor(rhi::TextureHandle texture,
                                                  SpriteSampler sampler) {
    SamplerBindGroups& groups = m_bindGroupCache[textureKey(texture)];
    rhi::BindGroupHandle& bg  = groups[static_cast<usize>(sampler)];
    if (!bg.valid())
        bg = m_device.createBindGroup({.texture = texture,
                                       .sampler = m_samplers[static_cast<usize>(sampler)]});
    return bg;
}

void Mesh2DRenderer::releaseTexture(rhi::TextureHandle texture) {
    auto it = m_bindGroupCache.find(textureKey(texture));
    if (it == m_bindGroupCache.end()) return;
    for (rhi::BindGroupHandle bg : it->second)
        if (bg.valid()) m_device.destroyBindGroup(bg);
    m_bindGroupCache.erase(it);
}

void Mesh2DRenderer::begin(const Mat4& viewProjection) {
    m_viewProjection = viewProjection;
    m_items.clear();
    m_drawCalls = 0;
}

void Mesh2DRenderer::submit(const Mesh2DInstance& instance) {
    m_items.push_back(instance);
}

void Mesh2DRenderer::end(rhi::ICommandList& cmd) {
    if (m_items.empty()) return;

    {
        // Layer decides overlap and must lead. Below it, sorting by pipeline and then by
        // texture is what lets the run loop skip redundant state changes — each mesh is
        // still its own draw call (it has its own buffers), but a run of meshes sharing a
        // material rebinds nothing between them.
        VORTEX_PROFILE_ZONE("mesh2d.sort");
        const auto order = [](const Mesh2DInstance& a, const Mesh2DInstance& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            if (a.blend != b.blend) return a.blend < b.blend;
            const u64 pa = a.material.pipeline.index, pb = b.material.pipeline.index;
            if (pa != pb) return pa < pb;
            return textureKey(a.texture) < textureKey(b.texture);
        };
        if (!std::is_sorted(m_items.begin(), m_items.end(), order))
            std::stable_sort(m_items.begin(), m_items.end(), order);
    }

    rhi::PipelineHandle  boundPipeline{};
    rhi::BindGroupHandle boundGroup{};

    for (const Mesh2DInstance& item : m_items) {
        if (!item.mesh.valid() || item.mesh.index >= m_meshes.size()) continue;
        const GpuMesh& gm = m_meshes[item.mesh.index];
        if (!gm.alive || gm.generation != item.mesh.generation || gm.indexCount == 0u) continue;

        const rhi::PipelineHandle pipe = item.material.valid()
                                       ? item.material.pipeline
                                       : m_pipelines[static_cast<usize>(item.blend)];
        if (pipe != boundPipeline) {
            cmd.setPipeline(pipe);
            boundPipeline = pipe;
            boundGroup    = {};   // a new pipeline invalidates whatever was bound to it
        }

        // A custom material brings its own bindings; otherwise the mesh samples its
        // texture, or the white pixel when it has none (a flat, vertex-coloured shape).
        const rhi::BindGroupHandle group =
            item.material.valid() && item.material.bindGroup.valid()
                ? item.material.bindGroup
                : bindGroupFor(item.texture.valid() ? item.texture : m_white, item.sampler);
        if (group != boundGroup) {
            cmd.setBindGroup(0, group);
            boundGroup = group;
        }

        const Push push{.mvp = m_viewProjection * item.transform, .tint = item.tint};
        cmd.pushConstants(&push, sizeof(Push));

        cmd.setVertexBuffer(0, gm.vbo);
        cmd.setIndexBuffer(gm.ibo, rhi::IndexType::U32);
        cmd.drawIndexed(gm.indexCount);
        ++m_drawCalls;
    }
}

}
