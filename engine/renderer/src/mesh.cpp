#include "vortex/renderer/mesh.hpp"

#include "vortex/core/assert.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "mesh_vert_spv.h"
#include "mesh_frag_spv.h"
#include "shadow_vert_spv.h"
#include "shadow_frag_spv.h"
#include "skybox_vert_spv.h"
#include "skybox_frag_spv.h"
#include "gbuffer_frag_spv.h"
#include "deferred_frag_spv.h"
#include "ssao_frag_spv.h"
#include "ssao_blur_frag_spv.h"
#include "volumetric_frag_spv.h"
#include "blit_frag_spv.h"
#include "decal_vert_spv.h"
#include "decal_frag_spv.h"
#include "fullscreen_vert_spv.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace vortex::renderer {

namespace {

constexpr f32 kPi = 3.14159265358979323846f;

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

// --- Procedural IBL environment (generated once on the CPU) ---------------
// A simple HDR sky: warm horizon, blue zenith, dark ground, plus a sun disc so
// metallic surfaces get a strong, bloom-able reflection highlight.

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
                        const f32 phi   = 2.0f * kPi * (p + 0.5f) / kPhi;
                        const f32 theta = 0.5f * kPi * (tt + 0.5f) / kTheta;
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

// --- Primitive helpers -----------------------------------------------------

void addQuad(MeshData& m, Vec3 n, Vec3 u, Vec3 v, f32 h) {
    const Vec3 c = n * h;
    const Vec3 p[4] = {c - u * h - v * h, c + u * h - v * h,
                       c + u * h + v * h, c - u * h + v * h};
    const Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const u32 base = static_cast<u32>(m.vertices.size());
    for (int i = 0; i < 4; ++i) m.vertices.push_back({.position = p[i], .normal = n, .uv = uv[i]});
    const u32 idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    m.indices.insert(m.indices.end(), idx, idx + 6);
}

// A ring of vertices around the Y axis at height `y`, used by the round primitives.
void addRing(MeshData& m, f32 y, f32 radius, Vec3 normal, u32 sectors, f32 vCoord,
             bool radialNormal) {
    for (u32 j = 0; j <= sectors; ++j) {
        const f32 theta = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
        const f32 ct = std::cos(theta), st = std::sin(theta);
        const Vec3 pos{radius * ct, y, radius * st};
        const Vec3 nrm = radialNormal ? normalize(Vec3{ct, normal.y, st}) : normal;
        m.vertices.push_back({.position = pos, .normal = nrm,
                              .uv = {static_cast<f32>(j) / sectors, vCoord}});
    }
}

// Stitch two consecutive rings of (sectors + 1) vertices into a quad strip.
// Stitch two consecutive rings into a quad strip, wound to face OUTWARD — the same
// way round as the cube's faces, so back-face culling keeps the surface you can see.
// The reverse of this ordering renders the far side of the mesh instead, which is not
// obviously wrong to look at (a sphere is still a disc) but points every normal away
// from the camera. That is what the sphere did until July 2026.
void bridgeRings(MeshData& m, u32 lower, u32 upper, u32 sectors) {
    for (u32 j = 0; j < sectors; ++j) {
        const u32 a = lower + j, b = upper + j;
        m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
    }
}

// A flat disc facing `dir` (+1 up, -1 down), fanned from a centre vertex.
void addDisc(MeshData& m, f32 y, f32 radius, f32 dir, u32 sectors) {
    const Vec3 n{0.0f, dir, 0.0f};
    const u32 centre = static_cast<u32>(m.vertices.size());
    m.vertices.push_back({.position = {0.0f, y, 0.0f}, .normal = n, .uv = {0.5f, 0.5f}});
    for (u32 j = 0; j <= sectors; ++j) {
        const f32 theta = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
        const f32 ct = std::cos(theta), st = std::sin(theta);
        m.vertices.push_back({.position = {radius * ct, y, radius * st}, .normal = n,
                              .uv = {ct * 0.5f + 0.5f, st * 0.5f + 0.5f}});
    }
    for (u32 j = 0; j < sectors; ++j) {
        const u32 a = centre + 1 + j, b = a + 1;
        if (dir > 0.0f) m.indices.insert(m.indices.end(), {centre, b, a});
        else            m.indices.insert(m.indices.end(), {centre, a, b});
    }
}

} // namespace

// ---------------------------------------------------------------------------
// MeshData
// ---------------------------------------------------------------------------

void MeshData::computeTangents() {
    std::vector<Vec3> tan(vertices.size(), Vec3{0, 0, 0});
    std::vector<Vec3> bit(vertices.size(), Vec3{0, 0, 0});

    for (usize i = 0; i + 2 < indices.size(); i += 3) {
        const u32 i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const MeshVertex& v0 = vertices[i0];
        const MeshVertex& v1 = vertices[i1];
        const MeshVertex& v2 = vertices[i2];

        const Vec3 e1 = v1.position - v0.position;
        const Vec3 e2 = v2.position - v0.position;
        const f32 du1 = v1.uv.x - v0.uv.x, dv1 = v1.uv.y - v0.uv.y;
        const f32 du2 = v2.uv.x - v0.uv.x, dv2 = v2.uv.y - v0.uv.y;

        const f32 det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;   // degenerate UVs: leave the default frame
        const f32 r = 1.0f / det;

        const Vec3 t = (e1 * dv2 - e2 * dv1) * r;
        const Vec3 b = (e2 * du1 - e1 * du2) * r;
        for (const u32 idx : {i0, i1, i2}) {
            tan[idx] = tan[idx] + t;
            bit[idx] = bit[idx] + b;
        }
    }

    for (usize i = 0; i < vertices.size(); ++i) {
        const Vec3 n = vertices[i].normal;
        Vec3 t = tan[i];
        if (dot(t, t) < 1e-12f) {
            // No usable UV gradient here — any tangent perpendicular to N will do.
            t = std::fabs(n.y) > 0.99f ? Vec3{1, 0, 0} : normalize(cross(Vec3{0, 1, 0}, n));
        } else {
            t = normalize(t - n * dot(n, t));   // Gram-Schmidt against the normal
        }
        const f32 handedness = dot(cross(n, t), bit[i]) < 0.0f ? -1.0f : 1.0f;
        vertices[i].tangent = {t.x, t.y, t.z, handedness};
    }
}

void MeshData::setColor(Vec4 c) {
    for (MeshVertex& v : vertices) v.color = c;
}

void MeshData::copyUVToLightmapUV() {
    for (MeshVertex& v : vertices) v.uv1 = v.uv;
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

MeshData makeCube(f32 size) {
    MeshData m;
    const f32 h = size * 0.5f;
    addQuad(m, {1, 0, 0},  {0, 0, -1}, {0, 1, 0}, h);   // +X
    addQuad(m, {-1, 0, 0}, {0, 0, 1},  {0, 1, 0}, h);   // -X
    addQuad(m, {0, 1, 0},  {0, 0, 1},  {1, 0, 0}, h);   // +Y
    addQuad(m, {0, -1, 0}, {1, 0, 0},  {0, 0, 1}, h);   // -Y
    addQuad(m, {0, 0, 1},  {1, 0, 0},  {0, 1, 0}, h);   // +Z
    addQuad(m, {0, 0, -1}, {-1, 0, 0}, {0, 1, 0}, h);   // -Z
    m.computeTangents();
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
    for (int i = 0; i < 4; ++i) m.vertices.push_back({.position = p[i], .normal = n, .uv = uv[i]});
    m.indices = {0, 1, 2, 0, 2, 3};
    m.computeTangents();
    m.copyUVToLightmapUV();   // a plane's UVs already cover it exactly once
    return m;
}

MeshData makeQuad(f32 width, f32 height) {
    // Upright in the XY plane, facing +Z: the shape a decal, billboard or sprite wants.
    MeshData m;
    const f32 hw = width * 0.5f, hh = height * 0.5f;
    const Vec3 n{0, 0, 1};
    const Vec3 p[4] = {{-hw, -hh, 0}, {hw, -hh, 0}, {hw, hh, 0}, {-hw, hh, 0}};
    const Vec2 uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int i = 0; i < 4; ++i) m.vertices.push_back({.position = p[i], .normal = n, .uv = uv[i]});
    m.indices = {0, 1, 2, 0, 2, 3};
    m.computeTangents();
    m.copyUVToLightmapUV();
    return m;
}

MeshData makeSphere(u32 rings, u32 sectors, f32 radius) {
    MeshData m;
    rings   = rings   < 2 ? 2 : rings;
    sectors = sectors < 3 ? 3 : sectors;

    for (u32 i = 0; i <= rings; ++i) {
        const f32 phi = kPi * static_cast<f32>(i) / static_cast<f32>(rings);   // 0..pi
        const f32 sp = std::sin(phi), cp = std::cos(phi);
        for (u32 j = 0; j <= sectors; ++j) {
            const f32 theta = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
            const f32 st = std::sin(theta), ct = std::cos(theta);
            const Vec3 nrm{sp * ct, cp, sp * st};
            m.vertices.push_back({.position = nrm * radius, .normal = nrm,
                                  .uv = {static_cast<f32>(j) / sectors,
                                         static_cast<f32>(i) / rings}});
        }
    }
    const u32 stride = sectors + 1;
    for (u32 i = 0; i < rings; ++i)
        bridgeRings(m, i * stride, (i + 1) * stride, sectors);

    m.computeTangents();
    return m;
}

MeshData makeCylinder(f32 radius, f32 height, u32 sectors) {
    MeshData m;
    sectors = sectors < 3 ? 3 : sectors;
    const f32 h = height * 0.5f;

    // Side: two rings with purely radial normals, so the caps stay sharp. The TOP ring
    // goes first, because bridgeRings winds a strip on the assumption that it walks
    // from the first ring toward the second the way the sphere does — pole downwards.
    // Hand it the rings the other way up and the wall comes out inside-out.
    const u32 top = static_cast<u32>(m.vertices.size());
    addRing(m, h, radius, {0, 0, 0}, sectors, 0.0f, /*radialNormal=*/true);
    const u32 bottom = static_cast<u32>(m.vertices.size());
    addRing(m, -h, radius, {0, 0, 0}, sectors, 1.0f, /*radialNormal=*/true);
    bridgeRings(m, top, bottom, sectors);

    addDisc(m, h, radius, 1.0f, sectors);
    addDisc(m, -h, radius, -1.0f, sectors);

    m.computeTangents();
    return m;
}

