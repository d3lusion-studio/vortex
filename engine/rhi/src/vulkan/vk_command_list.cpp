#include "vk_command_list.hpp"
#include "vk_device.hpp"

namespace vortex::rhi::vk {

namespace {

void transitionImage(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    if (newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

}

void VulkanCommandList::beginRenderPass(const RenderPassDesc& desc) {
    VulkanTexture* tex = m_device->getTexture(desc.color.target);
    if (!tex) return;

    transitionImage(m_cmd, tex->image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    tex->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView   = tex->view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp      = toVkLoadOp(desc.color.loadOp);
    color.storeOp     = toVkStoreOp(desc.color.storeOp);
    color.clearValue.color = {{desc.color.clearColor[0], desc.color.clearColor[1],
                               desc.color.clearColor[2], desc.color.clearColor[3]}};

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea           = {{0, 0}, {desc.width, desc.height}};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &color;
    vkCmdBeginRendering(m_cmd, &ri);
}

void VulkanCommandList::endRenderPass() {
    vkCmdEndRendering(m_cmd);
}

void VulkanCommandList::setPipeline(PipelineHandle h) {
    if (VulkanPipeline* p = m_device->getPipeline(h)) {
        vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
        m_currentLayout = p->layout;
    }
}

void VulkanCommandList::setBindGroup(u32 slot, BindGroupHandle h) {
    VulkanBindGroup* g = m_device->getBindGroup(h);
    if (!g || m_currentLayout == VK_NULL_HANDLE) return;
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentLayout,
                            slot, 1, &g->set, 0, nullptr);
}

void VulkanCommandList::pushConstants(const void* data, u32 size) {
    if (m_currentLayout == VK_NULL_HANDLE) return;
    vkCmdPushConstants(m_cmd, m_currentLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, size, data);
}

void VulkanCommandList::setViewport(const Viewport& vp) {
    VkViewport v{};
    v.x        = vp.x;
    v.y        = vp.y;
    v.width    = vp.width;
    v.height   = vp.height;
    v.minDepth = vp.minDepth;
    v.maxDepth = vp.maxDepth;
    vkCmdSetViewport(m_cmd, 0, 1, &v);
}

void VulkanCommandList::setScissor(i32 x, i32 y, u32 width, u32 height) {
    VkRect2D scissor{{x, y}, {width, height}};
    vkCmdSetScissor(m_cmd, 0, 1, &scissor);
}

void VulkanCommandList::setVertexBuffer(u32 slot, BufferHandle h, u64 offset) {
    if (VulkanBuffer* b = m_device->getBuffer(h)) {
        VkBuffer     buf = b->buffer;
        VkDeviceSize off = offset;
        vkCmdBindVertexBuffers(m_cmd, slot, 1, &buf, &off);
    }
}

void VulkanCommandList::setIndexBuffer(BufferHandle h, IndexType type) {
    if (VulkanBuffer* b = m_device->getBuffer(h)) {
        VkIndexType vkType = (type == IndexType::U16) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(m_cmd, b->buffer, 0, vkType);
    }
}

void VulkanCommandList::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
    vkCmdDraw(m_cmd, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                                    i32 vertexOffset, u32 firstInstance) {
    vkCmdDrawIndexed(m_cmd, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::transitionToPresent(TextureHandle h) {
    VulkanTexture* tex = m_device->getTexture(h);
    if (!tex) return;
    transitionImage(m_cmd, tex->image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    tex->currentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

}
