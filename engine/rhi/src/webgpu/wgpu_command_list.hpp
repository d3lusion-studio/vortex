#pragma once
#include "wgpu_common.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <vector>

namespace vortex::rhi::wgpu {

class WebGPUDevice;

class WebGPUCommandList final : public ICommandList {
public:
    WebGPUCommandList() = default;

    // Primary: drives a live render-pass encoder created by the device.
    void bindPrimary(WebGPUDevice* device) { m_device = device; m_deferred = false; }
    void setEncoder(WGPURenderPassEncoder enc) { m_encoder = enc; }

    // Secondary: records into m_commands for later replay.
    void bindSecondary(WebGPUDevice* device) {
        m_device = device;
        m_deferred = true;
        m_commands.clear();
        m_pushData.clear();
    }
    void replayOnto(WGPURenderPassEncoder enc);
    [[nodiscard]] WGPURenderPassEncoder encoder() const { return m_encoder; }

    void beginRenderPass(const RenderPassDesc&) override;
    void endRenderPass() override;

    void transition(TextureHandle, ResourceState newState) override;

    void setPipeline(PipelineHandle) override;
    void setBindGroup(u32 slot, BindGroupHandle) override;
    void pushConstants(const void* data, u32 size) override;
    void setViewport(const Viewport&) override;
    void setScissor(i32 x, i32 y, u32 width, u32 height) override;

    void setVertexBuffer(u32 slot, BufferHandle, u64 offset) override;
    void setIndexBuffer(BufferHandle, IndexType) override;

    void draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
    void drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                     i32 vertexOffset, u32 firstInstance) override;

    void dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;

private:
    enum class Op : u8 {
        Pipeline, BindGroup, Push, Viewport, Scissor,
        VertexBuffer, IndexBuffer, Draw, DrawIndexed
    };
    struct Cmd {
        Op  op;
        PipelineHandle  pipeline;
        BindGroupHandle bindGroup;
        BufferHandle    buffer;
        u32 slot = 0;
        u64 offset = 0;
        IndexType indexType = IndexType::U32;
        Viewport viewport{};
        i32 sx = 0, sy = 0; u32 sw = 0, sh = 0;
        u32 a = 0, b = 0, c = 0, e = 0; i32 d = 0;   // draw args
        u32 pushOffset = 0, pushSize = 0;             // slice into m_pushData
    };

    // Live execution helpers (run against m_encoder). Used both directly by the
    // primary and during secondary replay.
    void doPipeline(PipelineHandle);
    void doBindGroup(u32 slot, BindGroupHandle);
    void doPush(const void* data, u32 size);
    void doViewport(const Viewport&);
    void doScissor(i32 x, i32 y, u32 w, u32 h);
    void doVertexBuffer(u32 slot, BufferHandle, u64 offset);
    void doIndexBuffer(BufferHandle, IndexType);
    void doDraw(u32 vc, u32 ic, u32 fv, u32 fi);
    void doDrawIndexed(u32 ic, u32 inst, u32 fi, i32 vo, u32 finst);

    WebGPUDevice*         m_device  = nullptr;
    WGPURenderPassEncoder m_encoder = nullptr;
    u32                   m_currentPushSize = 0;

    bool                   m_deferred = false;
    std::vector<Cmd>       m_commands;
    std::vector<std::byte> m_pushData;
};

}