MeshData makeCone(f32 radius, f32 height, u32 sectors) {
    MeshData m;
    sectors = sectors < 3 ? 3 : sectors;
    const f32 h = height * 0.5f;

    // The apex is duplicated per sector so each side face gets its own normal —
    // one shared apex vertex would average them into a dimple.
    const f32 slope = radius / std::max(height, 1e-6f);
    for (u32 j = 0; j < sectors; ++j) {
        const f32 t0 = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
        const f32 t1 = 2.0f * kPi * static_cast<f32>(j + 1) / static_cast<f32>(sectors);
        const f32 tm = (t0 + t1) * 0.5f;

        auto sideNormal = [&](f32 theta) {
            return normalize(Vec3{std::cos(theta), slope, std::sin(theta)});
        };

        const u32 base = static_cast<u32>(m.vertices.size());
        m.vertices.push_back({.position = {0.0f, h, 0.0f}, .normal = sideNormal(tm),
                              .uv = {(static_cast<f32>(j) + 0.5f) / sectors, 0.0f}});
        m.vertices.push_back({.position = {radius * std::cos(t0), -h, radius * std::sin(t0)},
                              .normal = sideNormal(t0),
                              .uv = {static_cast<f32>(j) / sectors, 1.0f}});
        m.vertices.push_back({.position = {radius * std::cos(t1), -h, radius * std::sin(t1)},
                              .normal = sideNormal(t1),
                              .uv = {static_cast<f32>(j + 1) / sectors, 1.0f}});
        // apex, then the rim backwards: that is the order whose cross product points
        // out of the cone rather than into it.
        m.indices.insert(m.indices.end(), {base, base + 2, base + 1});
    }

    addDisc(m, -h, radius, -1.0f, sectors);

    m.computeTangents();
    return m;
}

MeshData makeTorus(f32 radius, f32 tubeRadius, u32 rings, u32 sides) {
    MeshData m;
    rings = rings < 3 ? 3 : rings;
    sides = sides < 3 ? 3 : sides;

    for (u32 i = 0; i <= rings; ++i) {
        const f32 u  = 2.0f * kPi * static_cast<f32>(i) / static_cast<f32>(rings);
        const f32 cu = std::cos(u), su = std::sin(u);
        for (u32 j = 0; j <= sides; ++j) {
            const f32 v  = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sides);
            const f32 cv = std::cos(v), sv = std::sin(v);

            const Vec3 nrm{cu * cv, sv, su * cv};
            const Vec3 pos{(radius + tubeRadius * cv) * cu, tubeRadius * sv,
                           (radius + tubeRadius * cv) * su};
            m.vertices.push_back({.position = pos, .normal = nrm,
                                  .uv = {static_cast<f32>(i) / rings,
                                         static_cast<f32>(j) / sides}});
        }
    }
    const u32 stride = sides + 1;
    for (u32 i = 0; i < rings; ++i)
        bridgeRings(m, i * stride, (i + 1) * stride, sides);

    m.computeTangents();
    return m;
}

MeshData makeCapsule(f32 radius, f32 height, u32 rings, u32 sectors) {
    // A cylinder of `height` with a hemisphere capping each end. `height` is the
    // straight section only, so total length is height + 2 * radius.
    MeshData m;
    rings   = rings   < 2 ? 2 : rings;
    sectors = sectors < 3 ? 3 : sectors;
    const f32 h = height * 0.5f;
    const u32 stride = sectors + 1;

    std::vector<u32> ringStarts;

    // Top hemisphere: phi 0 (pole) -> pi/2 (equator).
    for (u32 i = 0; i <= rings; ++i) {
        const f32 phi = 0.5f * kPi * static_cast<f32>(i) / static_cast<f32>(rings);
        const f32 sp = std::sin(phi), cp = std::cos(phi);
        ringStarts.push_back(static_cast<u32>(m.vertices.size()));
        for (u32 j = 0; j <= sectors; ++j) {
            const f32 theta = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
            const Vec3 nrm{sp * std::cos(theta), cp, sp * std::sin(theta)};
            m.vertices.push_back({.position = nrm * radius + Vec3{0, h, 0}, .normal = nrm,
                                  .uv = {static_cast<f32>(j) / sectors,
                                         static_cast<f32>(i) / (2 * rings + 2)}});
        }
    }
    // Bottom hemisphere: phi pi/2 (equator) -> pi (pole).
    for (u32 i = 0; i <= rings; ++i) {
        const f32 phi = 0.5f * kPi * (1.0f + static_cast<f32>(i) / static_cast<f32>(rings));
        const f32 sp = std::sin(phi), cp = std::cos(phi);
        ringStarts.push_back(static_cast<u32>(m.vertices.size()));
        for (u32 j = 0; j <= sectors; ++j) {
            const f32 theta = 2.0f * kPi * static_cast<f32>(j) / static_cast<f32>(sectors);
            const Vec3 nrm{sp * std::cos(theta), cp, sp * std::sin(theta)};
            m.vertices.push_back({.position = nrm * radius + Vec3{0, -h, 0}, .normal = nrm,
                                  .uv = {static_cast<f32>(j) / sectors,
                                         static_cast<f32>(rings + 2 + i) / (2 * rings + 2)}});
        }
    }

    // Bridge every consecutive ring, including across the straight middle section
    // (the last top ring to the first bottom ring), which is the cylinder wall.
    for (usize i = 0; i + 1 < ringStarts.size(); ++i)
        bridgeRings(m, ringStarts[i], ringStarts[i + 1], sectors);

    (void)stride;
    m.computeTangents();
    return m;
}

// ---------------------------------------------------------------------------
// MeshRenderer
// ---------------------------------------------------------------------------

