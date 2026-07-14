#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <vector>

namespace vortex::rhi {
class IGraphicsDevice;
class ICommandList;
}

namespace vortex::renderer {

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    // Tangent frame for normal mapping: xyz = tangent, w = bitangent handedness.
    // The default is a valid frame, so a mesh built without tangents still shades.
    Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

enum class LightType : u32 { Directional = 0, Point = 1, Spot = 2 };

struct Light {
    LightType type = LightType::Point;

    Vec3 position{0.0f, 0.0f, 0.0f};      // point/spot
    Vec3 direction{0.0f, -1.0f, 0.0f};    // directional/spot: the way light travels
    Vec3 color{1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;

    // Point/spot: influence is windowed to zero at `range`, so cost is bounded and
    // nothing pops at the edge. 0 means a pure inverse-square light with no cutoff.
    f32  range = 20.0f;

    // Spot cone, in radians from the axis. Between inner and outer the light fades.
    f32  innerAngle = 0.30f;
    f32  outerAngle = 0.50f;

    // Spherical area light: treat the source as a sphere of this radius rather than a
    // point. Widens the specular highlight instead of leaving a hard pinprick. 0 = point.
    f32  radius = 0.0f;
};

// The sun. Also drives the shadow map: the scene is rendered depth-only from an
// orthographic camera aimed along `direction`. It is always light 0, and it is the
// only light that casts a shadow.
struct DirectionalLight {
    Vec3 direction{-0.4f, -1.0f, -0.5f};   // direction the light travels (world)
    Vec3 color{1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;
    Vec3 ambient{1.0f, 1.0f, 1.0f};        // IBL intensity/tint (ambient from cubemaps)

    // Shadow frustum: an ortho box of half-extent `shadowExtent` centred on
    // `shadowTarget`, with the light placed `shadowDistance` back along -dir.
    Vec3 shadowTarget{0.0f, 0.0f, 0.0f};
    f32  shadowExtent   = 16.0f;
    f32  shadowDistance = 40.0f;
};

// Depth-map artefacts are a bias trade-off: too little and a surface shadows itself
// in stripes (acne), too much and the shadow floats away from its caster (peter-panning).
struct ShadowSettings {
    bool enabled    = true;
    f32  depthBias  = 0.0015f;   // scaled by the surface's slope to the light
    f32  normalBias = 0.0004f;   // constant floor, applied even head-on
    i32  pcfRadius  = 1;         // kernel half-width in texels; 1 => 3x3, 0 => hard edges
};

struct Fog {
    enum class Mode : u32 { Off = 0, Linear = 1, Exp = 2, Exp2 = 3 };

    Mode mode = Mode::Off;
    Vec3 color{0.55f, 0.62f, 0.72f};
    f32  density = 0.02f;        // Exp/Exp2
    f32  start   = 10.0f;        // Linear
    f32  end     = 80.0f;        // Linear
    // > 0 thins the fog out with height, which is what makes it read as atmosphere
    // pooling in a valley rather than a flat grey wash over the whole image.
    f32  heightFalloff = 0.0f;
};

// Screen-space ambient occlusion. Only the deferred path can do this — it needs the
// scene's depth and normals as textures, which is precisely what a G-buffer is.
struct Ssao {
    bool enabled   = false;
    f32  radius    = 0.5f;   // world units sampled around each pixel
    f32  intensity = 1.0f;   // how hard the occlusion darkens ambient
    f32  bias      = 0.02f;  // depth slack, to stop a surface occluding itself
    f32  power     = 1.5f;   // > 1 deepens the contacts and lifts the open areas
};

// Everything about the world (as opposed to the surfaces in it) for one frame.
struct SceneLighting {
    DirectionalLight   sun;
    std::vector<Light> lights;   // extra punctual lights; up to 15 alongside the sun
    ShadowSettings     shadow;
    Fog                fog;
    Ssao               ssao;
    f32                skyboxIntensity = 1.0f;
};

inline constexpr u32 kMaxLights = 16;

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

using MeshHandle     = Handle<struct MeshTag>;
using MaterialHandle = Handle<struct MaterialTag>;

// A metallic-roughness PBR material. Every texture is optional: a slot left invalid
// is filled with a 1x1 neutral texture, so the shader always samples all five and
// the factor below it is what you end up seeing.
struct MaterialDesc {
    rhi::TextureHandle albedo{};
    rhi::TextureHandle normalMap{};
    rhi::TextureHandle metallicRoughness{};   // G = roughness, B = metallic (glTF layout)
    rhi::TextureHandle emissive{};
    rhi::TextureHandle occlusion{};           // R = ambient occlusion
    rhi::SamplerHandle sampler{};             // default: linear + repeat

    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    f32  metallic  = 0.0f;
    f32  roughness = 0.5f;
    Vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    f32  emissiveStrength = 0.0f;   // HDR: > 1 makes the surface bloom
    f32  normalScale      = 1.0f;
    f32  occlusionStrength = 1.0f;
    f32  alphaCutoff = 0.0f;        // > 0 discards fragments below it (masked cutout)
    f32  uvScale     = 1.0f;        // tiles every map at once

