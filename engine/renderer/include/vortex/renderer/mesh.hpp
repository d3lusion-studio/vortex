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

#include <string>
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
    // The lightmap's own UV set. A lightmap needs an unwrap where no two triangles overlap
    // — every texel must belong to exactly one point on the surface — which is a different
    // requirement from the tiling, repeating UVs a brick texture wants. So it is a second
    // set, not the first one reused. The primitives below fill it with `uv`, which is
    // already non-overlapping for a plane; a real mesh needs a real unwrap.
    Vec2 uv1{0.0f, 0.0f};
    // Tangent frame for normal mapping: xyz = tangent, w = bitangent handedness.
    // The default is a valid frame, so a mesh built without tangents still shades.
    Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    // Skinning: up to four joints move this vertex, by these weights. Weights of all zero
    // mean "not skinned" — the vertex shader then leaves the vertex where it is, which is
    // what every non-skinned mesh in the scene relies on.
    u8   joints[4]{0, 0, 0, 0};
    Vec4 weights{0.0f, 0.0f, 0.0f, 0.0f};
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
//
// The shadow frustum is not configured here — it is fitted to the camera's own view
// every frame (see ShadowSettings), so it follows the player around without anyone
// having to nominate a region of the world in advance.
struct DirectionalLight {
    Vec3 direction{-0.4f, -1.0f, -0.5f};   // direction the light travels (world)
    Vec3 color{1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;
    Vec3 ambient{1.0f, 1.0f, 1.0f};        // IBL intensity/tint (ambient from cubemaps)
};

inline constexpr u32 kMaxCascades = 4;

// Depth-map artefacts are a bias trade-off: too little and a surface shadows itself
// in stripes (acne), too much and the shadow floats away from its caster (peter-panning).
struct ShadowSettings {
    bool enabled    = true;
    f32  depthBias  = 0.0015f;   // scaled by the surface's slope to the light
    f32  normalBias = 0.0004f;   // constant floor, applied even head-on
    i32  pcfRadius  = 1;         // kernel half-width in texels; 1 => 3x3, 0 => hard edges

    // Cascaded shadow maps. One shadow map stretched over the whole view has to choose
    // between covering the distance and resolving anything up close; cascades cut the
    // view into `cascadeCount` depth slices and give each its own map, so the slice at
    // your feet gets the same texels as the one at the horizon. 1 = the single-map path.
    //
    // All cascades share one depth texture, tiled 2x2 — the RHI has no texture arrays,
    // and an atlas needs none.
    u32 cascadeCount = 1;
    f32 maxDistance  = 60.0f;   // how far from the camera shadows are drawn at all
    // Where the splits fall between an even spread (0) and a logarithmic one (1).
    // Logarithmic matches how perspective compresses distance; a little of it goes far.
    f32 splitLambda  = 0.75f;

    // Side of the (square) shadow texture. Must match the target handed to setShadowMap:
    // the renderer needs it to place the cascades in the atlas and to snap them to texels.
    u32 resolution = 2048;
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

// A shadow map's texel is metres wide; the contact between an object and the floor it
// rests on is millimetres. Marching the depth buffer toward the light for a short way
// recovers exactly that detail, and only that — this is a supplement to the shadow map,
// not a replacement. Deferred only: it needs the depth buffer as a texture.
struct ContactShadows {
    bool enabled  = false;
    f32  distance = 0.25f;   // world units to march (keep it short — it is a detail pass)
    i32  steps    = 12;
    f32  thickness = 0.5f;   // how far behind a surface the ray still counts as blocked
};

// Light made visible in the air itself: march the view ray, ask the shadow map whether
// each step is lit, and accumulate what scatters toward the camera. This is what draws
// god rays through a gap. Deferred only.
struct VolumetricFog {
    bool enabled   = false;
    f32  density   = 0.06f;   // how much light scatters per unit of air
    i32  steps     = 48;
    f32  maxDistance = 40.0f;
    // Mie anisotropy: 0 scatters evenly, values toward 1 throw the light forward, which
    // is what makes a beam blaze when you look into it and stay faint when you don't.
    f32  anisotropy = 0.6f;
    Vec3 color{1.0f, 0.98f, 0.92f};
};

// A decal is a box of space, not a piece of geometry. Whatever surface the G-buffer holds
// inside the box gets the texture projected onto it — so a scorch mark lands on the floor,
// the wall and the step between them at once, with nothing to UV-unwrap and no z-fighting
// decal quad hovering a millimetre above the ground.
//
// The box is a unit cube, transformed by `model`; the decal projects down the box's local
// -Y, and its texture is mapped across the box's local XZ. Deferred only: it is stamped
// into the G-buffer's albedo before any lighting happens, so it lights like the surface
// it landed on.
struct Decal {
    Mat4               model = Mat4::identity();
    rhi::TextureHandle texture;
    f32                opacity = 1.0f;
    // Surfaces more side-on than this to the projection axis are skipped, or the texture
    // would stretch down them in the streak every decal system is known for.
    f32                angleFade = 0.3f;   // cosine; 0 accepts everything
};

// Everything about the world (as opposed to the surfaces in it) for one frame.
struct SceneLighting {
    DirectionalLight   sun;
    std::vector<Light> lights;   // extra punctual lights; up to 15 alongside the sun
    ShadowSettings     shadow;
    Fog                fog;
    Ssao               ssao;
    ContactShadows     contactShadows;
    VolumetricFog      volumetric;
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
    // Baked indirect light, sampled with the mesh's SECOND UV set. Where a lightmap is
    // present it REPLACES the image-based ambient rather than adding to it: it already
    // accounts for the sky, and counting both would light the scene twice.
    rhi::TextureHandle lightmap{};
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