MeshRenderer::MeshRenderer(rhi::IGraphicsDevice& device, rhi::Format colorFormat,
                           rhi::Format depthFormat)
    : m_device(device), m_colorFormat(colorFormat), m_depthFormat(depthFormat) {

    // Depth-only shadow pipeline: same vertex buffers, but only the position is
    // read and there is no colour attachment (colorFormat = Undefined).
    rhi::GraphicsPipelineDesc sd;
    sd.vertexSpirv         = toBytes(shadow_vert_spv, shadow_vert_spv_size);
    sd.fragmentSpirv       = toBytes(shadow_frag_spv, shadow_frag_spv_size);
    sd.vertexWgsl          = shadow_vert_spv_wgsl;
    sd.fragmentWgsl        = shadow_frag_spv_wgsl;
    sd.vertexLayout.stride = sizeof(MeshVertex);
    sd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
        {.location = 2, .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv)},
        {.location = 6, .format = rhi::VertexFormat::UInt4x8, .offset = offsetof(MeshVertex, joints)},
        {.location = 7, .format = rhi::VertexFormat::Float4,  .offset = offsetof(MeshVertex, weights)},
    };
    sd.topology         = rhi::PrimitiveTopology::TriangleList;
    sd.cull             = rhi::CullMode::Back;
    sd.colorFormat      = rhi::Format::Undefined;   // depth-only
    sd.pushConstantSize  = sizeof(ShadowPush);
    sd.hasPbrMaterial    = true;   // set 0: the albedo, for the cutout alpha test
    sd.hasUniformBuffer  = true;   // set 1: frame data
    sd.hasInstanceBuffer = true;   // ... with the instances and bones hanging off it
    sd.depthTest        = true;
    sd.depthWrite       = true;
    sd.depthCompare     = rhi::CompareOp::LessEqual;
    sd.depthFormat      = depthFormat;
    sd.debugName        = "shadow_pipeline";
    m_shadowPipeline = m_device.createGraphicsPipeline(sd);

    // Skybox: a fullscreen triangle at the far plane, sampling the environment cube.
    // Depth-tests (so geometry wins) but never writes.
    rhi::GraphicsPipelineDesc kd;
    kd.vertexSpirv       = toBytes(skybox_vert_spv, skybox_vert_spv_size);
    kd.fragmentSpirv     = toBytes(skybox_frag_spv, skybox_frag_spv_size);
    kd.vertexWgsl        = skybox_vert_spv_wgsl;
    kd.fragmentWgsl      = skybox_frag_spv_wgsl;
    kd.topology          = rhi::PrimitiveTopology::TriangleList;
    kd.cull              = rhi::CullMode::None;
    kd.colorFormat       = colorFormat;
    kd.hasUniformBuffer  = true;    // set 0
    kd.hasSceneTextures  = true;    // set 1
    kd.depthTest         = true;
    kd.depthWrite        = false;
    kd.depthCompare      = rhi::CompareOp::LessEqual;
    kd.depthFormat       = depthFormat;
    kd.debugName         = "skybox_pipeline";
    m_skyboxPipeline = m_device.createGraphicsPipeline(kd);

    // G-buffer fill: the forward vertex shader, but a fragment stage that writes the
    // surface into three targets instead of lighting it.
    rhi::GraphicsPipelineDesc gd;
    gd.vertexSpirv         = toBytes(mesh_vert_spv, mesh_vert_spv_size);
    gd.fragmentSpirv       = toBytes(gbuffer_frag_spv, gbuffer_frag_spv_size);
    gd.vertexWgsl          = mesh_vert_spv_wgsl;
    gd.fragmentWgsl        = gbuffer_frag_spv_wgsl;
    gd.vertexLayout.stride = sizeof(MeshVertex);
    gd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
        {.location = 1, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, normal)},
        {.location = 2, .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv)},
        {.location = 3, .format = rhi::VertexFormat::Float4, .offset = offsetof(MeshVertex, tangent)},
        {.location = 4, .format = rhi::VertexFormat::Float4, .offset = offsetof(MeshVertex, color)},
        {.location = 5, .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv1)},
        {.location = 6, .format = rhi::VertexFormat::UInt4x8, .offset = offsetof(MeshVertex, joints)},
        {.location = 7, .format = rhi::VertexFormat::Float4,  .offset = offsetof(MeshVertex, weights)},
    };
    gd.topology           = rhi::PrimitiveTopology::TriangleList;
    gd.cull               = rhi::CullMode::Back;
    gd.colorFormat        = kGBufferAlbedoFormat;
    gd.extraColorFormats[0] = kGBufferNormalFormat;
    gd.extraColorFormats[1] = kGBufferEmissiveFormat;
    gd.extraColorFormats[2] = kGBufferVelocityFormat;
    gd.hasPbrMaterial     = true;   // set 0
    gd.hasUniformBuffer   = true;   // set 1 (the vertex stage needs viewProj)
    gd.hasInstanceBuffer  = true;   // set 1, binding 1: previous model matrices
    gd.pushConstantSize   = sizeof(Push);
    gd.depthTest          = true;
    gd.depthWrite         = true;
    gd.depthCompare       = rhi::CompareOp::LessEqual;
    gd.depthFormat        = depthFormat;
    gd.debugName          = "gbuffer_pipeline";
    m_gbufferPipeline = m_device.createGraphicsPipeline(gd);

    // Deferred lighting: fullscreen, reads the G-buffer through the material set.
    rhi::GraphicsPipelineDesc dd;
    dd.vertexSpirv       = toBytes(fullscreen_vert_spv, fullscreen_vert_spv_size);
    dd.fragmentSpirv     = toBytes(deferred_frag_spv, deferred_frag_spv_size);
    dd.vertexWgsl        = fullscreen_vert_spv_wgsl;
    dd.fragmentWgsl      = deferred_frag_spv_wgsl;
    dd.topology          = rhi::PrimitiveTopology::TriangleList;
    dd.cull              = rhi::CullMode::None;
    dd.colorFormat       = colorFormat;
    dd.hasPbrMaterial    = true;    // set 0: the G-buffer
    dd.hasUniformBuffer  = true;    // set 1: frame data
    dd.hasSceneTextures  = true;    // set 2: IBL + shadow map
    dd.pushConstantSize  = sizeof(DeferredPush);
    dd.debugName         = "deferred_lighting";
    m_deferredPipeline = m_device.createGraphicsPipeline(dd);

    // SSAO: fullscreen, reads depth + normals out of the same G-buffer set.
    rhi::GraphicsPipelineDesc ad;
    ad.vertexSpirv      = toBytes(fullscreen_vert_spv, fullscreen_vert_spv_size);
    ad.fragmentSpirv    = toBytes(ssao_frag_spv, ssao_frag_spv_size);
    ad.vertexWgsl       = fullscreen_vert_spv_wgsl;
    ad.fragmentWgsl     = ssao_frag_spv_wgsl;
    ad.topology         = rhi::PrimitiveTopology::TriangleList;
    ad.cull             = rhi::CullMode::None;
    ad.colorFormat      = kSsaoFormat;
    ad.hasPbrMaterial   = true;    // set 0: the G-buffer
    ad.hasUniformBuffer = true;    // set 1: frame data (viewProj, camera)
    ad.pushConstantSize = sizeof(SsaoPush);
    ad.debugName        = "ssao";
    m_ssaoPipeline = m_device.createGraphicsPipeline(ad);

    // AO blur: an ordinary one-texture fullscreen pass.
    rhi::GraphicsPipelineDesc bd;
    bd.vertexSpirv         = toBytes(fullscreen_vert_spv, fullscreen_vert_spv_size);
    bd.fragmentSpirv       = toBytes(ssao_blur_frag_spv, ssao_blur_frag_spv_size);
    bd.vertexWgsl          = fullscreen_vert_spv_wgsl;
    bd.fragmentWgsl        = ssao_blur_frag_spv_wgsl;
    bd.topology            = rhi::PrimitiveTopology::TriangleList;
    bd.cull                = rhi::CullMode::None;
    bd.colorFormat         = kSsaoFormat;
    bd.hasMaterialTexture  = true;   // set 0: the raw AO
    bd.pushConstantSize    = sizeof(BlurPush);
    bd.debugName           = "ssao_blur";
    m_ssaoBlurPipeline = m_device.createGraphicsPipeline(bd);

    // Volumetric fog: fullscreen, marches the shadow map along each view ray, and adds
    // what it finds to the lit image.
    rhi::GraphicsPipelineDesc vd;
    vd.vertexSpirv      = toBytes(fullscreen_vert_spv, fullscreen_vert_spv_size);
    vd.fragmentSpirv    = toBytes(volumetric_frag_spv, volumetric_frag_spv_size);
    vd.vertexWgsl       = fullscreen_vert_spv_wgsl;
    vd.fragmentWgsl     = volumetric_frag_spv_wgsl;
    vd.topology         = rhi::PrimitiveTopology::TriangleList;
    vd.cull             = rhi::CullMode::None;
    vd.colorFormat      = colorFormat;
    vd.blendMode        = rhi::BlendMode::Additive;
    vd.hasPbrMaterial   = true;    // set 0: the G-buffer (depth)
    vd.hasUniformBuffer = true;    // set 1: frame data
    vd.hasSceneTextures = true;    // set 2: the shadow map
    vd.pushConstantSize = sizeof(VolumePush);
    vd.debugName        = "volumetric";
    m_volumetricPipeline = m_device.createGraphicsPipeline(vd);

    rhi::GraphicsPipelineDesc cd;
    cd.vertexSpirv        = toBytes(fullscreen_vert_spv, fullscreen_vert_spv_size);
    cd.fragmentSpirv      = toBytes(blit_frag_spv, blit_frag_spv_size);
    cd.vertexWgsl         = fullscreen_vert_spv_wgsl;
    cd.fragmentWgsl       = blit_frag_spv_wgsl;
    cd.topology           = rhi::PrimitiveTopology::TriangleList;
    cd.cull               = rhi::CullMode::None;
    cd.colorFormat        = colorFormat;
    cd.hasMaterialTexture = true;   // set 0: the source texture
    cd.debugName          = "scene_copy";
    m_blitPipeline = m_device.createGraphicsPipeline(cd);

    // Decals: draw the volume's BACK faces with no depth test at all. Back faces, so the
    // box still covers the screen when the camera is inside it; no depth test, because the
    // fragment stage decides what is inside the volume from the depth buffer itself, and a
    // depth test here would only reject the very pixels it needs to look at.
    rhi::GraphicsPipelineDesc dcl;
    dcl.vertexSpirv         = toBytes(decal_vert_spv, decal_vert_spv_size);
    dcl.fragmentSpirv       = toBytes(decal_frag_spv, decal_frag_spv_size);
    dcl.vertexWgsl          = decal_vert_spv_wgsl;
    dcl.fragmentWgsl        = decal_frag_spv_wgsl;
    dcl.vertexLayout.stride = sizeof(MeshVertex);
    dcl.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
    };
    dcl.topology           = rhi::PrimitiveTopology::TriangleList;
    dcl.cull               = rhi::CullMode::Front;
    dcl.colorFormat        = kGBufferAlbedoFormat;
    dcl.blendMode          = rhi::BlendMode::Alpha;
    // The albedo target's alpha is the METALLIC channel. Blending into it would drag
    // metallic toward the decal's coverage, which means nothing — so leave alpha alone.
    dcl.writeAlpha         = false;
    dcl.hasMaterialTexture = true;   // set 0: the decal's texture
    dcl.hasUniformBuffer   = true;   // set 1: frame data (viewProj, invViewProj)
    dcl.hasSceneTextures   = true;   // set 2: the G-buffer's depth
    dcl.pushConstantSize   = sizeof(DecalPush);
    dcl.depthTest          = false;
    dcl.depthWrite         = false;
    dcl.debugName          = "decal";
    m_decalPipeline = m_device.createGraphicsPipeline(dcl);

    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_uniformBuffers[i] = m_device.createBuffer(
            {.size = sizeof(FrameUBO), .usage = rhi::BufferUsage::Uniform,
             .domain = rhi::MemoryDomain::Upload, .debugName = "mesh_frame_ubo"});
        // The bind group is created in ensureInstanceCapacity(), because it has to name
        // the instance buffer, which does not exist until the first frame's instance
        // count is known.

        m_skyBuffers[i] = m_device.createBuffer(
            {.size = sizeof(SkyUBO), .usage = rhi::BufferUsage::Uniform,
             .domain = rhi::MemoryDomain::Upload, .debugName = "skybox_ubo"});
        m_skyBindGroups[i] = m_device.createBindGroup(
            {.uniformBuffer = m_skyBuffers[i], .uniformSize = sizeof(SkyUBO)});
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

    m_shadowSampler = m_device.createSampler({.minFilter = rhi::Filter::Nearest,
                                              .magFilter = rhi::Filter::Nearest,
                                              .addressU  = rhi::AddressMode::ClampToEdge,
                                              .addressV  = rhi::AddressMode::ClampToEdge});

    // The neutral maps a material falls back on, and the 1x1 white depth stand-in
    // that reads as "farther than everything", i.e. nothing is in shadow.
    m_white      = defaultTexture({1.0f, 1.0f, 1.0f, 1.0f});
    m_black      = defaultTexture({0.0f, 0.0f, 0.0f, 1.0f});
    m_flatNormal = defaultTexture({0.5f, 0.5f, 1.0f, 1.0f});   // +Z in tangent space
    m_dummyShadow = defaultTexture({1.0f, 1.0f, 1.0f, 1.0f});

    m_defaultSampler = m_device.createSampler({.minFilter = rhi::Filter::Linear,
                                               .magFilter = rhi::Filter::Linear,
                                               .addressU  = rhi::AddressMode::Repeat,
                                               .addressV  = rhi::AddressMode::Repeat});

    m_shadowMap  = m_dummyShadow;
    m_sceneColor = m_black;   // nothing behind a transmissive surface until told otherwise
    m_sceneDepth = m_dummyShadow;
    rebuildSceneBindGroup();

    m_defaultMaterial = createMaterial(MaterialDesc{});
    m_unitCube        = createMesh(makeCube(1.0f));
    ensureInstanceCapacity(256, 256);
}

void MeshRenderer::ensureInstanceCapacity(u32 instances, u32 bones) {
    if (instances <= m_instanceCapacity && bones <= m_boneCapacity) return;

    // Grow by doubling, so a scene that creeps upward does not reallocate every frame.
    u32 icap = m_instanceCapacity > 0 ? m_instanceCapacity : 256;
    while (icap < instances) icap *= 2;
    u32 bcap = m_boneCapacity > 0 ? m_boneCapacity : 256;
    while (bcap < bones) bcap *= 2;

    m_device.waitIdle();   // the old buffers may still be in flight
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        if (m_instanceBuffers[i].valid()) m_device.destroyBuffer(m_instanceBuffers[i]);
        if (m_boneBuffers[i].valid())     m_device.destroyBuffer(m_boneBuffers[i]);

        m_instanceBuffers[i] = m_device.createBuffer(
            {.size = static_cast<u64>(icap) * sizeof(GpuInstance),
             .usage = rhi::BufferUsage::Storage, .domain = rhi::MemoryDomain::Upload,
             .debugName = "mesh_instances"});
        m_boneBuffers[i] = m_device.createBuffer(
            {.size = static_cast<u64>(bcap) * sizeof(Mat4),
             .usage = rhi::BufferUsage::Storage, .domain = rhi::MemoryDomain::Upload,
             .debugName = "mesh_bones"});
    }
    m_instanceCapacity = icap;
    m_boneCapacity     = bcap;
    rebuildFrameBindGroups();
}

// The frame set names four buffers, and any of them can be replaced when it grows. One place
// rebuilds the groups, so a buffer that moves cannot leave a bind group pointing at the old one.
void MeshRenderer::rebuildFrameBindGroups() {
    m_device.waitIdle();
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        if (m_uniformBindGroups[i].valid()) m_device.destroyBindGroup(m_uniformBindGroups[i]);
        m_uniformBindGroups[i] = m_device.createBindGroup(
            {.uniformBuffer = m_uniformBuffers[i], .uniformSize = sizeof(FrameUBO),
             .storageBuffer = m_instanceBuffers[i],
             .storageSize   = static_cast<u64>(m_instanceCapacity) * sizeof(GpuInstance),
             .boneBuffer    = m_boneBuffers[i],
             .boneSize      = static_cast<u64>(m_boneCapacity) * sizeof(Mat4),
             .morphBuffer   = m_morphBuffer,
             .morphSize     = static_cast<u64>(m_morphCapacity) * sizeof(GpuMorphDelta)});
    }
}

