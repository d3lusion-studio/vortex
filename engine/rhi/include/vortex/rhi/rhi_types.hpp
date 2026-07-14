#pragma once
#include "vortex/core/math/color.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vortex::rhi {

struct BufferDesc {
    u64          size   = 0;
    BufferUsage  usage  = BufferUsage::Vertex;
    MemoryDomain domain = MemoryDomain::Upload;
    const char*  debugName = nullptr;
};

struct TextureDesc {
    u32          width  = 0;
    u32          height = 0;
    // sRGB by default, because by default a texture is a picture — and the bytes of a
    // picture are sRGB-encoded. An _SRGB format makes the GPU decode them to linear light
    // on every sample, for free, which is what the rest of the renderer expects to receive.
    // A texture that holds DATA rather than colour (a mask, a height field, packed normals)
    // must say so and ask for R8G8B8A8_UNORM, or its values will be silently curved.
    Format       format = Format::R8G8B8A8_SRGB;
    TextureUsage usage  = TextureUsage::Sampled;
    bool         cube   = false;   // create a cubemap (6 faces, sampled as samplerCube)
    const char*  debugName = nullptr;
};

struct SamplerDesc {
    Filter      minFilter = Filter::Linear;
    Filter      magFilter = Filter::Linear;
    AddressMode addressU  = AddressMode::ClampToEdge;
    AddressMode addressV  = AddressMode::ClampToEdge;
};

struct BindGroupDesc {
    TextureHandle texture{};
    SamplerHandle sampler{};

    BufferHandle  uniformBuffer{};
    u64           uniformSize = 0;

    // Per-instance data, read in the shader by gl_InstanceIndex. It rides in the same
    // set as the frame uniforms (binding 1), because a 3D pipeline has no spare set —
    // and because both are "what this draw needs to know", refreshed once per frame.
    BufferHandle  storageBuffer{};
    u64           storageSize = 0;

    // Skinning matrices for every skinned instance this frame, end to end. An instance
    // finds its own by the offset it carries in the storage buffer above.
    BufferHandle  boneBuffer{};
    u64           boneSize = 0;

    // Morph-target deltas for every mesh, end to end. A mesh is an offset into it — the shapes
    // never change, so this is uploaded once and only the weights move per frame.
    BufferHandle  morphBuffer{};
    u64           morphSize = 0;

    // Scene set: everything the lit pass needs about the world rather than about
    // the surface — the two IBL cubemaps (irradiance + environment) and the shadow
    // map. They live in one set because a 3D pipeline only gets four, and the
    // fourth is spent emulating push constants on WebGPU. Selected by isSceneSet.
    bool          isSceneSet = false;
    TextureHandle irradiance{};
    TextureHandle envMap{};
    SamplerHandle iblSampler{};
    TextureHandle shadowMap{};
    SamplerHandle shadowSampler{};
    // The lit scene, for refraction. A pass cannot sample the target it writes, so this
    // is a copy taken after the opaque pass — see MeshRenderer::setSceneColor.
    TextureHandle sceneColor{};
    // The scene's depth. A decal needs it to work out which surface its volume lands on.
    TextureHandle sceneDepth{};

    // PBR material set: the five maps of a metallic-roughness material, sharing
    // one sampler. Any handle left invalid must be filled by the caller with a
    // 1x1 neutral texture — the shader always samples all five.
    bool          isPbrMaterialSet = false;
    TextureHandle albedo{};
    TextureHandle normalMap{};
    TextureHandle metallicRoughness{};
    TextureHandle emissive{};
    TextureHandle occlusion{};
    TextureHandle lightmap{};
    SamplerHandle materialSampler{};
};

struct SwapchainDesc {
    u32         width  = 0;
    u32         height = 0;
    PresentMode present = PresentMode::Fifo;
};

struct VertexAttribute {
    u32          location = 0;
    VertexFormat format   = VertexFormat::Float3;
    u32          offset   = 0;
};

struct VertexLayout {
    u32                          stride = 0;
    std::vector<VertexAttribute> attributes;

    // Advance the buffer once per instance rather than once per vertex. A sprite
    // batch binds one such buffer and builds the quad's corners from the vertex
    // index, so the whole quad costs one record instead of four.
    bool perInstance = false;
};

// A G-buffer needs several colour targets written in one pass. Attachment 0 is the
// familiar single target; the `extra` slots hold attachments 1..N, and a slot counts
// only while it is filled, so an ordinary single-target pass ignores them entirely.
inline constexpr u32 kMaxExtraColorAttachments = 3;

