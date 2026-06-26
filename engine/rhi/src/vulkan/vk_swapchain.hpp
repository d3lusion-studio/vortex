#pragma once
#include "vk_common.hpp"
#include "vortex/rhi/rhi_handle.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <vector>

namespace vortex::rhi::vk {

class VulkanDevice;

class VulkanSwapchain final : public ISwapchain {
public:
    VulkanSwapchain(VulkanDevice& device, const SwapchainDesc& desc);
    ~VulkanSwapchain() override;

    [[nodiscard]] Format format() const override;
    void getExtent(u32& width, u32& height) const override;
    void requestResize(u32 width, u32 height) override;

    [[nodiscard]] VkSwapchainKHR handle() const { return m_swapchain; }
    [[nodiscard]] VkExtent2D     extent() const { return m_extent; }
    [[nodiscard]] bool           needsRecreate() const { return m_needsRecreate; }
    void                         markOutOfDate() { m_needsRecreate = true; }

    bool recreate();

    [[nodiscard]] TextureHandle textureForImage(u32 imageIndex) const { return m_imageTextures[imageIndex]; }
    [[nodiscard]] VkSemaphore   renderFinishedSemaphore(u32 imageIndex) const { return m_renderFinished[imageIndex]; }

private:
    void build();
    void destroy();

    VulkanDevice&  m_device;
    PresentMode    m_presentMode;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat       m_vkFormat  = VK_FORMAT_UNDEFINED;
    VkExtent2D     m_extent    = {0, 0};

    std::vector<TextureHandle> m_imageTextures;
    std::vector<VkSemaphore>   m_renderFinished;

    u32  m_pendingWidth  = 0;
    u32  m_pendingHeight = 0;
    bool m_needsRecreate = false;
};

}