    bool           unlit       = false;   // emit baseColor directly; for lines/gizmos
    rhi::BlendMode blend       = rhi::BlendMode::Opaque;
    bool           doubleSided = false;
};

struct MeshInstance {
    MeshHandle mesh;
    Mat4       model = Mat4::identity();

    // Tints the material's base colour (and is the base colour outright when no
    // material is set). Alpha drives transparency for a blended material.
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    // Used only when `material` is unset — the quick path for an untextured surface.
    f32 metallic  = 0.0f;
    f32 roughness = 0.5f;

    MaterialHandle material{};

    bool castsShadow    = true;
    bool receivesShadow = true;
};

// ---------------------------------------------------------------------------
// Mesh data + primitives
// ---------------------------------------------------------------------------

struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<u32>        indices;

    // Derive a tangent frame per vertex from the UVs. The primitives below already
    // call this; a custom mesh needs it before it can take a normal map.
    void computeTangents();
    // Paint every vertex, e.g. to feed the vertex-colour path without a texture.
    void setColor(Vec4 c);
};

[[nodiscard]] MeshData makeCube(f32 size = 1.0f);
[[nodiscard]] MeshData makePlane(f32 size = 1.0f);                        // XZ, faces +Y
[[nodiscard]] MeshData makeQuad(f32 width = 1.0f, f32 height = 1.0f);     // XY, faces +Z
[[nodiscard]] MeshData makeSphere(u32 rings = 16, u32 sectors = 24, f32 radius = 0.5f);
[[nodiscard]] MeshData makeCylinder(f32 radius = 0.5f, f32 height = 1.0f, u32 sectors = 24);
[[nodiscard]] MeshData makeCone(f32 radius = 0.5f, f32 height = 1.0f, u32 sectors = 24);
[[nodiscard]] MeshData makeTorus(f32 radius = 0.5f, f32 tubeRadius = 0.2f,
                                 u32 rings = 24, u32 sides = 16);
[[nodiscard]] MeshData makeCapsule(f32 radius = 0.35f, f32 height = 1.0f,
                                   u32 rings = 8, u32 sectors = 20);

// ---------------------------------------------------------------------------
// Ray casting
// ---------------------------------------------------------------------------

struct RayHit {
    bool  hit      = false;
    f32   distance = 0.0f;
    Vec3  point;
    Vec3  normal;                  // world space, from the triangle that was hit
    u32   triangle = 0;            // index of the triangle within its mesh
    usize instance = 0;            // index into the instances submitted this frame

    explicit operator bool() const { return hit; }
};

class MeshRenderer {
public:
    MeshRenderer(rhi::IGraphicsDevice& device, rhi::Format colorFormat, rhi::Format depthFormat);
    ~MeshRenderer();
    MeshRenderer(const MeshRenderer&)            = delete;
    MeshRenderer& operator=(const MeshRenderer&) = delete;