MeshRenderer::~MeshRenderer() {
    m_device.waitIdle();
    for (GpuMesh& gm : m_meshes) {
        if (!gm.alive) continue;
        m_device.destroyBuffer(gm.vbo);
        m_device.destroyBuffer(gm.ibo);
    }
    for (GpuMaterial& mat : m_materials) {
        if (!mat.alive) continue;
        m_device.destroyBindGroup(mat.bindGroup);
        if (mat.ownedSampler.valid()) m_device.destroySampler(mat.ownedSampler);
    }
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_device.destroyBindGroup(m_uniformBindGroups[i]);
        m_device.destroyBuffer(m_uniformBuffers[i]);
        if (m_instanceBuffers[i].valid()) m_device.destroyBuffer(m_instanceBuffers[i]);
        if (m_boneBuffers[i].valid())     m_device.destroyBuffer(m_boneBuffers[i]);
        m_device.destroyBindGroup(m_skyBindGroups[i]);
        m_device.destroyBuffer(m_skyBuffers[i]);
    }
    for (const SceneCache& sc : m_sceneCache) m_device.destroyBindGroup(sc.group);
    for (const GBufferCache& gc : m_gbufferCache) m_device.destroyBindGroup(gc.group);
    for (const DecalCache& dc : m_decalCache) m_device.destroyBindGroup(dc.group);

    m_device.destroySampler(m_defaultSampler);
    m_device.destroySampler(m_shadowSampler);
    m_device.destroySampler(m_iblSampler);
    m_device.destroyTexture(m_dummyShadow);
    m_device.destroyTexture(m_flatNormal);
    m_device.destroyTexture(m_black);
    m_device.destroyTexture(m_white);
    m_device.destroyTexture(m_irradiance);
    m_device.destroyTexture(m_envMap);

    for (const CachedPipeline& cp : m_pipelines) m_device.destroyPipeline(cp.pipeline);
    m_device.destroyPipeline(m_decalPipeline);
    m_device.destroyPipeline(m_blitPipeline);
    m_device.destroyPipeline(m_volumetricPipeline);
    m_device.destroyPipeline(m_ssaoBlurPipeline);
    m_device.destroyPipeline(m_ssaoPipeline);
    m_device.destroyPipeline(m_deferredPipeline);
    m_device.destroyPipeline(m_gbufferPipeline);
    m_device.destroyPipeline(m_skyboxPipeline);
    m_device.destroyPipeline(m_shadowPipeline);
}