    // Parallax occlusion mapping. A normal map fakes the *lighting* of relief but leaves
    // the surface flat, so it flattens out the moment you look along it. This walks the
    // view ray through a height field and shifts the UV to where the ray actually meets
    // the surface, so bricks occlude their own mortar and the relief holds at a glancing
    // angle. The height field is the **alpha channel of the normal map** (1 = the top of
    // the surface, 0 = the bottom of the grooves).
    //
    // 0 disables it — and disabled is the default, because a material whose normal map
    // has no meaningful alpha would otherwise get its UVs shifted by nonsense.
    f32  parallaxScale  = 0.0f;   // depth of the relief, in UV units. 0.02-0.08 is sane.
    i32  parallaxLayers = 24;     // ray-march steps. More = fewer stair-steps at grazing angles.

    // Transmission: how much light passes *through* the surface rather than bouncing off
    // it — glass, water, gems. The lit scene behind the surface is sampled and refracted
    // through the normal. Needs a blended material and MeshRenderer::setSceneColor().
    f32  transmission = 0.0f;   // 0 = opaque surface, 1 = fully see-through
    f32  ior          = 1.5f;   // index of refraction; 1.5 is window glass, 1.33 water

    // 0 = the material has no lightmap and takes its ambient from the environment.
    f32  lightmapIntensity = 0.0f;

    bool           unlit       = false;   // emit baseColor directly; for lines/gizmos
    rhi::BlendMode blend       = rhi::BlendMode::Opaque;
    bool           doubleSided = false;
};

struct MeshInstance {
    MeshHandle mesh;
    Mat4       model = Mat4::identity();

    // Where this instance was last frame. Motion blur needs it to know that an object
    // moved on its own rather than under the camera. Leave `hasPrevModel` false and the
    // instance is treated as stationary — its blur then comes from the camera alone.
    Mat4       prevModel    = Mat4::identity();
    bool       hasPrevModel = false;

    // Tints the material's base colour (and is the base colour outright when no
    // material is set). Alpha drives transparency for a blended material.
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    // Used only when `material` is unset — the quick path for an untextured surface.
    f32 metallic  = 0.0f;
    f32 roughness = 0.5f;

    MaterialHandle material{};

    bool castsShadow    = true;
    bool receivesShadow = true;

    // Skinning matrices for this instance, one per joint of its skeleton — the pose, already
    // combined with the inverse bind (anim::Skeleton::computeSkinningMatrices). Null means the
    // mesh is drawn as authored.
    //
    // A borrowed pointer, valid until the next begin(): the renderer copies these into a GPU
    // buffer during submit and never looks at them again.
    const Mat4* bones     = nullptr;
    u32         boneCount = 0;

    // How much of each of the mesh's morph targets to apply, in the order they were created.
    // Also borrowed until the next begin().
    const f32* morphWeights = nullptr;
    u32        morphCount   = 0;
};

// ---------------------------------------------------------------------------
// Mesh data + primitives
// ---------------------------------------------------------------------------

// A morph target is the mesh AS A DIFFERENCE: what to add to each vertex to get from the
// neutral shape to this one. Deltas, not absolute positions — so a target that only moves the
// mouth is zero everywhere else, and blending several of them at once (a smile AND a blink) is
// just a weighted sum, which is exactly what an expression is.
struct MorphTarget {
    std::string       name;
    std::vector<Vec3> positions;   // one per vertex; the delta from the neutral shape
    std::vector<Vec3> normals;     // may be empty
};

struct MeshData {
    std::vector<MeshVertex>  vertices;
    std::vector<u32>         indices;
    std::vector<MorphTarget> morphTargets;