    [[nodiscard]] MeshHandle createMesh(const MeshVertex* vertices, usize vertexCount,
                                        const u32* indices, usize indexCount);
    [[nodiscard]] MeshHandle createMesh(const MeshData& data) {
        return createMesh(data.vertices.data(), data.vertices.size(),
                          data.indices.data(), data.indices.size());
    }
    void destroyMesh(MeshHandle);

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc&);
    void destroyMaterial(MaterialHandle);

    void begin(const Camera& camera, const SceneLighting& scene);
    // Shorthand for a scene lit by nothing but the sun.
    void begin(const Camera& camera, const DirectionalLight& sun) {
        SceneLighting scene;
        scene.sun = sun;
        begin(camera, scene);
    }

    void drawMesh(MeshHandle, const Mat4& model, Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});
    void submit(const MeshInstance&);
    void submit(const MeshInstance* items, usize count);

    // The depth texture the lit pass will read shadows from. Call once per frame,
    // after the graph has declared the target and before end()/renderSkybox().
    // Leave it unset to render unshadowed.
    void setShadowMap(rhi::TextureHandle);

    // Depth-only pass from the light's point of view; fills the shadow map.
    void renderShadow(rhi::ICommandList& cmd);

    // The environment cubemap as the background, at the far plane. Record it inside
    // the scene pass, before end(), so it only costs the pixels nothing covers.
    void renderSkybox(rhi::ICommandList& cmd);

    // Main lit pass: opaque instances first, then blended ones back-to-front.
    void end(rhi::ICommandList& cmd);

    // --- Deferred path -----------------------------------------------------
    //
    // Forward shading pays for lighting once per fragment *drawn*, so overlapping
    // geometry pays repeatedly. Deferred writes the surface into a G-buffer, then
    // lights each screen pixel exactly once — the win grows with light count and
    // overdraw. Blended surfaces cannot be deferred (a G-buffer pixel holds one
    // surface), so they still go through the forward path afterwards.
    //
    // The targets a G-buffer pass must declare, in attachment order:
    static constexpr rhi::Format kGBufferAlbedoFormat   = rhi::Format::R8G8B8A8_UNORM;
    static constexpr rhi::Format kGBufferNormalFormat   = rhi::Format::R16G16B16A16_SFLOAT;
    static constexpr rhi::Format kGBufferEmissiveFormat = rhi::Format::R16G16B16A16_SFLOAT;

    // Fill the G-buffer: opaque instances only, no lighting.
    void renderGBuffer(rhi::ICommandList& cmd);

    // Hand over the filled G-buffer (textures from the render graph). Call between
    // the G-buffer pass and the lighting pass.
    void setGBuffer(rhi::TextureHandle albedo, rhi::TextureHandle normal,
                    rhi::TextureHandle emissive, rhi::TextureHandle depth);

    // The AO target's format. Single-channel would do, but the graph only hands out
    // colour targets, and this one is already the cheapest of those.
    static constexpr rhi::Format kSsaoFormat = rhi::Format::R8G8B8A8_UNORM;

    // Compute raw (noisy) ambient occlusion from the G-buffer's depth and normals.
    // Fullscreen; record after the G-buffer pass. Needs SceneLighting::ssao enabled.
    void renderSSAO(rhi::ICommandList& cmd);

    // Smooth the raw AO. `rawAo` is the sampling bind group for what renderSSAO wrote,
    // and the size is that target's, so the blur knows its texel step.
    void renderSSAOBlur(rhi::ICommandList& cmd, rhi::BindGroupHandle rawAo,
                        u32 width, u32 height);

    // Hand the blurred AO to the lighting pass. Call before renderDeferredLighting();
    // skip it (or pass an invalid handle) and ambient goes unoccluded.
    void setAmbientOcclusion(rhi::TextureHandle ao);

