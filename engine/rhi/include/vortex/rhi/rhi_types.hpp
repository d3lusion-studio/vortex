#pragma once
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <cstddef>
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
    const char*  debugName = nullptr;
};

struct SamplerDesc {
    Filter      minFilter = Filter::Linear;
    Filter      magFilter = Filter::Linear;
    AddressMode addressU  = AddressMode::ClampToEdge;
    AddressMode addressV  = AddressMode::ClampToEdge;
};

struct BindGroupDesc {
    TextureHandle texture;
    SamplerHandle sampler;
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
    std::vector<std::byte> vertexSpirv;
    std::vector<std::byte> fragmentSpirv;
    VertexLayout           vertexLayout;
    PrimitiveTopology      topology    = PrimitiveTopology::TriangleList;
    CullMode               cull        = CullMode::None;
    Format                 colorFormat = Format::B8G8R8A8_UNORM;
    bool                   alphaBlend  = false;  // straight-alpha blending for sprites
    bool                   hasMaterialTexture = false;  // set 0, binding 0: combined image sampler
    u32                    pushConstantSize   = 0;       // vertex-stage push constant block bytes
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

struct RenderPassDesc {
    ColorAttachment color;
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