rhi::TextureHandle MeshRenderer::defaultTexture(Vec4 rgba) {
    // UNORM, not SRGB: these stand in for data as often as for colour (normals,
    // roughness), and an sRGB decode would bend the values out from under the shader.
    const u8 px[4] = {
        static_cast<u8>(std::clamp(rgba.x, 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<u8>(std::clamp(rgba.y, 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<u8>(std::clamp(rgba.z, 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<u8>(std::clamp(rgba.w, 0.0f, 1.0f) * 255.0f + 0.5f),
    };
    return m_device.createTexture({.width = 1, .height = 1,
                                   .format = rhi::Format::R8G8B8A8_UNORM,
                                   .debugName = "mesh_default_1x1"}, px);
}

void MeshRenderer::rebuildSceneBindGroup() {
    for (const SceneCache& sc : m_sceneCache)
        if (sc.shadow == m_shadowMap && sc.sceneColor == m_sceneColor &&
            sc.sceneDepth == m_sceneDepth) {
            m_sceneBindGroup = sc.group;
            return;
        }

    const rhi::BindGroupHandle group = m_device.createBindGroup({
        .isSceneSet    = true,
        .irradiance    = m_irradiance,
        .envMap        = m_envMap,
        .iblSampler    = m_iblSampler,
        .shadowMap     = m_shadowMap,
        .shadowSampler = m_shadowSampler,
        .sceneColor    = m_sceneColor,
        .sceneDepth    = m_sceneDepth,
    });
    m_sceneCache.push_back({m_shadowMap, m_sceneColor, m_sceneDepth, group});
    m_sceneBindGroup = group;
}

void MeshRenderer::setSceneColor(rhi::TextureHandle tex) {
    const rhi::TextureHandle next = tex.valid() ? tex : m_black;
    if (next == m_sceneColor) return;
    m_sceneColor = next;
    rebuildSceneBindGroup();
}

void MeshRenderer::renderSceneCopy(rhi::ICommandList& cmd, rhi::BindGroupHandle source) {
    if (!source.valid()) return;
    VORTEX_PROFILE_ZONE("mesh.scene_copy");
    cmd.setPipeline(m_blitPipeline);
    cmd.setBindGroup(0, source);
    cmd.draw(3);
}

void MeshRenderer::setShadowMap(rhi::TextureHandle tex) {
    const rhi::TextureHandle next = tex.valid() ? tex : m_dummyShadow;
    if (next == m_shadowMap) return;
    m_shadowMap = next;
    rebuildSceneBindGroup();
}

rhi::PipelineHandle MeshRenderer::pipelineFor(PipelineKey key) {
    for (const CachedPipeline& cp : m_pipelines)
        if (cp.key.blend == key.blend && cp.key.doubleSided == key.doubleSided)
            return cp.pipeline;

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = toBytes(mesh_vert_spv, mesh_vert_spv_size);
    pd.fragmentSpirv       = toBytes(mesh_frag_spv, mesh_frag_spv_size);
    pd.vertexWgsl          = mesh_vert_spv_wgsl;
    pd.fragmentWgsl        = mesh_frag_spv_wgsl;
    pd.vertexLayout.stride = sizeof(MeshVertex);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, position)},
        {.location = 1, .format = rhi::VertexFormat::Float3, .offset = offsetof(MeshVertex, normal)},
        {.location = 2, .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv)},
        {.location = 3, .format = rhi::VertexFormat::Float4, .offset = offsetof(MeshVertex, tangent)},
        {.location = 4, .format = rhi::VertexFormat::Float4, .offset = offsetof(MeshVertex, color)},
        {.location = 5, .format = rhi::VertexFormat::Float2, .offset = offsetof(MeshVertex, uv1)},
        {.location = 6, .format = rhi::VertexFormat::UInt4x8, .offset = offsetof(MeshVertex, joints)},
        {.location = 7, .format = rhi::VertexFormat::Float4,  .offset = offsetof(MeshVertex, weights)},
    };
    pd.topology         = rhi::PrimitiveTopology::TriangleList;
    pd.cull             = key.doubleSided ? rhi::CullMode::None : rhi::CullMode::Back;
    pd.colorFormat      = m_colorFormat;
    pd.blendMode        = key.blend;
    pd.hasPbrMaterial   = true;     // set 0: the material's five maps
    pd.hasUniformBuffer = true;     // set 1: per-frame data
    pd.hasInstanceBuffer = true;    // set 1, binding 1: previous model matrices
    pd.hasSceneTextures = true;     // set 2: IBL cubemaps + shadow map
    pd.pushConstantSize = sizeof(Push);
    pd.depthTest        = true;
    // A blended surface must not write depth, or the surfaces behind it — drawn
    // later, since they sort back-to-front — get rejected and vanish.
    pd.depthWrite       = key.blend == rhi::BlendMode::Opaque;
    pd.depthCompare     = rhi::CompareOp::LessEqual;
    pd.depthFormat      = m_depthFormat;
    pd.debugName        = "mesh_pipeline";

    const rhi::PipelineHandle pipe = m_device.createGraphicsPipeline(pd);
    m_pipelines.push_back({key, pipe});
    return pipe;
}

MeshHandle MeshRenderer::createMesh(const MeshData& data, bool dynamic) {
    const MeshHandle h = createMesh(data.vertices.data(), data.vertices.size(),
                                    data.indices.data(), data.indices.size(), dynamic);
    if (h.valid() && !data.morphTargets.empty()) {
        appendMorphTargets(m_meshes[h.index], data);
        m_meshes[h.index].cpu.morphTargets = data.morphTargets;   // the CPU path needs them too
    }
    return h;
}

MeshHandle MeshRenderer::createMesh(const MeshVertex* vertices, usize vertexCount,
                                    const u32* indices, usize indexCount, bool dynamic) {
    GpuMesh gm;
    gm.indexCount  = static_cast<u32>(indexCount);
    gm.vertexCount = static_cast<u32>(vertexCount);
    gm.dynamic     = dynamic;
    gm.alive       = true;
    gm.vbo = m_device.createBuffer(
        {.size = vertexCount * sizeof(MeshVertex), .usage = rhi::BufferUsage::Vertex,
         .domain = dynamic ? rhi::MemoryDomain::Upload : rhi::MemoryDomain::Device,
         .debugName = "mesh_vertices"},
        vertices);
    gm.ibo = m_device.createBuffer(
        {.size = indexCount * sizeof(u32), .usage = rhi::BufferUsage::Index,
         .domain = rhi::MemoryDomain::Device, .debugName = "mesh_indices"},
        indices);

    // Keep the triangles on the CPU so rayCast() can hit them exactly.
    gm.cpu.vertices.assign(vertices, vertices + vertexCount);
    gm.cpu.indices.assign(indices, indices + indexCount);

    const u32 index = static_cast<u32>(m_meshes.size());
    m_meshes.push_back(std::move(gm));
    return {.index = index, .generation = 0};
}

void MeshRenderer::appendMorphTargets(GpuMesh& gm, const MeshData& data) {
    if (data.morphTargets.empty()) return;

    gm.morphBase    = static_cast<u32>(m_morphData.size());
    gm.morphTargets = static_cast<u32>(data.morphTargets.size());

    // Laid out target-major: all of target 0's deltas, then all of target 1's. The vertex
    // shader indexes it as base + target * vertexCount + gl_VertexIndex, which is one multiply
    // and an add — the alternative (vertex-major) would need the target count at every lookup.
    for (const MorphTarget& target : data.morphTargets) {
        for (usize v = 0; v < gm.vertexCount; ++v) {
            GpuMorphDelta d{};
            if (v < target.positions.size())
                d.position = {target.positions[v].x, target.positions[v].y,
                              target.positions[v].z, 0.0f};
            if (v < target.normals.size())
                d.normal = {target.normals[v].x, target.normals[v].y, target.normals[v].z, 0.0f};
            m_morphData.push_back(d);
        }
    }

    // Grow the GPU buffer to fit, and rebuild the bind groups that name it. This happens at
    // load, not per frame — the shapes are fixed; only the weights move.
    const u32 needed = static_cast<u32>(m_morphData.size());
    if (needed > m_morphCapacity) {
        m_device.waitIdle();
        if (m_morphBuffer.valid()) m_device.destroyBuffer(m_morphBuffer);

        u32 cap = m_morphCapacity > 0 ? m_morphCapacity : 1024;
        while (cap < needed) cap *= 2;
        m_morphBuffer = m_device.createBuffer(
            {.size = static_cast<u64>(cap) * sizeof(GpuMorphDelta),
             .usage = rhi::BufferUsage::Storage, .domain = rhi::MemoryDomain::Upload,
             .debugName = "mesh_morph_targets"});
        m_morphCapacity = cap;
        rebuildFrameBindGroups();
    }
    m_device.updateBuffer(m_morphBuffer, m_morphData.data(),
                          m_morphData.size() * sizeof(GpuMorphDelta));
}

void MeshRenderer::updateMesh(MeshHandle h, const MeshVertex* vertices, usize vertexCount) {
    if (!h.valid() || h.index >= m_meshes.size() || vertices == nullptr) return;
    GpuMesh& gm = m_meshes[h.index];
    VORTEX_ASSERT(gm.dynamic, "updateMesh() on a mesh that was not created dynamic");
    if (!gm.alive || !gm.dynamic) return;

    const usize count = std::min(vertexCount, static_cast<usize>(gm.vertexCount));
    m_device.updateBuffer(gm.vbo, vertices, count * sizeof(MeshVertex));
}

void MeshRenderer::destroyMesh(MeshHandle h) {
    if (!h.valid() || h.index >= m_meshes.size()) return;
    GpuMesh& gm = m_meshes[h.index];
    if (!gm.alive) return;
    m_device.waitIdle();
    m_device.destroyBuffer(gm.vbo);
    m_device.destroyBuffer(gm.ibo);
    gm.cpu = {};
    gm.alive = false;
}

MaterialHandle MeshRenderer::createMaterial(const MaterialDesc& desc) {
    GpuMaterial mat;
    mat.desc  = desc;
    mat.alive = true;

    rhi::SamplerHandle sampler = desc.sampler;
    if (!sampler.valid()) sampler = m_defaultSampler;

    mat.bindGroup = m_device.createBindGroup({
        .isPbrMaterialSet  = true,
        .albedo            = desc.albedo.valid()            ? desc.albedo            : m_white,
        .normalMap         = desc.normalMap.valid()         ? desc.normalMap         : m_flatNormal,
        .metallicRoughness = desc.metallicRoughness.valid() ? desc.metallicRoughness : m_white,
        .emissive          = desc.emissive.valid()          ? desc.emissive          : m_white,
        .occlusion         = desc.occlusion.valid()         ? desc.occlusion         : m_white,
        .lightmap          = desc.lightmap.valid()          ? desc.lightmap          : m_black,
        .materialSampler   = sampler,
    });

    const u32 index = static_cast<u32>(m_materials.size());
    m_materials.push_back(mat);
    return {.index = index, .generation = 0};
}

void MeshRenderer::destroyMaterial(MaterialHandle h) {
    if (!h.valid() || h.index >= m_materials.size()) return;
    GpuMaterial& mat = m_materials[h.index];
    if (!mat.alive) return;
    m_device.waitIdle();
    m_device.destroyBindGroup(mat.bindGroup);
    if (mat.ownedSampler.valid()) m_device.destroySampler(mat.ownedSampler);
    mat.alive = false;
}

void MeshRenderer::buildCascades(const Camera& camera, const DirectionalLight& sun,
                                 const ShadowSettings& sh) {
    const Vec3 lightDir = normalize(sun.direction);
    const Vec3 up = std::fabs(lightDir.y) > 0.99f ? Vec3{0.0f, 0.0f, 1.0f}
                                                  : Vec3{0.0f, 1.0f, 0.0f};

    const f32 nearZ = camera.nearZ;
    const f32 farZ  = std::min(sh.maxDistance, camera.farZ);

    // Where the splits fall. A uniform spread wastes the near cascade on distance a
    // perspective camera barely uses; a logarithmic one starves the far cascade.
    // `splitLambda` mixes them — the standard practical-split-scheme compromise.
    f32 splits[kMaxCascades + 1];
    splits[0] = nearZ;
    for (u32 i = 1; i <= m_cascadeCount; ++i) {
        const f32 t   = static_cast<f32>(i) / static_cast<f32>(m_cascadeCount);
        const f32 log = nearZ * std::pow(farZ / nearZ, t);
        const f32 uni = nearZ + (farZ - nearZ) * t;
        splits[i] = sh.splitLambda * log + (1.0f - sh.splitLambda) * uni;
    }

    // The camera's basis, to walk out its frustum corners.
    const Vec3 fwd   = normalize(camera.target - camera.position);
    const Vec3 right = normalize(cross(fwd, camera.up));
    const Vec3 camUp = cross(right, fwd);

    const f32 tanHalfV = std::tan(camera.fovYRadians * 0.5f);
    const f32 tanHalfH = tanHalfV * camera.aspect;

    for (u32 c = 0; c < m_cascadeCount; ++c) {
        const f32 zn = splits[c], zf = splits[c + 1];

        Vec3 corners[8];
        u32  n = 0;
        for (const f32 z : {zn, zf}) {
            const Vec3 centre = camera.position + fwd * z;
            const f32  h = tanHalfV * z, w = tanHalfH * z;
            corners[n++] = centre + camUp * h + right * w;
            corners[n++] = centre + camUp * h - right * w;
            corners[n++] = centre - camUp * h + right * w;
            corners[n++] = centre - camUp * h - right * w;
        }

        // Fit a SPHERE to the slice, not a box. A box fitted to the frustum changes size
        // as the camera turns, and a shadow map that resizes every frame shimmers; the
        // bounding sphere of a slice is rotation-invariant, so the map stays steady.
        Vec3 centroid{0.0f, 0.0f, 0.0f};
        for (const Vec3& p : corners) centroid = centroid + p;
        centroid = centroid * (1.0f / 8.0f);

        f32 radius = 0.0f;
        for (const Vec3& p : corners) radius = std::max(radius, length(p - centroid));
        radius = std::ceil(radius * 16.0f) / 16.0f;   // quantise, so it stops breathing

        const f32  depth = radius * 4.0f;   // room for casters standing behind the slice
        const Vec3 eye   = centroid - lightDir * (radius * 2.0f);

        Mat4 view = Mat4::lookAt(eye, centroid, up);
        Mat4 proj = Mat4::orthoRH(-radius, radius, -radius, radius, 0.0f, depth);

        // Snap the whole cascade to whole shadow texels. Without this the map slides
        // under the geometry by a fraction of a texel every frame and the shadow edges
        // crawl — the single most visible CSM artefact there is.
        const f32  texels = static_cast<f32>(m_shadowAtlasRes) /
                            (m_cascadeCount > 1 ? 2.0f : 1.0f);
        const Mat4 vp     = proj * view;
        const Vec4 origin = vp * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
        const f32  sx = origin.x * texels * 0.5f, sy = origin.y * texels * 0.5f;
        const f32  ox = (std::round(sx) - sx) * 2.0f / texels;
        const f32  oy = (std::round(sy) - sy) * 2.0f / texels;
        proj.at(0, 3) += ox;
        proj.at(1, 3) += oy;

        m_frameData.cascadeViewProj[c] = proj * view;
        reinterpret_cast<f32*>(&m_frameData.cascadeSplits)[c] = zf;
        // What one texel of this cascade covers in world units — the shader offsets its
        // lookup by this much along the normal, which is what keeps a far cascade's
        // coarse texels from striping the ground with acne.
        reinterpret_cast<f32*>(&m_frameData.cascadeTexelWorld)[c] = 2.0f * radius / texels;
    }

    // Unused cascade slots point past the far plane, so nothing ever selects them.
    for (u32 c = m_cascadeCount; c < kMaxCascades; ++c) {
        m_frameData.cascadeViewProj[c] = m_frameData.cascadeViewProj[m_cascadeCount - 1];
        reinterpret_cast<f32*>(&m_frameData.cascadeSplits)[c] = farZ;
        reinterpret_cast<f32*>(&m_frameData.cascadeTexelWorld)[c] =
            reinterpret_cast<f32*>(&m_frameData.cascadeTexelWorld)[m_cascadeCount - 1];
    }
}

void MeshRenderer::begin(const Camera& camera, const SceneLighting& scene) {
    m_instances.clear();
    m_decals.clear();
    m_opaque.clear();
    m_blended.clear();
    m_classified = false;
    m_drawCalls  = 0;

    // Advance the frame-in-flight slot here rather than at the end of a pass: the
    // deferred path records several passes per frame, and each must see the same
    // uniform buffer.
    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;

    const DirectionalLight& sun = scene.sun;
    const Vec3 d = normalize(sun.direction);

    const Mat4 viewProj = camera.viewProjection();
    m_cameraPos = camera.position;

    const ShadowSettings& sh = scene.shadow;
    m_cascadeCount   = std::clamp(sh.cascadeCount, 1u, kMaxCascades);
    m_shadowAtlasRes = std::max(sh.resolution, 1u);
    buildCascades(camera, sun, sh);

    // m_frameData still holds last frame's matrix at this point — grab it before it is
    // overwritten; that is the whole of what motion blur needs to know about the past.
    if (m_hasPrevFrame) m_prevViewProj = m_frameData.viewProj;
    else                m_prevViewProj = viewProj;   // first frame: nothing has moved yet
    m_hasPrevFrame = true;
    m_frameData.prevViewProj = m_prevViewProj;
    m_frameData.invViewProj  = viewProj.inverse();

    m_frameData.viewProj      = viewProj;
    m_frameData.lightViewProj = m_frameData.cascadeViewProj[0];
    m_frameData.cameraPos = {camera.position.x, camera.position.y, camera.position.z, 0.0f};
    m_frameData.ambient   = {sun.ambient.x, sun.ambient.y, sun.ambient.z, 1.0f};

    const Fog& fog = scene.fog;
    m_frameData.fogColor  = {fog.color.x, fog.color.y, fog.color.z, fog.density};
    m_frameData.fogParams = {fog.start, fog.end, static_cast<f32>(fog.mode), fog.heightFalloff};

    m_frameData.shadowParams = {sh.depthBias, sh.normalBias,
                                static_cast<f32>(std::max(sh.pcfRadius, 0)),
                                sh.enabled ? 1.0f : 0.0f};

    const ContactShadows& cs = scene.contactShadows;
    m_frameData.contactParams = {cs.enabled ? 1.0f : 0.0f, cs.distance,
                                 static_cast<f32>(std::max(cs.steps, 1)), cs.thickness};

    // Light 0 is always the sun: the shadow map was rendered from it, and the
    // shader only consults the shadow map for light 0.
    m_frameData.lights[0].position  = {0.0f, 0.0f, 0.0f, static_cast<f32>(LightType::Directional)};
    m_frameData.lights[0].direction = {d.x, d.y, d.z, 0.0f};
    m_frameData.lights[0].color     = {sun.color.x, sun.color.y, sun.color.z, sun.intensity};
    m_frameData.lights[0].params    = {0.0f, 0.0f, 0.0f, 0.0f};

    u32 count = 1;
    for (const Light& lt : scene.lights) {
        if (count >= kMaxLights) break;
        const Vec3 ld = normalize(lt.direction);
        GpuLight& g = m_frameData.lights[count];
        g.position  = {lt.position.x, lt.position.y, lt.position.z,
                       static_cast<f32>(lt.type)};
        g.direction = {ld.x, ld.y, ld.z, lt.range};
        g.color     = {lt.color.x, lt.color.y, lt.color.z, lt.intensity};
        g.params    = {std::cos(lt.innerAngle), std::cos(lt.outerAngle), lt.radius, 0.0f};
        ++count;
    }
    m_frameData.misc = {static_cast<f32>(count), static_cast<f32>(m_cascadeCount), 0.0f, 0.0f};

    m_skyData.invViewProj = viewProj.inverse();
    m_skyData.cameraPos   = m_frameData.cameraPos;
    m_skyData.params      = {scene.skyboxIntensity, 0.0f, 0.0f, 0.0f};

    m_deferredPush.invViewProj = m_skyData.invViewProj;
    m_deferredPush.params      = {scene.skyboxIntensity, 1.0f,
                                  scene.ssao.enabled ? 1.0f : 0.0f, 0.0f};

    m_ssaoPush.invViewProj = m_skyData.invViewProj;
    m_ssaoPush.params      = {scene.ssao.radius, scene.ssao.intensity,
                              scene.ssao.bias, scene.ssao.power};

    const VolumetricFog& vf = scene.volumetric;
    m_volumetricOn          = vf.enabled;
    m_volumePush.invViewProj = m_skyData.invViewProj;
    m_volumePush.params = {vf.density, static_cast<f32>(std::max(vf.steps, 1)),
                           vf.maxDistance, std::clamp(vf.anisotropy, -0.95f, 0.95f)};
    m_volumePush.color  = {vf.color.x, vf.color.y, vf.color.z, 0.0f};

    m_device.updateBuffer(m_uniformBuffers[m_frame], &m_frameData, sizeof(FrameUBO));
    m_device.updateBuffer(m_skyBuffers[m_frame], &m_skyData, sizeof(SkyUBO));
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

    // The shadow pass is usually recorded first, and it reads the instance and bone buffers
    // that classify() uploads. Relying on a later end() to have uploaded them by then works
    // only by accident of ordering — an app that renders shadows and nothing else would get
    // last frame's pose. So do it here, and let the flag make the second call free.
    classify();

    cmd.setPipeline(m_shadowPipeline);
    cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);   // set 1: frame + instances + bones

    // One cascade fills the map; several tile it 2x2. Each gets its own viewport, so the
    // same geometry is recorded once per cascade against a different light matrix.
    const f32 res  = static_cast<f32>(m_shadowAtlasRes);
    const f32 tile = m_cascadeCount > 1 ? res * 0.5f : res;

    for (u32 c = 0; c < m_cascadeCount; ++c) {
        const f32 tx = m_cascadeCount > 1 ? static_cast<f32>(c % 2) * tile : 0.0f;
        const f32 ty = m_cascadeCount > 1 ? static_cast<f32>(c / 2) * tile : 0.0f;

        cmd.setViewport({.x = tx, .y = ty, .width = tile, .height = tile});
        cmd.setScissor(static_cast<u32>(tx), static_cast<u32>(ty),
                       static_cast<u32>(tile), static_cast<u32>(tile));

        for (u32 i = 0; i < m_instances.size(); ++i) {
            const MeshInstance& inst = m_instances[i];
            if (!inst.castsShadow) continue;
            if (!inst.mesh.valid() || inst.mesh.index >= m_meshes.size()) continue;
            const GpuMesh& gm = m_meshes[inst.mesh.index];
            if (!gm.alive || gm.indexCount == 0) continue;

            const MaterialHandle mh = inst.material.valid() ? inst.material : m_defaultMaterial;
            if (mh.index >= m_materials.size() || !m_materials[mh.index].alive) continue;
            const GpuMaterial& mat = m_materials[mh.index];

            // A blended surface has no business writing an opaque depth. A pane of glass that
            // casts a solid black shadow is worse than one that casts none, and a depth map
            // has no way to record "let 40% of the light through".
            if (mat.desc.blend != rhi::BlendMode::Opaque) continue;

            const ShadowPush pc{m_frameData.cascadeViewProj[c] * inst.model,
                                {mat.desc.alphaCutoff, mat.desc.uvScale, 0.0f, 0.0f}};

            cmd.setBindGroup(0, mat.bindGroup);   // set 0: the albedo, for the alpha test
            cmd.pushConstants(&pc, sizeof(ShadowPush));
            cmd.setVertexBuffer(0, gm.vbo);
            cmd.setIndexBuffer(gm.ibo, rhi::IndexType::U32);
            cmd.drawIndexed(gm.indexCount, 1, 0, 0, i);   // firstInstance = the pose to skin by
        }
    }
}

void MeshRenderer::renderVolumetric(rhi::ICommandList& cmd) {
    if (!m_volumetricOn || !m_ssaoBindGroup.valid()) return;
    VORTEX_PROFILE_ZONE("mesh.volumetric");

    cmd.setPipeline(m_volumetricPipeline);
    cmd.setBindGroup(0, m_ssaoBindGroup);                // set 0: G-buffer (depth)
    cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);   // set 1: frame data
    cmd.setBindGroup(2, m_sceneBindGroup);               // set 2: the shadow map
    cmd.pushConstants(&m_volumePush, sizeof(VolumePush));
    cmd.draw(3);
}

