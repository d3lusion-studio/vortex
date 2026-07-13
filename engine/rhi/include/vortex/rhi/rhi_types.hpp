#pragma once
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
    Format       format = Format::R8G8B8A8_UNORM;
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

    // Image-based-lighting set: two cubemaps (irradiance + prefiltered env)
    // sharing one sampler. Selected when isIblSet is true.
    bool          isIblSet = false;
    TextureHandle irradiance{};
    TextureHandle envMap{};
    SamplerHandle iblSampler{};
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
};

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
    bool                   alphaBlend  = false;  // straight-alpha blending for sprites
    bool                   additiveBlend = false;  // src + dst; used for bloom composite/particles
    bool                   hasMaterialTexture = false;  // a set: sampled image + sampler
    bool                   hasUniformBuffer   = false;  // a set: single uniform buffer (vtx+frag)
    bool                   hasIblTextures     = false;  // a set: irradiance + env cubemaps
    u32                    pushConstantSize   = 0;       // vertex-stage push constant block bytes
    bool                   depthTest    = false;
    bool                   depthWrite   = false;
    CompareOp              depthCompare = CompareOp::LessEqual;
    Format                 depthFormat  = Format::Undefined;
    u32                    sampleCount  = 1;  // MSAA samples; 1 = no multisampling.
                                              // Targets/resolve attachments are future work.
    const char*            debugName   = nullptr;
};

struct Viewport {
    f32 x = 0, y = 0;
    f32 width = 0, height = 0;
    f32 minDepth = 0.0f, maxDepth = 1.0f;
};

struct ColorAttachment {
    TextureHandle target;
    LoadOp        loadOp  = LoadOp::Clear;
    StoreOp       storeOp = StoreOp::Store;
    f32           clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct DepthAttachment {
    TextureHandle target;
    LoadOp        loadOp     = LoadOp::Clear;
    StoreOp       storeOp    = StoreOp::DontCare;
    f32           clearDepth = 1.0f;
};

struct RenderPassDesc {
    ColorAttachment color;
    DepthAttachment depth;
    u32             width  = 0;
    u32             height = 0;
    bool            secondaryContents = false;
};

struct FrameContext {
    class ICommandList* cmd        = nullptr;
    TextureHandle       backbuffer;
    u32                 width      = 0;
    u32                 height     = 0;
    bool                valid      = false;
};

}
