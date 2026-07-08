#include "vortex/renderer/mesh.hpp"

#include "vortex/core/profiler.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "mesh_vert_spv.h"
#include "mesh_frag_spv.h"
#include "shadow_vert_spv.h"
#include "shadow_frag_spv.h"

#include <cmath>
#include <cstring>

namespace vortex::renderer {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

// --- Procedural IBL environment (generated once on the CPU) ---------------
// A simple HDR sky: warm horizon, blue zenith, dark ground, plus a sun disc so
// metallic surfaces get a strong, bloom-able reflection highlight.

constexpr f32 kIblPi = 3.14159265358979323846f;

Vec3 skyColor(Vec3 d) {
    const Vec3  sunDir = normalize(Vec3{0.5f, 0.8f, 0.3f});
    const Vec3  zenith{0.35f, 0.55f, 1.0f};
    const Vec3  horizon{1.0f, 0.85f, 0.7f};
    const Vec3  ground{0.20f, 0.18f, 0.16f};

    Vec3 col;
    if (d.y >= 0.0f) {
        const f32 t = std::pow(d.y, 0.45f);
        col = horizon * (1.0f - t) + zenith * t;
        col = col * 1.6f;
    } else {
        const f32 t = std::pow(-d.y, 0.5f);
        col = horizon * (1.0f - t) + ground * t;
    }
    const f32 sun = std::pow(std::max(dot(d, sunDir), 0.0f), 600.0f);
    return col + Vec3{18.0f, 16.0f, 12.0f} * sun;
}

// OpenGL/Vulkan cube face convention. face: 0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z.
Vec3 cubeDir(u32 face, f32 s, f32 t) {   // s,t in [0,1]
    const f32 sc = 2.0f * s - 1.0f, tc = 2.0f * t - 1.0f;
    switch (face) {
        case 0:  return normalize(Vec3{ 1.0f, -tc, -sc});
        case 1:  return normalize(Vec3{-1.0f, -tc,  sc});
        case 2:  return normalize(Vec3{ sc,  1.0f,  tc});
        case 3:  return normalize(Vec3{ sc, -1.0f, -tc});
        case 4:  return normalize(Vec3{ sc, -tc,  1.0f});
        default: return normalize(Vec3{-sc, -tc, -1.0f});
    }
}

Vec3 sampleCube(const std::vector<f32>& px, u32 res, Vec3 d) {
    const f32 ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    u32 face; f32 sc, tc, ma;
    if (ax >= ay && ax >= az)      { ma = ax; if (d.x > 0) { face = 0; sc = -d.z; tc = -d.y; } else { face = 1; sc = d.z; tc = -d.y; } }
    else if (ay >= az)             { ma = ay; if (d.y > 0) { face = 2; sc = d.x;  tc = d.z;  } else { face = 3; sc = d.x; tc = -d.z; } }
    else                           { ma = az; if (d.z > 0) { face = 4; sc = d.x;  tc = -d.y; } else { face = 5; sc = -d.x; tc = -d.y; } }
    const f32 s = (sc / ma + 1.0f) * 0.5f, t = (tc / ma + 1.0f) * 0.5f;
    u32 x = static_cast<u32>(std::min(s, 0.999999f) * res);
    u32 y = static_cast<u32>(std::min(t, 0.999999f) * res);
    const usize idx = (static_cast<usize>(face) * res * res + static_cast<usize>(y) * res + x) * 4;
    return {px[idx], px[idx + 1], px[idx + 2]};
}

// 6 faces of res*res RGBA32F, laid out contiguously for a cube upload.
std::vector<f32> genEnvironment(u32 res) {
    std::vector<f32> out(static_cast<usize>(6) * res * res * 4, 0.0f);
    for (u32 f = 0; f < 6; ++f)
        for (u32 y = 0; y < res; ++y)
            for (u32 x = 0; x < res; ++x) {
                const Vec3 d = cubeDir(f, (x + 0.5f) / res, (y + 0.5f) / res);
                const Vec3 c = skyColor(d);
                const usize i = (static_cast<usize>(f) * res * res + static_cast<usize>(y) * res + x) * 4;
                out[i] = c.x; out[i + 1] = c.y; out[i + 2] = c.z; out[i + 3] = 1.0f;
            }
    return out;
}

// Cosine-weighted hemisphere convolution of the environment: the diffuse
// irradiance each surface normal receives.
std::vector<f32> genIrradiance(const std::vector<f32>& env, u32 envRes, u32 outRes) {
    std::vector<f32> out(static_cast<usize>(6) * outRes * outRes * 4, 0.0f);
    constexpr u32 kPhi = 32, kTheta = 12;
    for (u32 f = 0; f < 6; ++f)
        for (u32 y = 0; y < outRes; ++y)
            for (u32 x = 0; x < outRes; ++x) {
                const Vec3 N = cubeDir(f, (x + 0.5f) / outRes, (y + 0.5f) / outRes);
                Vec3 up = std::fabs(N.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
                const Vec3 right = normalize(cross(up, N));
                up = cross(N, right);

                Vec3 sum{0, 0, 0};
                f32 wsum = 0.0f;
                for (u32 p = 0; p < kPhi; ++p)
                    for (u32 tt = 0; tt < kTheta; ++tt) {
                        const f32 phi   = 2.0f * kIblPi * (p + 0.5f) / kPhi;
                        const f32 theta = 0.5f * kIblPi * (tt + 0.5f) / kTheta;
                        const f32 st = std::sin(theta), ct = std::cos(theta);
                        const Vec3 dir = right * (st * std::cos(phi)) +
                                         up    * (st * std::sin(phi)) + N * ct;
                        const f32 w = ct * st;   // cosine weight * solid-angle term
                        sum = sum + sampleCube(env, envRes, dir) * w;
                        wsum += w;
                    }
                const Vec3 irr = sum * (1.0f / wsum);
                const usize i = (static_cast<usize>(f) * outRes * outRes +
                                 static_cast<usize>(y) * outRes + x) * 4;
                out[i] = irr.x; out[i + 1] = irr.y; out[i + 2] = irr.z; out[i + 3] = 1.0f;
            }
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
    // A flat quad centred on the origin in the XZ plane, facing +Y. (Unlike a
    // cube face it must not be pushed out along its normal.)
    MeshData m;
    const f32 h = size * 0.5f;
    const Vec3 n{0, 1, 0};
    const Vec3 p[4] = {{-h, 0, -h}, {-h, 0, h}, {h, 0, h}, {h, 0, -h}};
    const Vec2 uv[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    for (int i = 0; i < 4; ++i) m.vertices.push_back({p[i], n, uv[i]});
    m.indices = {0, 1, 2, 0, 2, 3};
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
    pd.hasMaterialTexture = true;   // set 0: the shadow map (sampled depth)
    pd.hasUniformBuffer = true;     // set 1: per-frame data
    pd.hasIblTextures   = true;     // set 2: irradiance + environment cubemaps
    pd.pushConstantSize = sizeof(Push);
    pd.depthTest        = true;
    pd.depthWrite       = true;
    pd.depthCompare     = rhi::CompareOp::LessEqual;
    pd.depthFormat      = depthFormat;
    pd.debugName        = "mesh_pipeline";
    m_pipeline = m_device.createGraphicsPipeline(pd);

    // Depth-only shadow pipeline: same vertex buffers, but only the position is
    // read and there is no colour attachment (colorFormat = Undefined).
    rhi::GraphicsPipelineDesc sd;
    sd.vertexSpirv         = toBytes(shadow_vert_spv, shadow_vert_spv_size);
    sd.fragmentSpirv       = toBytes(shadow_frag_spv, shadow_frag_spv_size);
    sd.vertexLayout.stride = sizeof(MeshVertex);
    sd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
    };
    sd.topology         = rhi::PrimitiveTopology::TriangleList;
    sd.cull             = rhi::CullMode::Back;
    sd.colorFormat      = rhi::Format::Undefined;   // depth-only
    sd.pushConstantSize = sizeof(Mat4);
    sd.depthTest        = true;
    sd.depthWrite       = true;
    sd.depthCompare     = rhi::CompareOp::LessEqual;
    sd.depthFormat      = depthFormat;
    sd.debugName        = "shadow_pipeline";
    m_shadowPipeline = m_device.createGraphicsPipeline(sd);

    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_uniformBuffers[i] = m_device.createBuffer(
            {.size = sizeof(FrameUBO), .usage = rhi::BufferUsage::Uniform,
             .domain = rhi::MemoryDomain::Upload, .debugName = "mesh_frame_ubo"});
        m_uniformBindGroups[i] = m_device.createBindGroup(
            {.uniformBuffer = m_uniformBuffers[i], .uniformSize = sizeof(FrameUBO)});
    }

    // Build the IBL cubemaps once (procedural sky + its diffuse convolution).
    constexpr u32 kEnvRes = 64, kIrrRes = 16;
    const std::vector<f32> envData = genEnvironment(kEnvRes);
    const std::vector<f32> irrData = genIrradiance(envData, kEnvRes, kIrrRes);
    m_envMap = m_device.createTexture(
        {.width = kEnvRes, .height = kEnvRes, .format = rhi::Format::R32G32B32A32_SFLOAT,
         .cube = true, .debugName = "ibl_env"}, envData.data());
    m_irradiance = m_device.createTexture(
        {.width = kIrrRes, .height = kIrrRes, .format = rhi::Format::R32G32B32A32_SFLOAT,
         .cube = true, .debugName = "ibl_irradiance"}, irrData.data());
    m_iblSampler = m_device.createSampler({.minFilter = rhi::Filter::Linear,
                                           .magFilter = rhi::Filter::Linear,
                                           .addressU  = rhi::AddressMode::ClampToEdge,
                                           .addressV  = rhi::AddressMode::ClampToEdge});
    m_iblBindGroup = m_device.createBindGroup({.isIblSet = true,
                                               .irradiance = m_irradiance,
                                               .envMap = m_envMap,
                                               .iblSampler = m_iblSampler});
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
    m_device.destroyBindGroup(m_iblBindGroup);
    m_device.destroySampler(m_iblSampler);
    m_device.destroyTexture(m_irradiance);
    m_device.destroyTexture(m_envMap);
    m_device.destroyPipeline(m_shadowPipeline);
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

    // Orthographic shadow camera looking along the light direction at the target.
    const Vec3 eye = light.shadowTarget - d * light.shadowDistance;
    const Vec3 up  = std::fabs(d.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
    const f32  e   = light.shadowExtent;
    const Mat4 lightView = Mat4::lookAt(eye, light.shadowTarget, up);
    const Mat4 lightProj = Mat4::ortho(-e, e, -e, e, 0.05f, light.shadowDistance * 2.0f);

    m_frameData.viewProj      = camera.viewProjection();
    m_frameData.lightViewProj = lightProj * lightView;
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

void MeshRenderer::renderShadow(rhi::ICommandList& cmd) {
    if (m_instances.empty()) return;

    VORTEX_PROFILE_ZONE("mesh.shadow");
    cmd.setPipeline(m_shadowPipeline);
    for (const MeshInstance& inst : m_instances) {
        if (!inst.mesh.valid() || inst.mesh.index >= m_meshes.size()) continue;
        const GpuMesh& gm = m_meshes[inst.mesh.index];
        if (!gm.alive || gm.indexCount == 0) continue;

        const Mat4 lightMvp = m_frameData.lightViewProj * inst.model;
        cmd.pushConstants(&lightMvp, sizeof(Mat4));
        cmd.setVertexBuffer(0, gm.vbo);
        cmd.setIndexBuffer(gm.ibo, rhi::IndexType::U32);
        cmd.drawIndexed(gm.indexCount);
    }
}

void MeshRenderer::end(rhi::ICommandList& cmd, rhi::BindGroupHandle shadowMap) {
    if (m_instances.empty()) return;

    VORTEX_PROFILE_ZONE("mesh.end");
    m_device.updateBuffer(m_uniformBuffers[m_frame], &m_frameData, sizeof(FrameUBO));

    cmd.setPipeline(m_pipeline);
    if (shadowMap.valid()) cmd.setBindGroup(0, shadowMap);   // set 0: shadow map
    cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);        // set 1: frame data
    cmd.setBindGroup(2, m_iblBindGroup);                      // set 2: IBL cubemaps

    for (const MeshInstance& inst : m_instances) {
        if (!inst.mesh.valid() || inst.mesh.index >= m_meshes.size()) continue;
        const GpuMesh& gm = m_meshes[inst.mesh.index];
        if (!gm.alive || gm.indexCount == 0) continue;

        const Push pc{inst.model, inst.color, {inst.metallic, inst.roughness, 0.0f, 0.0f}};
        cmd.pushConstants(&pc, sizeof(Push));
        cmd.setVertexBuffer(0, gm.vbo);
        cmd.setIndexBuffer(gm.ibo, rhi::IndexType::U32);
        cmd.drawIndexed(gm.indexCount);
        ++m_drawCalls;
    }

    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
}

}