void MeshRenderer::renderSkybox(rhi::ICommandList& cmd) {
    VORTEX_PROFILE_ZONE("mesh.skybox");
    cmd.setPipeline(m_skyboxPipeline);
    cmd.setBindGroup(0, m_skyBindGroups[m_frame]);   // set 0: sky uniforms
    cmd.setBindGroup(1, m_sceneBindGroup);           // set 1: IBL + shadow map
    cmd.draw(3);
}

void MeshRenderer::recordInstance(rhi::ICommandList& cmd, const MeshInstance& inst,
                                  u32 instanceIndex) {
    const GpuMesh& gm = m_meshes[inst.mesh.index];

    const MaterialHandle mh = inst.material.valid() ? inst.material : m_defaultMaterial;
    const GpuMaterial& mat  = m_materials[mh.index];
    const MaterialDesc& d   = mat.desc;

    // Without a material, the instance's own colour/metallic/roughness are the
    // material — the short path for a plain untextured surface.
    const bool bare = !inst.material.valid();

    Push pc;
    // The first three rows of the model matrix; the fourth is (0,0,0,1) by construction.
    const Mat4& m = inst.model;
    pc.modelRow0 = {m.at(0, 0), m.at(0, 1), m.at(0, 2), m.at(0, 3)};
    pc.modelRow1 = {m.at(1, 0), m.at(1, 1), m.at(1, 2), m.at(1, 3)};
    pc.modelRow2 = {m.at(2, 0), m.at(2, 1), m.at(2, 2), m.at(2, 3)};

    pc.color = bare ? inst.color
                    : Vec4{d.baseColor.x * inst.color.x, d.baseColor.y * inst.color.y,
                           d.baseColor.z * inst.color.z, d.baseColor.w * inst.color.w};
    pc.material = bare ? Vec4{inst.metallic, inst.roughness, 1.0f, 1.0f}
                       : Vec4{d.metallic, d.roughness, d.normalScale, d.occlusionStrength};
    pc.emissive = bare ? Vec4{0.0f, 0.0f, 0.0f, 0.0f}
                       : Vec4{d.emissiveColor.x, d.emissiveColor.y, d.emissiveColor.z,
                              d.emissiveStrength};
    pc.params = {bare ? 0.0f : d.alphaCutoff,
                 bare ? 1.0f : d.uvScale,
                 (!bare && d.unlit) ? 1.0f : 0.0f,
                 inst.receivesShadow ? 1.0f : 0.0f};
    pc.extra  = bare ? Vec4{0.0f, 0.0f, 0.0f, 1.5f}
                     : Vec4{d.parallaxScale,
                            static_cast<f32>(std::max(d.parallaxLayers, 4)),
                            d.transmission, d.ior};

    cmd.setBindGroup(0, mat.bindGroup);
    cmd.pushConstants(&pc, sizeof(Push));
    cmd.setVertexBuffer(0, gm.vbo);
    cmd.setIndexBuffer(gm.ibo, rhi::IndexType::U32);
    // firstInstance is how the vertex stage learns its index: gl_InstanceIndex picks up
    // exactly this value, which costs nothing and needs no push-constant bytes — of which
    // there are none left.
    cmd.drawIndexed(gm.indexCount, 1, 0, 0, instanceIndex);
    ++m_drawCalls;
}

void MeshRenderer::submitDecal(const Decal& d) { m_decals.push_back(d); }

rhi::BindGroupHandle MeshRenderer::decalGroup(rhi::TextureHandle tex) {
    for (const DecalCache& dc : m_decalCache)
        if (dc.tex == tex) return dc.group;

    const rhi::BindGroupHandle group = m_device.createBindGroup(
        {.texture = tex, .sampler = m_defaultSampler});
    m_decalCache.push_back({tex, group});
    return group;
}

void MeshRenderer::renderDecals(rhi::ICommandList& cmd) {
    if (m_decals.empty() || !m_unitCube.valid()) return;
    VORTEX_PROFILE_ZONE("mesh.decals");

    const GpuMesh& box = m_meshes[m_unitCube.index];

    cmd.setPipeline(m_decalPipeline);
    cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);   // set 1: frame data
    cmd.setBindGroup(2, m_sceneBindGroup);               // set 2: the G-buffer's depth
    cmd.setVertexBuffer(0, box.vbo);
    cmd.setIndexBuffer(box.ibo, rhi::IndexType::U32);

    for (const Decal& d : m_decals) {
        if (!d.texture.valid()) continue;

        const Mat4& m   = d.model;
        const Mat4  inv = m.inverse();

        DecalPush pc;
        pc.modelRow0 = {m.at(0, 0), m.at(0, 1), m.at(0, 2), m.at(0, 3)};
        pc.modelRow1 = {m.at(1, 0), m.at(1, 1), m.at(1, 2), m.at(1, 3)};
        pc.modelRow2 = {m.at(2, 0), m.at(2, 1), m.at(2, 2), m.at(2, 3)};
        pc.invRow0   = {inv.at(0, 0), inv.at(0, 1), inv.at(0, 2), inv.at(0, 3)};
        pc.invRow1   = {inv.at(1, 0), inv.at(1, 1), inv.at(1, 2), inv.at(1, 3)};
        pc.invRow2   = {inv.at(2, 0), inv.at(2, 1), inv.at(2, 2), inv.at(2, 3)};
        pc.params    = {d.opacity, d.angleFade, 0.0f, 0.0f};

        cmd.setBindGroup(0, decalGroup(d.texture));
        cmd.pushConstants(&pc, sizeof(DecalPush));
        cmd.drawIndexed(box.indexCount);
        ++m_drawCalls;
    }
}