    // Derive a tangent frame per vertex from the UVs. The primitives below already
    // call this; a custom mesh needs it before it can take a normal map.
    void computeTangents();
    // Paint every vertex, e.g. to feed the vertex-colour path without a texture.
    void setColor(Vec4 c);

    // Use the texture UVs as the lightmap unwrap. Correct only where those UVs already
    // cover the surface exactly once — true of a plane or a quad, false of a cube (whose
    // six faces all map onto the same 0..1 square, so they would share lightmap texels).
    void copyUVToLightmapUV();
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
                                        const u32* indices, usize indexCount,
                                        bool dynamic = false);
    // The MeshData overload is the one that carries morph targets: they cannot be passed as
    // loose pointers without also passing their count and their stride, and at that point it is
    // a struct.
    [[nodiscard]] MeshHandle createMesh(const MeshData& data, bool dynamic = false);

    // Rewrite a dynamic mesh's vertices. The buffer is host-visible, so this is a memcpy —
    // which is what makes CPU skinning viable at all. Creating the mesh without `dynamic`
    // puts it in device memory, where this would have to stage a copy every frame instead.
    void updateMesh(MeshHandle, const MeshVertex* vertices, usize vertexCount);

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

    // Depth-only pass from the light's point of view; fills the shadow map. With more
    // than one cascade the map is an atlas, and this renders each cascade into its own
    // quadrant — so it sets the viewport itself, from ShadowSettings::resolution.
    void renderShadow(rhi::ICommandList& cmd);

    // The environment cubemap as the background, at the far plane. Record it inside
    // the scene pass, before end(), so it only costs the pixels nothing covers.
    void renderSkybox(rhi::ICommandList& cmd);

    // Main lit pass: opaque instances first, then blended ones back-to-front.
    void end(rhi::ICommandList& cmd);

    // The opaque half of the forward pass, on its own. Split the two when a transmissive
    // material is in play: the blended surfaces need the opaque scene as a texture, and
    // it cannot be copied out from inside the pass that is still writing it.
    void endOpaque(rhi::ICommandList& cmd);

    // Copy the lit scene so transmissive surfaces can refract it. `source` is the
    // sampling bind group for the target holding the opaque scene.
    void renderSceneCopy(rhi::ICommandList& cmd, rhi::BindGroupHandle source);

    // Hand over that copy. Call before the transparent pass; without it, a transmissive
    // material sees black behind itself.
    void setSceneColor(rhi::TextureHandle);

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
    // Screen-space motion since the last frame. Signed, so it needs a float format.
    static constexpr rhi::Format kGBufferVelocityFormat = rhi::Format::R16G16B16A16_SFLOAT;

    // Fill the G-buffer: opaque instances only, no lighting.
    void renderGBuffer(rhi::ICommandList& cmd);

    // Queue a decal for this frame. Cleared by begin(), like the mesh instances.
    void submitDecal(const Decal&);

    // Stamp the queued decals into the G-buffer's albedo. Record this AFTER the G-buffer
    // pass and BEFORE the lighting pass, into a pass that writes the albedo target (with
    // LoadOp::Load) and samples the depth target.
    void renderDecals(rhi::ICommandList& cmd);

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

    // Light scattering in the air between the camera and each surface, marched against
    // the shadow map. Additive over the lit scene; record it after the lighting pass.
    // Needs SceneLighting::volumetric enabled and the G-buffer handed over.
    void renderVolumetric(rhi::ICommandList& cmd);

    // Forward pass for the blended instances the G-buffer had to skip. Record it
    // into a pass that loads the lit colour and the G-buffer's depth.
    void endTransparent(rhi::ICommandList& cmd);

