#include "vk_command_list.hpp"
#include "vk_device.hpp"

#include "vortex/core/log.hpp"

namespace vortex::rhi::vk {

namespace {

struct StageAccess { VkPipelineStageFlags stage; VkAccessFlags access; };

StageAccess stageAccessFor(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            return {VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return {VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT};
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return {VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0};
        case VK_IMAGE_LAYOUT_UNDEFINED:
        default:
            return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0};
    }
}

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout) {
    if (oldLayout == newLayout) return;
    const StageAccess src = stageAccessFor(oldLayout);
    const StageAccess dst = stageAccessFor(newLayout);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange    = {aspect, 0, 1, 0, 1};
    barrier.srcAccessMask       = src.access;
    barrier.dstAccessMask       = dst.access;

    vkCmdPipelineBarrier(cmd, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

}

void VulkanCommandList::beginRenderPass(const RenderPassDesc& desc) {
    // A pass with no colour target is depth-only (e.g. the shadow map pass).
    VulkanTexture* tex = m_device->getTexture(desc.color.target);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    if (tex) {
        // LoadOp::Load must preserve existing contents, so keep the current layout
        // as the barrier source; a Clear/DontCare pass can discard from UNDEFINED.
        const VkImageLayout oldLayout = desc.color.loadOp == LoadOp::Load
            ? tex->currentLayout : VK_IMAGE_LAYOUT_UNDEFINED;
        transitionImage(m_cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
                        oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        tex->currentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        color.imageView   = tex->view;
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp      = toVkLoadOp(desc.color.loadOp);
        color.storeOp     = toVkStoreOp(desc.color.storeOp);
        color.clearValue.color = {{desc.color.clearColor[0], desc.color.clearColor[1],
                                   desc.color.clearColor[2], desc.color.clearColor[3]}};
    }

    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea           = {{0, 0}, {desc.width, desc.height}};
    ri.layerCount           = 1;
    ri.colorAttachmentCount = tex ? 1u : 0u;
    ri.pColorAttachments    = tex ? &color : nullptr;

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    VulkanTexture* depthTex = m_device->getTexture(desc.depth.target);
    if (depthTex) {
        transitionImage(m_cmd, depthTex->image, VK_IMAGE_ASPECT_DEPTH_BIT,
                        depthTex->currentLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        depthTex->currentLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

        depth.imageView   = depthTex->view;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp      = toVkLoadOp(desc.depth.loadOp);
        depth.storeOp     = toVkStoreOp(desc.depth.storeOp);
        depth.clearValue.depthStencil = {desc.depth.clearDepth, 0};
        ri.pDepthAttachment = &depth;
    }

    if (desc.secondaryContents)
        ri.flags |= VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
    vkCmdBeginRendering(m_cmd, &ri);
}

void VulkanCommandList::transition(TextureHandle h, ResourceState newState) {
    VulkanTexture* tex = m_device->getTexture(h);
    if (!tex) return;
    const VkImageLayout newLayout = toVkLayout(newState);
    transitionImage(m_cmd, tex->image, tex->aspect, tex->currentLayout, newLayout);
    tex->currentLayout = newLayout;
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
    vkCmdPushConstants(m_cmd, m_currentLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
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

void VulkanCommandList::dispatch(u32, u32, u32) {
    // Stub: compute pipelines are not wired yet, so there is nothing bound to
    // dispatch against. Warn once rather than issue invalid GPU work.
    static bool warned = false;
    if (!warned) { VORTEX_WARN("RHI", "dispatch() is not implemented (compute stub)"); warned = true; }
}

void VulkanCommandList::transitionToPresent(TextureHandle h) {
    VulkanTexture* tex = m_device->getTexture(h);
    if (!tex) return;
    transitionImage(m_cmd, tex->image, VK_IMAGE_ASPECT_COLOR_BIT,
                    tex->currentLayout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    tex->currentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
}

}