void MeshRenderer::classify() {
    if (m_classified) return;
    m_classified = true;

    // Split the queue: opaque surfaces can be drawn in any order (the depth buffer
    // resolves them), blended ones cannot — they must arrive back-to-front.
    for (u32 i = 0; i < m_instances.size(); ++i) {
        const MeshInstance& inst = m_instances[i];
        if (!inst.mesh.valid() || inst.mesh.index >= m_meshes.size()) continue;
        const GpuMesh& gm = m_meshes[inst.mesh.index];
        if (!gm.alive || gm.indexCount == 0) continue;

        rhi::BlendMode blend = rhi::BlendMode::Opaque;
        if (inst.material.valid()) {
            if (inst.material.index >= m_materials.size() ||
                !m_materials[inst.material.index].alive)
                continue;
            blend = m_materials[inst.material.index].desc.blend;
        }
        (blend == rhi::BlendMode::Opaque ? m_opaque : m_blended).push_back(i);
    }

    auto viewDepth = [&](u32 i) {
        const Mat4& m = m_instances[i].model;
        const Vec3 centre{m.at(0, 3), m.at(1, 3), m.at(2, 3)};   // translation column
        const Vec3 toEye = centre - m_cameraPos;
        return dot(toEye, toEye);
    };
    std::sort(m_blended.begin(), m_blended.end(),
              [&](u32 a, u32 b) { return viewDepth(a) > viewDepth(b); });   // far -> near

    // Per-instance data goes up once, before anything is recorded. An instance that did
    // not say where it was last frame is treated as stationary, so its only motion comes
    // from the camera — which is what a reprojection would have given anyway.
    // Pack every skinned instance's bones into one buffer, and hand each instance the offset
    // where its own begin. One buffer and one upload, however many characters there are.
    m_boneData.clear();
    m_instanceData.resize(m_instances.size());
    for (usize i = 0; i < m_instances.size(); ++i) {
        const MeshInstance& inst = m_instances[i];
        m_instanceData[i].prevModel = inst.hasPrevModel ? inst.prevModel : inst.model;

        f32 lightmap = 0.0f;
        if (inst.material.valid() && inst.material.index < m_materials.size()) {
            const MaterialDesc& d = m_materials[inst.material.index].desc;
            if (d.lightmap.valid()) lightmap = d.lightmapIntensity;
        }

        f32 boneOffset = 0.0f, boneCount = 0.0f;
        if (inst.bones != nullptr && inst.boneCount > 0) {
            boneOffset = static_cast<f32>(m_boneData.size());
            boneCount  = static_cast<f32>(inst.boneCount);
            m_boneData.insert(m_boneData.end(), inst.bones, inst.bones + inst.boneCount);
        }

        GpuInstance& gi = m_instanceData[i];
        gi.morphInfo      = {0.0f, 0.0f, 0.0f, 0.0f};
        gi.morphWeights[0] = {0.0f, 0.0f, 0.0f, 0.0f};
        gi.morphWeights[1] = {0.0f, 0.0f, 0.0f, 0.0f};

        f32 morphBase = 0.0f;
        if (inst.morphWeights != nullptr && inst.morphCount > 0 &&
            inst.mesh.valid() && inst.mesh.index < m_meshes.size()) {
            const GpuMesh& gm = m_meshes[inst.mesh.index];
            const u32 count = std::min({inst.morphCount, gm.morphTargets, 8u});
            morphBase = static_cast<f32>(gm.morphBase);
            gi.morphInfo = {static_cast<f32>(count), static_cast<f32>(gm.vertexCount), 0.0f, 0.0f};
            for (u32 w = 0; w < count; ++w)
                (&gi.morphWeights[w / 4].x)[w % 4] = inst.morphWeights[w];
        }

        gi.params = {lightmap, boneOffset, boneCount, morphBase};
    }

    ensureInstanceCapacity(static_cast<u32>(m_instances.size()),
                           static_cast<u32>(m_boneData.size()));

    if (!m_instanceData.empty())
        m_device.updateBuffer(m_instanceBuffers[m_frame], m_instanceData.data(),
                              m_instanceData.size() * sizeof(GpuInstance));
    if (!m_boneData.empty())
        m_device.updateBuffer(m_boneBuffers[m_frame], m_boneData.data(),
                              m_boneData.size() * sizeof(Mat4));
}

void MeshRenderer::drawList(rhi::ICommandList& cmd, const std::vector<u32>& list,
                            rhi::PipelineHandle forced) {
    rhi::PipelineHandle bound{};
    for (const u32 i : list) {
        const MeshInstance& inst = m_instances[i];

        rhi::PipelineHandle pipe = forced;
        if (!pipe.valid()) {
            PipelineKey key;
            if (inst.material.valid()) {
                const MaterialDesc& d = m_materials[inst.material.index].desc;
                key.blend       = d.blend;
                key.doubleSided = d.doubleSided;
            }
            pipe = pipelineFor(key);
        }

        if (pipe != bound) {
            cmd.setPipeline(pipe);
            // The frame and scene sets have the same layout in every mesh pipeline,
            // but the backends track bindings per pipeline, so re-bind after a switch.
            cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);
            if (pipe != m_gbufferPipeline)   // the G-buffer fill has no scene set
                cmd.setBindGroup(2, m_sceneBindGroup);
            bound = pipe;
        }
        recordInstance(cmd, inst, i);
    }
}

void MeshRenderer::end(rhi::ICommandList& cmd) {
    if (m_instances.empty()) return;
    VORTEX_PROFILE_ZONE("mesh.end");

    classify();
    drawList(cmd, m_opaque, {});
    drawList(cmd, m_blended, {});
}

void MeshRenderer::endOpaque(rhi::ICommandList& cmd) {
    if (m_instances.empty()) return;
    VORTEX_PROFILE_ZONE("mesh.opaque");

    classify();
    drawList(cmd, m_opaque, {});
}

// --- Deferred ---------------------------------------------------------------

void MeshRenderer::renderGBuffer(rhi::ICommandList& cmd) {
    if (m_instances.empty()) return;
    VORTEX_PROFILE_ZONE("mesh.gbuffer");

    classify();
    drawList(cmd, m_opaque, m_gbufferPipeline);
}

rhi::BindGroupHandle MeshRenderer::gbufferGroup(rhi::TextureHandle ao) {
    for (const GBufferCache& gc : m_gbufferCache)
        if (gc.albedo == m_gbufferAlbedo && gc.normal == m_gbufferNormal &&
            gc.emissive == m_gbufferEmissive && gc.depth == m_gbufferDepth && gc.ao == ao)
            return gc.group;

    // The G-buffer rides in the PBR material set: five image slots and a sampler is
    // exactly what it needs, so it costs no new descriptor layout. The slot names
    // therefore do not match their contents — see deferred.frag.
    const rhi::BindGroupHandle group = m_device.createBindGroup({
        .isPbrMaterialSet  = true,
        .albedo            = m_gbufferAlbedo,
        .normalMap         = m_gbufferNormal,
        .metallicRoughness = m_gbufferEmissive,
        .emissive          = m_gbufferDepth,
        .occlusion         = ao,
        .lightmap          = m_white,           // unused by the lighting pass
        .materialSampler   = m_shadowSampler,   // point sampling: no G-buffer filtering
    });
    m_gbufferCache.push_back({m_gbufferAlbedo, m_gbufferNormal, m_gbufferEmissive,
                              m_gbufferDepth, ao, group});
    return group;
}

void MeshRenderer::setGBuffer(rhi::TextureHandle albedo, rhi::TextureHandle normal,
                              rhi::TextureHandle emissive, rhi::TextureHandle depth) {
    m_gbufferAlbedo   = albedo;
    m_gbufferNormal   = normal;
    m_gbufferEmissive = emissive;
    m_gbufferDepth    = depth;
    m_aoMap           = m_white;

    // Decals read the depth through the scene set, not the G-buffer set: their pipeline
    // spends set 0 on the decal's own texture.
    const rhi::TextureHandle nextDepth = depth.valid() ? depth : m_dummyShadow;
    if (nextDepth != m_sceneDepth) {
        m_sceneDepth = nextDepth;
        rebuildSceneBindGroup();
    }

    // Two groups over the same G-buffer: the SSAO pass must not have the AO map bound,
    // because that is the target it is writing.
    m_ssaoBindGroup    = gbufferGroup(m_white);
    m_gbufferBindGroup = m_ssaoBindGroup;
}

void MeshRenderer::setAmbientOcclusion(rhi::TextureHandle ao) {
    m_aoMap            = ao.valid() ? ao : m_white;
    m_gbufferBindGroup = gbufferGroup(m_aoMap);
}

void MeshRenderer::renderSSAO(rhi::ICommandList& cmd) {
    if (!m_ssaoBindGroup.valid()) return;
    VORTEX_PROFILE_ZONE("mesh.ssao");

    cmd.setPipeline(m_ssaoPipeline);
    cmd.setBindGroup(0, m_ssaoBindGroup);                // set 0: G-buffer (no AO)
    cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);   // set 1: frame data
    cmd.pushConstants(&m_ssaoPush, sizeof(SsaoPush));
    cmd.draw(3);
}

void MeshRenderer::renderSSAOBlur(rhi::ICommandList& cmd, rhi::BindGroupHandle rawAo,
                                  u32 width, u32 height) {
    if (!rawAo.valid() || width == 0 || height == 0) return;
    VORTEX_PROFILE_ZONE("mesh.ssao_blur");

    const BlurPush push{{1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height),
                         0.0f, 0.0f}};
    cmd.setPipeline(m_ssaoBlurPipeline);
    cmd.setBindGroup(0, rawAo);
    cmd.pushConstants(&push, sizeof(BlurPush));
    cmd.draw(3);
}

void MeshRenderer::renderDeferredLighting(rhi::ICommandList& cmd) {
    if (!m_gbufferBindGroup.valid()) return;
    VORTEX_PROFILE_ZONE("mesh.deferred");

    cmd.setPipeline(m_deferredPipeline);
    cmd.setBindGroup(0, m_gbufferBindGroup);             // set 0: the G-buffer
    cmd.setBindGroup(1, m_uniformBindGroups[m_frame]);   // set 1: frame data
    cmd.setBindGroup(2, m_sceneBindGroup);               // set 2: IBL + shadow map
    cmd.pushConstants(&m_deferredPush, sizeof(DeferredPush));
    cmd.draw(3);
}

void MeshRenderer::endTransparent(rhi::ICommandList& cmd) {
    if (m_instances.empty()) return;
    VORTEX_PROFILE_ZONE("mesh.transparent");

    classify();
    drawList(cmd, m_blended, {});
}

// ---------------------------------------------------------------------------
// Ray casting
// ---------------------------------------------------------------------------

