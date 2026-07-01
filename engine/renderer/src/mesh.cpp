#include "vortex/renderer/mesh.hpp"

#include "vortex/core/profiler.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "mesh_vert_spv.h"
#include "mesh_frag_spv.h"

#include <cmath>
#include <cstring>

namespace vortex::renderer {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

}

namespace {

void addQuad(MeshData& m, Vec3 n, Vec3 u, Vec3 v, f32 h) {
    const Vec3 c = n * h;
    const Vec3 p[4] = {c - u * h - v * h, c + u * h - v * h,
                       c + u * h + v * h, c - u * h + v * h};
    const Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const u32 base = static_cast<u32>(m.vertices.size());
    for (int i = 0; i < 4; ++i) m.vertices.push_back({p[i], n, uv[i]});
    const u32 idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    m.indices.insert(m.indices.end(), idx, idx + 6);
}

} // namespace

MeshData makeCube(f32 size) {
    MeshData m;
    const f32 h = size * 0.5f;
    addQuad(m, {1, 0, 0},  {0, 0, -1}, {0, 1, 0}, h);   // +X
    addQuad(m, {-1, 0, 0}, {0, 0, 1},  {0, 1, 0}, h);   // -X
    addQuad(m, {0, 1, 0},  {0, 0, 1},  {1, 0, 0}, h);   // +Y
    addQuad(m, {0, -1, 0}, {1, 0, 0},  {0, 0, 1}, h);   // -Y
    addQuad(m, {0, 0, 1},  {1, 0, 0},  {0, 1, 0}, h);   // +Z
    addQuad(m, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, h);   // -Z
    return m;
}

MeshData makePlane(f32 size) {
    MeshData m;
    addQuad(m, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, size * 0.5f);
    return m;
}

MeshData makeSphere(u32 rings, u32 sectors, f32 radius) {
    MeshData m;
    rings   = rings   < 2 ? 2 : rings;
    sectors = sectors < 3 ? 3 : sectors;
    constexpr f32 kPi = 3.14159265358979323846f;

    for (u32 i = 0; i <= rings; ++i) {
        const f32 phi = kPi * static_cast<f32>(i) / static_cast<f32>(rings);   // 0..pi
        const f32 sp = std::sin(phi), cp = std::cos(phi);
        for (u32 j = 0; j <= sectors; ++j) {
            const f32 theta = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
            const f32 st = std::sin(theta), ct = std::cos(theta);
            const Vec3 nrm{sp * ct, cp, sp * st};
            m.vertices.push_back({nrm * radius, nrm,
                                  {static_cast<f32>(j) / sectors, static_cast<f32>(i) / rings}});
        }
    }
    const u32 stride = sectors + 1;
    for (u32 i = 0; i < rings; ++i) {
        for (u32 j = 0; j < sectors; ++j) {
            const u32 k1 = i * stride + j;
            const u32 k2 = k1 + stride;
            m.indices.insert(m.indices.end(), {k1, k2, k1 + 1, k1 + 1, k2, k2 + 1});
        }
    }
    return m;
}

MeshRenderer::MeshRenderer(rhi::IGraphicsDevice& device, rhi::Format colorFormat,
                           rhi::Format depthFormat)
    : m_device(device) {

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = toBytes(mesh_vert_spv, mesh_vert_spv_size);
    pd.fragmentSpirv       = toBytes(mesh_frag_spv, mesh_frag_spv_size);
    pd.vertexLayout.stride = sizeof(MeshVertex);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
        {.location = 1, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, normal)},
        {.location = 2, .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv)},
    };
    pd.topology         = rhi::PrimitiveTopology::TriangleList;
    pd.cull             = rhi::CullMode::Back;
    pd.colorFormat      = colorFormat;
    pd.alphaBlend       = false;
    pd.hasUniformBuffer = true;
    pd.pushConstantSize = sizeof(Push);
    pd.depthTest        = true;
    pd.depthWrite       = true;
    pd.depthCompare     = rhi::CompareOp::LessEqual;
    pd.depthFormat      = depthFormat;
    pd.debugName        = "mesh_pipeline";
    m_pipeline = m_device.createGraphicsPipeline(pd);

    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_uniformBuffers[i] = m_device.createBuffer(
            {.size = sizeof(FrameUBO), .usage = rhi::BufferUsage::Uniform,
             .domain = rhi::MemoryDomain::Upload, .debugName = "mesh_frame_ubo"});
        m_uniformBindGroups[i] = m_device.createBindGroup(
            {.uniformBuffer = m_uniformBuffers[i], .uniformSize = sizeof(FrameUBO)});
    }
}