    // --- Lightmap baking ---------------------------------------------------
    //
    // A lightmap trades time now for light later: the sun's shadowing and the sky's
    // occlusion are integrated once, offline, per texel of a static surface — and then
    // cost a single texture fetch per frame no matter how many bounces went into them.
    // It is the only way to get soft, occluded ambient light that does not have to be
    // re-derived every frame, and the reason it works at all is that none of it moves.
    //
    // The bake rasterises `target`'s triangles into its SECOND UV set, and for each texel
    // it lands on, shoots rays at the world: one at the sun (is this point shadowed?) and
    // `skySamples` over the hemisphere (how much sky can it see?). Everything in
    // `occluders` blocks those rays, so a lightmap knows about geometry that is nowhere
    // near it — which is exactly what a shadow map cannot afford to.
    //
    // Returns RGBA8 pixels, ready for createTexture(). This is slow and meant to run once,
    // at load — not per frame.
    struct LightmapBake {
        u32  resolution = 128;
        u32  skySamples = 64;    // hemisphere rays per texel; the cost is nearly all here
        f32  skyIntensity = 1.0f;
        Vec3 skyColor{0.55f, 0.65f, 0.85f};
        f32  rayBias = 0.005f;   // lift the ray off the surface, or it hits the surface

        // Bake the SUN's direct light in as well. Off by default, and think before turning
        // it on: the real-time pass lights the same surface with the same sun, so a bake
        // that includes it counts that light twice — the floor goes flat and bright and the
        // cast shadows wash out of it. Baked light is meant to be the light the real-time
        // pass CANNOT compute: the sky's occlusion, the bounce off a nearby wall.
        //
        // Turn it on only for a surface whose real-time direct light you have disabled.
        bool directSun = false;
    };
    [[nodiscard]] std::vector<u8> bakeLightmap(const MeshInstance& target,
                                               const MeshInstance* occluders, usize occluderCount,
                                               const DirectionalLight& sun,
                                               const LightmapBake& settings) const;

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
        u32               indexCount  = 0;
        u32               vertexCount = 0;
        bool              dynamic     = false;   // host-visible: updateMesh() may rewrite it
        bool              alive       = false;
        // Where this mesh's morph deltas start in the shared morph buffer, and how many
        // targets it has. Every mesh's targets live in one buffer; a mesh is an offset into it.
        u32               morphBase    = 0;
        u32               morphTargets = 0;
        MeshData          cpu;   // kept for ray casting (and CPU morphing)
    };

    // One vertex's delta for one target. vec4s, not vec3s: std430 would pad a vec3 to 16 bytes
    // anyway, and pretending otherwise is how a buffer silently reads one field to the left.
    struct GpuMorphDelta {
        Vec4 position;
        Vec4 normal;
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
        Mat4     lightViewProj;      // cascade 0, kept for the forward path's vertex stage
        Vec4     cameraPos;
        Vec4     ambient;
        Vec4     fogColor;
        Vec4     fogParams;
        Vec4     shadowParams;       // depthBias, normalBias, pcfRadius, enabled
        Vec4     misc;               // lightCount, cascadeCount, 0, 0
        Vec4     contactParams;      // enabled, distance, steps, thickness
        Vec4     cascadeSplits;      // view-space distance at which each cascade ends
        Vec4     cascadeTexelWorld;  // world units one shadow texel spans, per cascade
        Mat4     cascadeViewProj[kMaxCascades];
        GpuLight lights[kMaxLights];
        Mat4     prevViewProj;   // last frame's, for per-object velocity
        Mat4     invViewProj;    // clip -> world; decals rebuild a surface's position with it
    };

    // Per-instance data the vertex stage looks up by gl_InstanceIndex. Only what push
    // constants cannot hold lives here — the model matrix this instance had LAST frame,
    // which is 64 bytes the 128-byte push block has no room for.
    struct GpuInstance {
        Mat4 prevModel;
        // x = lightmap intensity (0 = none)
        // y = this instance's first bone in the bone buffer
        // z = how many bones it has (0 = not skinned)
        // w = where its mesh's morph deltas start in the morph buffer
        // The push block is at its 128-byte ceiling, and this is the buffer that exists
        // precisely for what will not fit there.
        Vec4 params;
        // x = how many morph targets are active (0 = none), y = the mesh's vertex count
        Vec4 morphInfo;
        // Up to eight target weights. Eight is not a law of nature — it is what fits in two
        // vec4s, and a face rig that needs more should be blending its expressions down to a
        // handful before it gets here.
        Vec4 morphWeights[2];
    };

    struct SkyUBO {
        Mat4 invViewProj;
        Vec4 cameraPos;
        Vec4 params;
    };

    // 128 bytes — the guaranteed Vulkan push-constant budget, exactly, and it was full.
    //
    // The model matrix is stored as its first three ROWS, not as a Mat4: the fourth row
    // of an affine transform is always (0,0,0,1), so storing it costs 16 bytes to say
    // nothing. Dropping it is what paid for `extra`.
    struct Push {
        Vec4 modelRow0;
        Vec4 modelRow1;
        Vec4 modelRow2;
        Vec4 color;
        Vec4 material;   // metallic, roughness, normalScale, occlusionStrength
        Vec4 emissive;   // rgb, strength
        Vec4 params;     // alphaCutoff, uvScale, unlit, receivesShadow
        Vec4 extra;      // parallaxScale, parallaxLayers, transmission, ior
    };