namespace {

// Möller-Trumbore. Returns the ray parameter t, or -1 for a miss.
f32 intersectTriangle(const Ray& r, Vec3 v0, Vec3 v1, Vec3 v2) {
    constexpr f32 kEps = 1e-7f;
    const Vec3 e1 = v1 - v0;
    const Vec3 e2 = v2 - v0;
    const Vec3 p  = cross(r.direction, e2);
    const f32  det = dot(e1, p);
    if (std::fabs(det) < kEps) return -1.0f;   // ray is parallel to the triangle

    const f32  invDet = 1.0f / det;
    const Vec3 tv = r.origin - v0;
    const f32  u  = dot(tv, p) * invDet;
    if (u < 0.0f || u > 1.0f) return -1.0f;

    const Vec3 q = cross(tv, e1);
    const f32  v = dot(r.direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;

    const f32 t = dot(e2, q) * invDet;
    return t > kEps ? t : -1.0f;
}

Vec3 transformPoint(const Mat4& m, Vec3 p) {
    const Vec4 r = m * Vec4{p.x, p.y, p.z, 1.0f};
    return {r.x, r.y, r.z};
}

} // namespace

RayHit MeshRenderer::rayCastMesh(MeshHandle h, const Mat4& model, const Ray& ray) const {
    RayHit best;
    if (!h.valid() || h.index >= m_meshes.size()) return best;
    const GpuMesh& gm = m_meshes[h.index];
    if (!gm.alive) return best;

    // The ray goes into model space, so the triangles are tested where they are
    // stored — one matrix inverse per mesh instead of one transform per vertex.
    const Mat4 inv = model.inverse();
    const Vec4 o = inv * Vec4{ray.origin.x, ray.origin.y, ray.origin.z, 1.0f};
    const Vec4 d = inv * Vec4{ray.direction.x, ray.direction.y, ray.direction.z, 0.0f};
    const Ray  local{{o.x, o.y, o.z}, normalize(Vec3{d.x, d.y, d.z})};

    const std::vector<MeshVertex>& vs = gm.cpu.vertices;
    const std::vector<u32>&        is = gm.cpu.indices;

    f32 bestT = 0.0f;
    for (usize i = 0; i + 2 < is.size(); i += 3) {
        const f32 t = intersectTriangle(local, vs[is[i]].position,
                                        vs[is[i + 1]].position, vs[is[i + 2]].position);
        if (t < 0.0f || (best.hit && t >= bestT)) continue;

        bestT         = t;
        best.hit      = true;
        best.triangle = static_cast<u32>(i / 3);
    }
    if (!best.hit) return best;

    const usize base = static_cast<usize>(best.triangle) * 3;
    const Vec3 p0 = vs[is[base]].position;
    const Vec3 p1 = vs[is[base + 1]].position;
    const Vec3 p2 = vs[is[base + 2]].position;

    const Vec3 localPoint  = local.at(bestT);
    const Vec3 localNormal = normalize(cross(p1 - p0, p2 - p0));

    best.point  = transformPoint(model, localPoint);
    // Normals transform by the inverse-transpose, not the model matrix, or a
    // non-uniform scale tilts them off the surface.
    const Mat4 nrmMat = inv;   // inverse; the transpose is applied by the dot below
    best.normal = normalize(Vec3{
        nrmMat.at(0, 0) * localNormal.x + nrmMat.at(1, 0) * localNormal.y + nrmMat.at(2, 0) * localNormal.z,
        nrmMat.at(0, 1) * localNormal.x + nrmMat.at(1, 1) * localNormal.y + nrmMat.at(2, 1) * localNormal.z,
        nrmMat.at(0, 2) * localNormal.x + nrmMat.at(1, 2) * localNormal.y + nrmMat.at(2, 2) * localNormal.z,
    });
    best.distance = length(best.point - ray.origin);
    return best;
}

// ---------------------------------------------------------------------------
// Lightmap baking
// ---------------------------------------------------------------------------

std::vector<u8> MeshRenderer::bakeLightmap(const MeshInstance& target,
                                           const MeshInstance* occluders, usize occluderCount,
                                           const DirectionalLight& sun,
                                           const LightmapBake& s) const {
    const u32 res = std::max(s.resolution, 4u);
    std::vector<u8> pixels(static_cast<usize>(res) * res * 4, 0);

    if (!target.mesh.valid() || target.mesh.index >= m_meshes.size()) return pixels;
    const GpuMesh& tm = m_meshes[target.mesh.index];
    if (!tm.alive) return pixels;

    // Every occluder's triangles in world space, grouped per object and wrapped in a box.
    //
    // Without the boxes this is unusable: a texel shoots dozens of rays, each ray would
    // test every triangle in the scene, and the bake becomes billions of intersections. A
    // ray that flies off into the sky now costs one slab test per object instead of eight
    // hundred triangle tests, which is the difference between a second and an afternoon.
    struct Tri { Vec3 a, b, c; };
    struct Occluder {
        std::vector<Tri> tris;
        Vec3 bmin{0, 0, 0}, bmax{0, 0, 0};
    };
    std::vector<Occluder> world;

    for (usize i = 0; i < occluderCount; ++i) {
        const MeshInstance& o = occluders[i];
        if (!o.mesh.valid() || o.mesh.index >= m_meshes.size()) continue;
        const GpuMesh& gm = m_meshes[o.mesh.index];
        if (!gm.alive) continue;

        Occluder occ;
        const std::vector<MeshVertex>& vs = gm.cpu.vertices;
        const std::vector<u32>&        is = gm.cpu.indices;
        for (usize t = 0; t + 2 < is.size(); t += 3)
            occ.tris.push_back({transformPoint(o.model, vs[is[t]].position),
                                transformPoint(o.model, vs[is[t + 1]].position),
                                transformPoint(o.model, vs[is[t + 2]].position)});
        if (occ.tris.empty()) continue;

        occ.bmin = occ.bmax = occ.tris[0].a;
        for (const Tri& t : occ.tris)
            for (const Vec3& p : {t.a, t.b, t.c}) {
                occ.bmin = {std::min(occ.bmin.x, p.x), std::min(occ.bmin.y, p.y),
                            std::min(occ.bmin.z, p.z)};
                occ.bmax = {std::max(occ.bmax.x, p.x), std::max(occ.bmax.y, p.y),
                            std::max(occ.bmax.z, p.z)};
            }
        world.push_back(std::move(occ));
    }

    // Slab test: the ray enters the box only if it is inside all three slabs at once.
    auto hitsBox = [](const Ray& r, Vec3 bmin, Vec3 bmax, f32 maxDist) {
        f32 tmin = 0.0f, tmax = maxDist;
        for (int axis = 0; axis < 3; ++axis) {
            const f32 o = (&r.origin.x)[axis];
            const f32 d = (&r.direction.x)[axis];
            const f32 lo = (&bmin.x)[axis], hi = (&bmax.x)[axis];
            if (std::fabs(d) < 1e-8f) {
                if (o < lo || o > hi) return false;   // parallel and outside the slab
                continue;
            }
            f32 t1 = (lo - o) / d, t2 = (hi - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        return true;
    };

    auto occluded = [&](const Ray& r, f32 maxDist) {
        for (const Occluder& occ : world) {
            if (!hitsBox(r, occ.bmin, occ.bmax, maxDist)) continue;
            for (const Tri& t : occ.tris) {
                const f32 hit = intersectTriangle(r, t.a, t.b, t.c);
                if (hit > 0.0f && hit < maxDist) return true;
            }
        }
        return false;
    };

    const Vec3 sunDir   = normalize(sun.direction);
    const Vec3 toSun    = sunDir * -1.0f;
    const Vec3 sunLight = sun.color * sun.intensity;

    const std::vector<MeshVertex>& vs = tm.cpu.vertices;
    const std::vector<u32>&        is = tm.cpu.indices;

    // Rasterise the mesh in ITS OWN UV1 space. Each texel the triangle covers is a point on
    // the surface, recovered by barycentric interpolation — this is the whole reason the
    // lightmap needs an unwrap where no two triangles overlap.
    for (usize t = 0; t + 2 < is.size(); t += 3) {
        const MeshVertex& v0 = vs[is[t]];
        const MeshVertex& v1 = vs[is[t + 1]];
        const MeshVertex& v2 = vs[is[t + 2]];

        const Vec2 uv0 = v0.uv1, uv1 = v1.uv1, uv2 = v2.uv1;

        const f32 minU = std::min({uv0.x, uv1.x, uv2.x});
        const f32 maxU = std::max({uv0.x, uv1.x, uv2.x});
        const f32 minV = std::min({uv0.y, uv1.y, uv2.y});
        const f32 maxV = std::max({uv0.y, uv1.y, uv2.y});

        const i32 x0 = std::max(0, static_cast<i32>(minU * res) - 1);
        const i32 x1 = std::min(static_cast<i32>(res) - 1, static_cast<i32>(maxU * res) + 1);
        const i32 y0 = std::max(0, static_cast<i32>(minV * res) - 1);
        const i32 y1 = std::min(static_cast<i32>(res) - 1, static_cast<i32>(maxV * res) + 1);

        const f32 denom = (uv1.y - uv2.y) * (uv0.x - uv2.x) + (uv2.x - uv1.x) * (uv0.y - uv2.y);
        if (std::fabs(denom) < 1e-12f) continue;   // degenerate in UV space: no texels

        for (i32 py = y0; py <= y1; ++py) {
            for (i32 px = x0; px <= x1; ++px) {
                const f32 u = (static_cast<f32>(px) + 0.5f) / static_cast<f32>(res);
                const f32 v = (static_cast<f32>(py) + 0.5f) / static_cast<f32>(res);

                f32 w0 = ((uv1.y - uv2.y) * (u - uv2.x) + (uv2.x - uv1.x) * (v - uv2.y)) / denom;
                f32 w1 = ((uv2.y - uv0.y) * (u - uv2.x) + (uv0.x - uv2.x) * (v - uv2.y)) / denom;
                f32 w2 = 1.0f - w0 - w1;

                // A small slack, so texels straddling a triangle's edge still get filled —
                // otherwise the seams between triangles bake as black cracks.
                constexpr f32 kSlack = -0.02f;
                if (w0 < kSlack || w1 < kSlack || w2 < kSlack) continue;

                const Vec3 localP = v0.position * w0 + v1.position * w1 + v2.position * w2;
                const Vec3 localN = v0.normal * w0 + v1.normal * w1 + v2.normal * w2;

                const Vec3 P = transformPoint(target.model, localP);
                const Vec3 N = normalize(transformPoint(target.model, localP + localN) - P);

                const Vec3 origin = P + N * s.rayBias;

                // Direct sun, shadowed by everything in the world — only if asked for. See
                // LightmapBake::directSun: by default the real-time pass owns the sun and
                // the bake owns only what the real-time pass cannot see.
                Vec3 radiance{0.0f, 0.0f, 0.0f};
                if (s.directSun) {
                    const f32 NdotL = dot(N, toSun);
                    if (NdotL > 0.0f && !occluded({origin, toSun}, 1e6f))
                        radiance = radiance + sunLight * NdotL;
                }

                // Sky: cosine-weighted hemisphere around N. This is the part a shadow map
                // cannot give you — soft occlusion from geometry anywhere in the scene.
                Vec3 up = std::fabs(N.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
                const Vec3 tangent  = normalize(cross(up, N));
                const Vec3 bitangent = cross(N, tangent);

                f32 visible = 0.0f;
                for (u32 i = 0; i < s.skySamples; ++i) {
                    // Deterministic low-discrepancy directions: a golden-ratio spiral over
                    // the hemisphere. Random ones would make the bake differ between runs.
                    const f32 a  = (static_cast<f32>(i) + 0.5f) / static_cast<f32>(s.skySamples);
                    const f32 r  = std::sqrt(a);                    // cosine-weighted
                    const f32 ph = static_cast<f32>(i) * 2.39996323f;   // golden angle
                    const Vec3 dir = normalize(tangent * (r * std::cos(ph)) +
                                               bitangent * (r * std::sin(ph)) +
                                               N * std::sqrt(std::max(1.0f - a, 0.0f)));
                    if (!occluded({origin, dir}, 1e6f)) visible += 1.0f;
                }
                visible /= static_cast<f32>(s.skySamples);
                radiance = radiance + s.skyColor * (visible * s.skyIntensity);

                const usize idx = (static_cast<usize>(py) * res + static_cast<usize>(px)) * 4;
                for (int c = 0; c < 3; ++c) {
                    const f32 value = (&radiance.x)[c];
                    pixels[idx + static_cast<usize>(c)] =
                        static_cast<u8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
                }
                pixels[idx + 3] = 255;
            }
        }
    }
    return pixels;
}

RayHit MeshRenderer::rayCast(const Ray& ray) const {
    RayHit best;
    for (usize i = 0; i < m_instances.size(); ++i) {
        RayHit h = rayCastMesh(m_instances[i].mesh, m_instances[i].model, ray);
        if (!h.hit) continue;
        if (best.hit && h.distance >= best.distance) continue;
        h.instance = i;
        best = h;
    }
    return best;
}

}