struct GraphicsPipelineDesc {
    // Every pipeline carries its shaders in both languages: Vulkan consumes the SPIR-V, WebGPU
    // consumes the WGSL (browsers accept nothing else). The generated shader headers provide
    // both from a single GLSL source, so callers fill both unconditionally and the backend
    // picks. A backend given an empty blob for its language fails loudly at pipeline creation.
    std::vector<std::byte> vertexSpirv;
    std::vector<std::byte> fragmentSpirv;
    std::string            vertexWgsl;
    std::string            fragmentWgsl;
    VertexLayout           vertexLayout;
    PrimitiveTopology      topology    = PrimitiveTopology::TriangleList;
    CullMode               cull        = CullMode::None;
    Format                 colorFormat = Format::B8G8R8A8_UNORM;  // Undefined => depth-only pass
    // Formats of colour attachments 1..N, for a pipeline that writes a G-buffer.
    // Must line up with the RenderPassDesc the pipeline is used in.
    Format                 extraColorFormats[kMaxExtraColorAttachments] = {
        Format::Undefined, Format::Undefined, Format::Undefined};
    bool                   alphaBlend  = false;  // straight-alpha blending for sprites
    bool                   additiveBlend = false;  // src + dst; used for bloom composite/particles
    // Supersedes the two bools above when set to anything but Opaque. The bools stay
    // because most 2D pipelines only ever need those two modes and read better that way.
    BlendMode              blendMode   = BlendMode::Opaque;
    // Off = the pipeline writes RGB and leaves the target's alpha alone. A decal blending
    // into the G-buffer's albedo needs this: that target's alpha is the METALLIC channel,
    // and an alpha blend would drag it toward the decal's coverage, which is meaningless.
    bool                   writeAlpha  = true;
    bool                   hasMaterialTexture = false;  // a set: sampled image + sampler
    bool                   hasUniformBuffer   = false;  // a set: single uniform buffer (vtx+frag)
    bool                   hasInstanceBuffer  = false;  // adds a storage buffer to that set
    bool                   hasSceneTextures   = false;  // a set: IBL cubemaps + shadow map
    bool                   hasPbrMaterial     = false;  // a set: the five PBR maps + sampler
    u32                    pushConstantSize   = 0;       // vertex-stage push constant block bytes
    bool                   depthTest    = false;
    bool                   depthWrite   = false;
    CompareOp              depthCompare = CompareOp::LessEqual;
    Format                 depthFormat  = Format::Undefined;
    u32                    sampleCount  = 1;  // MSAA samples; 1 = no multisampling.
                                              // Targets/resolve attachments are future work.
    const char*            debugName   = nullptr;
};

// How many colour attachments a pipeline writes (attachment 0 plus any extras).
[[nodiscard]] inline u32 colorAttachmentCount(const GraphicsPipelineDesc& d) noexcept {
    if (d.colorFormat == Format::Undefined) return 0;
    u32 n = 1;
    while (n <= kMaxExtraColorAttachments && d.extraColorFormats[n - 1] != Format::Undefined) ++n;
    return n;
}

// The one blend mode a pipeline actually gets, folding the legacy bools into the enum.
[[nodiscard]] inline BlendMode effectiveBlend(const GraphicsPipelineDesc& d) noexcept {
    if (d.blendMode != BlendMode::Opaque) return d.blendMode;
    if (d.additiveBlend) return BlendMode::Additive;
    if (d.alphaBlend)    return BlendMode::Alpha;
    return BlendMode::Opaque;
}

struct Viewport {
    f32 x = 0, y = 0;
    f32 width = 0, height = 0;
    f32 minDepth = 0.0f, maxDepth = 1.0f;
};

struct ColorAttachment {
    TextureHandle target;
    LoadOp        loadOp  = LoadOp::Clear;
    StoreOp       storeOp = StoreOp::Store;

    // LINEAR light, not the hex you picked in a design tool. The backbuffer is an sRGB
    // format, so the hardware encodes whatever is written to it — including this clear —
    // and an sRGB value put here would get the curve applied to it a second time.
    // Color::fromRgb() already decodes; setClear() is the short way to say that.
    f32           clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    void setClear(const Color& c) noexcept {
        clearColor[0] = c.r; clearColor[1] = c.g; clearColor[2] = c.b; clearColor[3] = c.a;
    }
};

struct DepthAttachment {
    TextureHandle target;
    LoadOp        loadOp     = LoadOp::Clear;
    StoreOp       storeOp    = StoreOp::DontCare;
    f32           clearDepth = 1.0f;
};

struct RenderPassDesc {
    ColorAttachment color;
    ColorAttachment extra[kMaxExtraColorAttachments];
    DepthAttachment depth;
    u32             width  = 0;
    u32             height = 0;
    bool            secondaryContents = false;

    [[nodiscard]] u32 colorCount() const noexcept {
        if (!color.target.valid()) return 0;
        u32 n = 1;
        while (n <= kMaxExtraColorAttachments && extra[n - 1].target.valid()) ++n;
        return n;
    }
};

struct FrameContext {
    class ICommandList* cmd        = nullptr;
    TextureHandle       backbuffer;
    u32                 width      = 0;
    u32                 height     = 0;
    bool                valid      = false;
};

}
