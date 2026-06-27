#pragma once
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"
#include "vortex/rhi/rhi_enums.hpp"

namespace vortex::rhi {

struct RenderPassDesc;
struct Viewport;

class ICommandList {
public:
    virtual ~ICommandList() = default;

    virtual void beginRenderPass(const RenderPassDesc&) = 0;
    virtual void endRenderPass() = 0;

    virtual void setPipeline(PipelineHandle) = 0;
    virtual void setBindGroup(u32 slot, BindGroupHandle) = 0;
    virtual void pushConstants(const void* data, u32 size) = 0;
    virtual void setViewport(const Viewport&) = 0;
    virtual void setScissor(i32 x, i32 y, u32 width, u32 height) = 0;

    virtual void setVertexBuffer(u32 slot, BufferHandle, u64 offset = 0) = 0;
    virtual void setIndexBuffer(BufferHandle, IndexType) = 0;

    virtual void draw(u32 vertexCount, u32 instanceCount = 1,
                      u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void drawIndexed(u32 indexCount, u32 instanceCount = 1,
                             u32 firstIndex = 0, i32 vertexOffset = 0,
                             u32 firstInstance = 0) = 0;
};

}