    // Light every pixel the G-buffer covers, and paint the sky into the rest.
    // A fullscreen pass — needs no depth attachment.
    void renderDeferredLighting(rhi::ICommandList& cmd);

    // This frame's clip space -> last frame's clip space. Feed it to
    // PostProcess::addMotionBlur(), which uses it to find where each pixel came from.
    // Valid after begin(); on the first frame it is the identity (no motion).
    [[nodiscard]] Mat4 reprojection() const;

    // Forward pass for the blended instances the G-buffer had to skip. Record it
    // into a pass that loads the lit colour and the G-buffer's depth.
    void endTransparent(rhi::ICommandList& cmd);

    // Nearest hit against the instances submitted this frame. CPU-side, against the
    // mesh's triangles — exact, not a bounding-box guess. Valid after submit().
    [[nodiscard]] RayHit rayCast(const Ray&) const;
    [[nodiscard]] RayHit rayCastMesh(MeshHandle, const Mat4& model, const Ray&) const;

    // World -> light clip space, valid after begin(); use it to size the shadow pass.
    [[nodiscard]] const Mat4& lightViewProj() const { return m_frameData.lightViewProj; }

    [[nodiscard]] u32 drawCallCount() const { return m_drawCalls; }

private:
    struct GpuMesh {
        rhi::BufferHandle vbo;
        rhi::BufferHandle ibo;
        u32               indexCount = 0;
        bool              alive      = false;
        MeshData          cpu;   // kept for ray casting
    };

    struct GpuMaterial {
        rhi::BindGroupHandle bindGroup;
        rhi::SamplerHandle   ownedSampler;   // only set when we made the default one
        MaterialDesc         desc;
        bool                 alive = false;
    };

    struct GpuLight {
        Vec4 position;
        Vec4 direction;
        Vec4 color;
        Vec4 params;
    };

    // Mirrors the `Frame` block in mesh.vert/mesh.frag (std140).
    struct FrameUBO {
        Mat4     viewProj;
        Mat4     lightViewProj;
        Vec4     cameraPos;
        Vec4     ambient;
        Vec4     fogColor;
        Vec4     fogParams;
        Vec4     shadowParams;
        Vec4     misc;
        GpuLight lights[kMaxLights];
    };

    struct SkyUBO {
        Mat4 invViewProj;
        Vec4 cameraPos;
        Vec4 params;
    };

    // 128 bytes — the guaranteed Vulkan push-constant budget, exactly.
    struct Push {
        Mat4 model;
        Vec4 color;
        Vec4 material;   // metallic, roughness, normalScale, occlusionStrength
        Vec4 emissive;   // rgb, strength
        Vec4 params;     // alphaCutoff, uvScale, unlit, receivesShadow
    };

    struct DeferredPush {
        Mat4 invViewProj;
        Vec4 params;     // skyboxIntensity, materialAoStrength, ssaoEnabled, 0
    };

    struct SsaoPush {
        Mat4 invViewProj;
        Vec4 params;     // radius, intensity, bias, power
    };

    struct BlurPush {
        Vec4 params;     // texel size xy
    };

    // One pipeline per (blend mode, sidedness) pair, built on first use.
    struct PipelineKey {
        rhi::BlendMode blend       = rhi::BlendMode::Opaque;
        bool           doubleSided = false;
    };
    [[nodiscard]] rhi::PipelineHandle pipelineFor(PipelineKey);

    [[nodiscard]] rhi::TextureHandle defaultTexture(Vec4 rgba);
    void rebuildSceneBindGroup();
    void recordInstance(rhi::ICommandList& cmd, const MeshInstance&);
    void classify();   // split the submitted instances into opaque and blended
    void drawList(rhi::ICommandList& cmd, const std::vector<u32>& list,
                  rhi::PipelineHandle forced);

    rhi::IGraphicsDevice& m_device;
    rhi::Format           m_colorFormat;
    rhi::Format           m_depthFormat;

