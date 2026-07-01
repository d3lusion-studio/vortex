#pragma once
#include "vk_common.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/rhi_types.hpp"

namespace vortex::rhi::vk {

class VulkanDevice;

class VulkanCommandList final : public ICommandList {
public:
    VulkanCommandList() = default;

    void bind(VulkanDevice* device, VkCommandBuffer cmd) {
        m_device = device;
        m_cmd    = cmd;
    }

    [[nodiscard]] VkCommandBuffer commandBuffer() const { return m_cmd; }

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

    void transitionToPresent(TextureHandle);

private:
    VulkanDevice*    m_device        = nullptr;
    VkCommandBuffer  m_cmd           = VK_NULL_HANDLE;
    VkPipelineLayout m_currentLayout = VK_NULL_HANDLE;
};

}