MeshRenderer::~MeshRenderer() {
    m_device.waitIdle();
    for (GpuMesh& gm : m_meshes) {
        if (!gm.alive) continue;
        m_device.destroyBuffer(gm.vbo);
        m_device.destroyBuffer(gm.ibo);
    }
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_device.destroyBindGroup(m_uniformBindGroups[i]);
        m_device.destroyBuffer(m_uniformBuffers[i]);
    }
    m_device.destroyPipeline(m_pipeline);
}

MeshHandle MeshRenderer::createMesh(const MeshVertex* vertices, usize vertexCount,
                                    const u32* indices, usize indexCount) {
    GpuMesh gm;
    gm.indexCount = static_cast<u32>(indexCount);
    gm.alive      = true;
    gm.vbo = m_device.createBuffer(
        {.size = vertexCount * sizeof(MeshVertex), .usage = rhi::BufferUsage::Vertex,
         .domain = rhi::MemoryDomain::Device, .debugName = "mesh_vertices"},
        vertices);
    gm.ibo = m_device.createBuffer(
        {.size = indexCount * sizeof(u32), .usage = rhi::BufferUsage::Index,
         .domain = rhi::MemoryDomain::Device, .debugName = "mesh_indices"},
        indices);

    const u32 index = static_cast<u32>(m_meshes.size());
    m_meshes.push_back(gm);
    return {.index = index, .generation = 0};
}

void MeshRenderer::destroyMesh(MeshHandle h) {
    if (!h.valid() || h.index >= m_meshes.size()) return;
    GpuMesh& gm = m_meshes[h.index];
    if (!gm.alive) return;
    m_device.waitIdle();
    m_device.destroyBuffer(gm.vbo);
    m_device.destroyBuffer(gm.ibo);
    gm.alive = false;
}

void MeshRenderer::begin(const Camera& camera, const DirectionalLight& light) {
    m_instances.clear();
    m_drawCalls = 0;

    const Vec3 d = normalize(light.direction);
    m_frameData.viewProj   = camera.viewProjection();
    m_frameData.lightDir   = {d.x, d.y, d.z, 0.0f};
    m_frameData.lightColor = {light.color.x, light.color.y, light.color.z, light.intensity};
    m_frameData.ambient    = {light.ambient.x, light.ambient.y, light.ambient.z, 0.0f};
    m_frameData.cameraPos  = {camera.position.x, camera.position.y, camera.position.z, 0.0f};
}

void MeshRenderer::drawMesh(MeshHandle mesh, const Mat4& model, Vec4 color) {
    m_instances.push_back({.mesh = mesh, .model = model, .color = color});
}

void MeshRenderer::submit(const MeshInstance& inst) { m_instances.push_back(inst); }

void MeshRenderer::submit(const MeshInstance* items, usize count) {
    m_instances.insert(m_instances.end(), items, items + count);
}

void MeshRenderer::end(rhi::ICommandList& cmd) {
    if (m_instances.empty()) return;

    VORTEX_PROFILE_ZONE("mesh.end");
    m_device.updateBuffer(m_uniformBuffers[m_frame], &m_frameData, sizeof(FrameUBO));

    cmd.setPipeline(m_pipeline);
    cmd.setBindGroup(0, m_uniformBindGroups[m_frame]);

    for (const MeshInstance& inst : m_instances) {
        if (!inst.mesh.valid() || inst.mesh.index >= m_meshes.size()) continue;
        const GpuMesh& gm = m_meshes[inst.mesh.index];
        if (!gm.alive || gm.indexCount == 0) continue;

        const Push pc{inst.model, inst.color};
        cmd.pushConstants(&pc, sizeof(Push));
        cmd.setVertexBuffer(0, gm.vbo);
        cmd.setIndexBuffer(gm.ibo, rhi::IndexType::U32);
        cmd.drawIndexed(gm.indexCount);
        ++m_drawCalls;
    }

    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
}

}