    // 80 bytes. The shadow pass carries the light matrix and just enough of the material to
    // run the same alpha test the lit pass runs.
    struct ShadowPush {
        Mat4 lightMvp;
        Vec4 params;   // alphaCutoff, uvScale, 0, 0
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

    struct VolumePush {
        Mat4 invViewProj;
        Vec4 params;     // density, steps, maxDistance, anisotropy
        Vec4 color;
    };

    // One pipeline per (blend mode, sidedness) pair, built on first use.
    struct PipelineKey {
        rhi::BlendMode blend       = rhi::BlendMode::Opaque;
        bool           doubleSided = false;
    };
    [[nodiscard]] rhi::PipelineHandle pipelineFor(PipelineKey);

    [[nodiscard]] rhi::TextureHandle defaultTexture(Vec4 rgba);
    void rebuildSceneBindGroup();
    void recordInstance(rhi::ICommandList& cmd, const MeshInstance&, u32 instanceIndex);
    void buildCascades(const Camera&, const DirectionalLight&, const ShadowSettings&);
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
    rhi::PipelineHandle         m_volumetricPipeline;
    rhi::PipelineHandle         m_blitPipeline;
    rhi::PipelineHandle         m_decalPipeline;
    MeshHandle                  m_unitCube;   // the volume every decal is drawn as

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
    // Per-instance data, one buffer per frame in flight, grown as the scene needs.
    rhi::BufferHandle    m_instanceBuffers[rhi::kMaxFramesInFlight];
    u32                  m_instanceCapacity = 0;
    std::vector<GpuInstance> m_instanceData;

    // Every skinned instance's bones, end to end. One buffer for the whole frame: a hundred
    // characters is a hundred offsets into it, not a hundred buffers.
    rhi::BufferHandle    m_boneBuffers[rhi::kMaxFramesInFlight];
    u32                  m_boneCapacity = 0;
    std::vector<Mat4>    m_boneData;

    // Every mesh's morph deltas, uploaded once at mesh creation and never touched again — the
    // shapes do not change, only the weights do. That is the whole reason morphing is cheap.
    rhi::BufferHandle          m_morphBuffer;
    u32                        m_morphCapacity = 0;
    std::vector<GpuMorphDelta> m_morphData;
    void appendMorphTargets(GpuMesh&, const MeshData&);
    void rebuildFrameBindGroups();

    void ensureInstanceCapacity(u32 instances, u32 bones);
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
    rhi::TextureHandle   m_sceneColor;         // the lit scene copy, or the black dummy
    rhi::TextureHandle   m_sceneDepth;         // the G-buffer's depth, for decals
    rhi::TextureHandle   m_dummyShadow;        // 1x1 white => "nothing is shadowed"
    rhi::BindGroupHandle m_sceneBindGroup;
    struct SceneCache {
        rhi::TextureHandle   shadow, sceneColor, sceneDepth;
        rhi::BindGroupHandle group;
    };
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
    std::vector<Decal>        m_decals;
    // One bind group per decal texture, made on first use and kept.
    struct DecalCache { rhi::TextureHandle tex; rhi::BindGroupHandle group; };
    std::vector<DecalCache>   m_decalCache;
    [[nodiscard]] rhi::BindGroupHandle decalGroup(rhi::TextureHandle);

    struct DecalPush {
        Vec4 modelRow0, modelRow1, modelRow2;
        Vec4 invRow0, invRow1, invRow2;
        Vec4 params;   // opacity, angleFade, 0, 0
    };
    std::vector<u32>          m_opaque;        // indices into m_instances
    std::vector<u32>          m_blended;
    FrameUBO                  m_frameData{};
    SkyUBO                    m_skyData{};
    DeferredPush              m_deferredPush{};
    SsaoPush                  m_ssaoPush{};
    VolumePush                m_volumePush{};
    u32                       m_cascadeCount   = 1;
    u32                       m_shadowAtlasRes = 2048;
    bool                      m_volumetricOn   = false;
    Mat4                      m_prevViewProj = Mat4::identity();
    bool                      m_hasPrevFrame = false;
    Vec3                      m_cameraPos{};
    bool                      m_classified = false;
    u32                       m_drawCalls  = 0;
};

}
