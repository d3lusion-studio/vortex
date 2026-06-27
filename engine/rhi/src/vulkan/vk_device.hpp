#pragma once
#include "vk_command_list.hpp"
#include "vk_common.hpp"
#include "vk_resources.hpp"

#include "vortex/rhi/device.hpp"

#include <atomic>
#include <functional>
#include <memory>

namespace vortex::pf { class IWindow; }

namespace vortex::rhi::vk {

class VulkanSwapchain;

inline constexpr u32 kMaxSecondaries = 64;

class VulkanDevice final : public IGraphicsDevice {
public:
    explicit VulkanDevice(pf::IWindow& window);
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&)            = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc&, const void* initialData) override;
    void destroyBuffer(BufferHandle) override;
    void updateBuffer(BufferHandle, const void* data, u64 size, u64 offset) override;

    [[nodiscard]] TextureHandle createTexture(const TextureDesc&, const void* pixels) override;
    void destroyTexture(TextureHandle) override;

    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc&) override;
    void destroySampler(SamplerHandle) override;

    [[nodiscard]] BindGroupHandle createBindGroup(const BindGroupDesc&) override;
    void destroyBindGroup(BindGroupHandle) override;

    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) override;
    void destroyPipeline(PipelineHandle) override;

    [[nodiscard]] std::unique_ptr<ISwapchain> createSwapchain(const SwapchainDesc&, pf::IWindow&) override;

    [[nodiscard]] FrameContext beginFrame(ISwapchain&) override;
    void endFrame() override;

    void waitIdle() override;

    [[nodiscard]] ICommandList* acquireSecondaryCommandList() override;
    void executeSecondary(ICommandList& primary, ICommandList* const* lists, u32 count) override;

    [[nodiscard]] f64 gpuFrameTimeMs() const override { return m_gpuFrameTimeMs; }

    [[nodiscard]] VkDevice         vkDevice()      const { return m_device; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    [[nodiscard]] VkInstance       instance()      const { return m_instance; }
    [[nodiscard]] VkSurfaceKHR     surface()       const { return m_surface; }
    [[nodiscard]] VkQueue          graphicsQueue() const { return m_graphicsQueue; }
    [[nodiscard]] u32              graphicsQueueFamily() const { return m_graphicsQueueFamily; }

    [[nodiscard]] VulkanBuffer*    getBuffer(BufferHandle h)       { return m_buffers.get(h); }
    [[nodiscard]] VulkanPipeline*  getPipeline(PipelineHandle h)   { return m_pipelines.get(h); }
    [[nodiscard]] VulkanTexture*   getTexture(TextureHandle h)     { return m_textures.get(h); }
    [[nodiscard]] VulkanSampler*   getSampler(SamplerHandle h)     { return m_samplers.get(h); }
    [[nodiscard]] VulkanBindGroup* getBindGroup(BindGroupHandle h) { return m_bindGroups.get(h); }

    [[nodiscard]] TextureHandle registerTexture(const VulkanTexture&);
    void unregisterTexture(TextureHandle);

private:
    void createSurface(pf::IWindow& window);
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& fn);

    VkInstance               m_instance         = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger   = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface          = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice   = VK_NULL_HANDLE;
    VkDevice                 m_device           = VK_NULL_HANDLE;
    VmaAllocator             m_allocator        = VK_NULL_HANDLE;
    VkPipelineCache          m_pipelineCache    = VK_NULL_HANDLE;
    VkDescriptorPool         m_descriptorPool   = VK_NULL_HANDLE;
    VkDescriptorSetLayout    m_materialSetLayout = VK_NULL_HANDLE;  // set 0: combined image sampler

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue  = VK_NULL_HANDLE;
    u32     m_graphicsQueueFamily = 0;

    struct SecondaryRecorder {
        VkCommandPool     pool = VK_NULL_HANDLE;
        VkCommandBuffer   cmd  = VK_NULL_HANDLE;
        VulkanCommandList list;
    };

    struct FrameData {
        VkCommandPool   pool           = VK_NULL_HANDLE;
        VkCommandBuffer cmd            = VK_NULL_HANDLE;
        VkSemaphore     imageAvailable = VK_NULL_HANDLE;
        VkFence         inFlight       = VK_NULL_HANDLE;

        std::unique_ptr<SecondaryRecorder> secondaries[kMaxSecondaries];
        std::atomic<u32>                   secondaryNext{0};
        bool                               timestampWritten = false;
    };
    FrameData m_frames[kFramesInFlight];
    u32       m_currentFrame = 0;

    // GPU timestamp profiling (two queries per frame in flight: start + end).
    VkQueryPool m_timestampPool      = VK_NULL_HANDLE;
    f32         m_timestampPeriodNs  = 0.0f;
    bool        m_timestampsSupported = false;
    f64         m_gpuFrameTimeMs     = 0.0;

    // One-off transfer submissions (staging copies).
    VkCommandPool m_uploadPool  = VK_NULL_HANDLE;
    VkFence       m_uploadFence = VK_NULL_HANDLE;

    Pool<VulkanBuffer,    BufferTag>    m_buffers;
    Pool<VulkanPipeline,  PipelineTag>  m_pipelines;
    Pool<VulkanTexture,   TextureTag>   m_textures;
    Pool<VulkanSampler,   SamplerTag>   m_samplers;
    Pool<VulkanBindGroup, BindGroupTag> m_bindGroups;

    VulkanCommandList m_cmdList;

    VulkanSwapchain* m_frameSwapchain = nullptr;
    u32              m_frameImageIndex = 0;
    bool             m_frameActive     = false;
};

}
