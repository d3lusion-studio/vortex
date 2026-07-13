#include "vk_swapchain.hpp"
#include "vk_device.hpp"

#include "vortex/core/log.hpp"

#include <VkBootstrap.h>

namespace vortex::rhi::vk {

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, const SwapchainDesc& desc)
    : m_device(device), m_presentMode(desc.present) {
    m_pendingWidth  = desc.width;
    m_pendingHeight = desc.height;
    build();
}

VulkanSwapchain::~VulkanSwapchain() {
    destroy();
}

void VulkanSwapchain::build() {
    vkb::SwapchainBuilder builder{m_device.physicalDevice(), m_device.vkDevice(), m_device.surface()};
    builder.set_desired_present_mode(toVkPresentMode(m_presentMode))
           .set_desired_extent(m_pendingWidth, m_pendingHeight)
           // An _SRGB backbuffer makes the hardware encode linear -> sRGB on write, which is
           // what lets everything above render in linear light without a manual gamma step.
           // Blending happens before the encode, so it is correct too — that is the whole
           // reason to ask for the format rather than to pow() in a shader.
           .set_desired_format(VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB,
                                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
           .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    auto ret = builder.build();
    if (!ret) {
        VORTEX_ERROR("RHI", "Swapchain creation failed: %s", ret.error().message().c_str());
        return;
    }
    vkb::Swapchain vkbSc = ret.value();
    m_swapchain = vkbSc.swapchain;
    m_vkFormat  = vkbSc.image_format;
    m_extent    = vkbSc.extent;

    auto images = vkbSc.get_images().value();
    VkDevice dev = m_device.vkDevice();
    for (VkImage image : images) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image            = image;
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = m_vkFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        VK_CHECK(vkCreateImageView(dev, &vci, nullptr, &view));

        VulkanTexture tex{};
        tex.image            = image;
        tex.view             = view;
        tex.format           = m_vkFormat;
        tex.extent           = m_extent;
        tex.currentLayout    = VK_IMAGE_LAYOUT_UNDEFINED;
        tex.isSwapchainImage = true;
        m_imageTextures.push_back(m_device.registerTexture(tex));

        VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkSemaphore sem = VK_NULL_HANDLE;
        VK_CHECK(vkCreateSemaphore(dev, &semCI, nullptr, &sem));
        m_renderFinished.push_back(sem);
    }

    VORTEX_INFO("RHI", "Swapchain: %ux%u, %zu images",
                m_extent.width, m_extent.height, m_imageTextures.size());
}

void VulkanSwapchain::destroy() {
    VkDevice dev = m_device.vkDevice();
    if (dev == VK_NULL_HANDLE) return;

    for (TextureHandle h : m_imageTextures) {
        if (VulkanTexture* t = m_device.getTexture(h))
            vkDestroyImageView(dev, t->view, nullptr);
        m_device.unregisterTexture(h);
    }
    m_imageTextures.clear();

    for (VkSemaphore sem : m_renderFinished)
        vkDestroySemaphore(dev, sem, nullptr);
    m_renderFinished.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(dev, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanSwapchain::recreate() {
    if (m_pendingWidth == 0 || m_pendingHeight == 0)
        return false;
    destroy();
    build();
    m_needsRecreate = false;
    return m_swapchain != VK_NULL_HANDLE;
}

Format VulkanSwapchain::format() const {
    return fromVkFormat(m_vkFormat);
}

void VulkanSwapchain::getExtent(u32& width, u32& height) const {
    width  = m_extent.width;
    height = m_extent.height;
}

void VulkanSwapchain::requestResize(u32 width, u32 height) {
    m_pendingWidth  = width;
    m_pendingHeight = height;
    m_needsRecreate = true;
}

}