    struct CachedPipeline { PipelineKey key; rhi::PipelineHandle pipeline; };
    std::vector<CachedPipeline> m_pipelines;
    rhi::PipelineHandle         m_shadowPipeline;
    rhi::PipelineHandle         m_skyboxPipeline;
    rhi::PipelineHandle         m_gbufferPipeline;
    rhi::PipelineHandle         m_deferredPipeline;
    rhi::PipelineHandle         m_ssaoPipeline;
    rhi::PipelineHandle         m_ssaoBlurPipeline;

    // The filled G-buffer, bound through the PBR material set layout. Cached the
    // same way the scene set is: one bind group per frame-in-flight, then stable.
    // The SSAO pass reads the same set, so it needs no group of its own — but the
    // lighting group also carries the AO map, which SSAO is still writing, so the two
    // cannot share one group without reading a target they are in the middle of filling.
    rhi::BindGroupHandle m_gbufferBindGroup;   // + AO: for the lighting pass
    rhi::BindGroupHandle m_ssaoBindGroup;      // no AO: for the SSAO pass
    rhi::TextureHandle   m_gbufferAlbedo, m_gbufferNormal, m_gbufferEmissive, m_gbufferDepth;
    rhi::TextureHandle   m_aoMap;
    struct GBufferCache {
        rhi::TextureHandle   albedo, normal, emissive, depth, ao;
        rhi::BindGroupHandle group;
    };
    std::vector<GBufferCache> m_gbufferCache;
    [[nodiscard]] rhi::BindGroupHandle gbufferGroup(rhi::TextureHandle ao);

    rhi::BufferHandle    m_uniformBuffers[rhi::kMaxFramesInFlight];
    rhi::BindGroupHandle m_uniformBindGroups[rhi::kMaxFramesInFlight];
    rhi::BufferHandle    m_skyBuffers[rhi::kMaxFramesInFlight];
    rhi::BindGroupHandle m_skyBindGroups[rhi::kMaxFramesInFlight];

    // Image-based lighting: a procedural environment + its diffuse irradiance,
    // built once on the CPU and uploaded as cubemaps.
    rhi::TextureHandle   m_envMap;
    rhi::TextureHandle   m_irradiance;
    rhi::SamplerHandle   m_iblSampler;

    // The scene set (IBL + shadow map). Rebuilt whenever the shadow texture changes,
    // which in practice is once per frame-in-flight, then never again.
    rhi::SamplerHandle   m_shadowSampler;
    rhi::TextureHandle   m_shadowMap;          // the current frame's, or the dummy
    rhi::TextureHandle   m_dummyShadow;        // 1x1 white => "nothing is shadowed"
    rhi::BindGroupHandle m_sceneBindGroup;
    struct SceneCache { rhi::TextureHandle tex; rhi::BindGroupHandle group; };
    std::vector<SceneCache> m_sceneCache;

    // The five neutral 1x1 maps that stand in for whatever a material omits.
    rhi::TextureHandle   m_white;
    rhi::TextureHandle   m_flatNormal;
    rhi::TextureHandle   m_black;
    rhi::SamplerHandle   m_defaultSampler;
    MaterialHandle       m_defaultMaterial;

    u32 m_frame = 0;

    std::vector<GpuMesh>      m_meshes;
    std::vector<GpuMaterial>  m_materials;
    std::vector<MeshInstance> m_instances;
    std::vector<u32>          m_opaque;        // indices into m_instances
    std::vector<u32>          m_blended;
    FrameUBO                  m_frameData{};
    SkyUBO                    m_skyData{};
    DeferredPush              m_deferredPush{};
    SsaoPush                  m_ssaoPush{};
    Mat4                      m_prevViewProj = Mat4::identity();
    bool                      m_hasPrevFrame = false;
    Vec3                      m_cameraPos{};
    bool                      m_classified = false;
    u32                       m_drawCalls  = 0;
};

}
