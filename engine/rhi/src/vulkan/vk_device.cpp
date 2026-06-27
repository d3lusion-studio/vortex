#include "vk_device.hpp"
#include "vk_surface.hpp"
#include "vk_swapchain.hpp"

#include "vortex/core/assert.hpp"
#include "vortex/platform/window.hpp"

#include <VkBootstrap.h>

#include <cstring>

namespace vortex::rhi::vk {

VulkanDevice::VulkanDevice(pf::IWindow& window) {
    vkb::InstanceBuilder ib;
    ib.set_app_name("Vortex").require_api_version(1, 3, 0);

    auto sysInfo = vkb::SystemInfo::get_system_info();
    const bool useValidation = sysInfo.has_value() && sysInfo->validation_layers_available;
    if (useValidation) {
        ib.request_validation_layers(true).use_default_debug_messenger();
        VORTEX_INFO("RHI", "Validation layers enabled");
    } else {
        VORTEX_WARN("RHI", "Validation layers unavailable — running without them");
    }

    ib.enable_extension(VK_KHR_SURFACE_EXTENSION_NAME);
    if (const char* ext = surfaceExtensionName(window.nativeHandleKind()))
        ib.enable_extension(ext);

    auto instRet = ib.build();
    if (!instRet) {
        VORTEX_ERROR("RHI", "Failed to create Vulkan instance: %s", instRet.error().message().c_str());
        return;
    }
    vkb::Instance vkbInstance = instRet.value();
    m_instance       = vkbInstance.instance;
    m_debugMessenger = vkbInstance.debug_messenger;

    createSurface(window);
    if (m_surface == VK_NULL_HANDLE) return;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{vkbInstance};
    auto physRet = selector.set_surface(m_surface)
                           .set_minimum_version(1, 3)
                           .set_required_features_13(features13)
                           .select();
    if (!physRet) {
        VORTEX_ERROR("RHI", "No suitable GPU: %s", physRet.error().message().c_str());
        return;
    }
    vkb::PhysicalDevice vkbPhys = physRet.value();
    m_physicalDevice = vkbPhys.physical_device;
    VORTEX_INFO("RHI", "GPU: %s", vkbPhys.properties.deviceName);

    vkb::DeviceBuilder db{vkbPhys};
    auto devRet = db.build();
    if (!devRet) {
        VORTEX_ERROR("RHI", "Failed to create device: %s", devRet.error().message().c_str());
        return;
    }
    vkb::Device vkbDevice = devRet.value();
    m_device              = vkbDevice.device;
    m_graphicsQueue       = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    m_presentQueue        = vkbDevice.get_queue(vkb::QueueType::present).value();

    VmaAllocatorCreateInfo allocCI{};
    allocCI.physicalDevice   = m_physicalDevice;
    allocCI.device           = m_device;
    allocCI.instance         = m_instance;
    allocCI.vulkanApiVersion = VK_API_VERSION_1_3;
    VK_CHECK(vmaCreateAllocator(&allocCI, &m_allocator));

    VkPipelineCacheCreateInfo cacheCI{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    VK_CHECK(vkCreatePipelineCache(m_device, &cacheCI, nullptr, &m_pipelineCache));

    for (u32 i = 0; i < kFramesInFlight; ++i) {
        VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolCI.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolCI.queueFamilyIndex = m_graphicsQueueFamily;
        VK_CHECK(vkCreateCommandPool(m_device, &poolCI, nullptr, &m_frames[i].pool));

        VkCommandBufferAllocateInfo cmdAI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdAI.commandPool        = m_frames[i].pool;
        cmdAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAI.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_device, &cmdAI, &m_frames[i].cmd));

        VkSemaphoreCreateInfo semCI{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(m_device, &semCI, nullptr, &m_frames[i].imageAvailable));

        VkFenceCreateInfo fenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(m_device, &fenceCI, nullptr, &m_frames[i].inFlight));
    }

    VkCommandPoolCreateInfo uploadPoolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    uploadPoolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    uploadPoolCI.queueFamilyIndex = m_graphicsQueueFamily;
    VK_CHECK(vkCreateCommandPool(m_device, &uploadPoolCI, nullptr, &m_uploadPool));

    VkFenceCreateInfo uploadFenceCI{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(m_device, &uploadFenceCI, nullptr, &m_uploadFence));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo setLayoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setLayoutCI.bindingCount = 1;
    setLayoutCI.pBindings    = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &setLayoutCI, nullptr, &m_materialSetLayout));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024};
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets       = 1024;
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes    = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(m_device, &poolCI, nullptr, &m_descriptorPool));

    VORTEX_INFO("RHI", "Vulkan device initialised");
}

VulkanDevice::~VulkanDevice() {
    if (m_device == VK_NULL_HANDLE) {
        if (m_surface)  vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        if (m_instance) vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);
        if (m_instance) vkDestroyInstance(m_instance, nullptr);
        return;
    }

    vkDeviceWaitIdle(m_device);

    m_pipelines.forEachAlive([&](VulkanPipeline& p) {
        vkDestroyPipeline(m_device, p.pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, p.layout, nullptr);
    });
    m_buffers.forEachAlive([&](VulkanBuffer& b) {
        vmaDestroyBuffer(m_allocator, b.buffer, b.allocation);
    });
    m_textures.forEachAlive([&](VulkanTexture& t) {
        if (t.view) vkDestroyImageView(m_device, t.view, nullptr);
        if (t.allocation) vmaDestroyImage(m_allocator, t.image, t.allocation);
    });
    m_samplers.forEachAlive([&](VulkanSampler& s) {
        vkDestroySampler(m_device, s.sampler, nullptr);
    });

    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_materialSetLayout, nullptr);

    for (u32 i = 0; i < kFramesInFlight; ++i) {
        vkDestroyFence(m_device, m_frames[i].inFlight, nullptr);
        vkDestroySemaphore(m_device, m_frames[i].imageAvailable, nullptr);
        vkDestroyCommandPool(m_device, m_frames[i].pool, nullptr);
    }
    vkDestroyFence(m_device, m_uploadFence, nullptr);
    vkDestroyCommandPool(m_device, m_uploadPool, nullptr);

    vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
    vmaDestroyAllocator(m_allocator);
    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_instance, m_debugMessenger);
    vkDestroyInstance(m_instance, nullptr);
}

void VulkanDevice::createSurface(pf::IWindow& window) {
    m_surface = createNativeSurface(m_instance, window.nativeHandleKind(),
                                    window.nativeDisplayHandle(), window.nativeWindowHandle());
    VORTEX_ASSERT(m_surface != VK_NULL_HANDLE, "Failed to create Vulkan surface");
}

void VulkanDevice::immediateSubmit(const std::function<void(VkCommandBuffer)>& fn) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool        = m_uploadPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &ai, &cmd));

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    fn(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkResetFences(m_device, 1, &m_uploadFence));
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &si, m_uploadFence));
    VK_CHECK(vkWaitForFences(m_device, 1, &m_uploadFence, VK_TRUE, UINT64_MAX));
    vkFreeCommandBuffers(m_device, m_uploadPool, 1, &cmd);
}

BufferHandle VulkanDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
    VkBufferUsageFlags usage = 0;
    if (hasFlag(desc.usage, BufferUsage::Vertex))  usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasFlag(desc.usage, BufferUsage::Index))   usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasFlag(desc.usage, BufferUsage::Uniform)) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasFlag(desc.usage, BufferUsage::Storage)) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    const bool deviceLocal = (desc.domain == MemoryDomain::Device);
    if (deviceLocal && initialData) usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = desc.size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    if (!deviceLocal)
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VulkanBuffer buffer{};
    buffer.size = desc.size;
    VmaAllocationInfo allocInfo{};
    VK_CHECK(vmaCreateBuffer(m_allocator, &bci, &aci, &buffer.buffer, &buffer.allocation, &allocInfo));
    buffer.mapped = allocInfo.pMappedData;

    if (initialData) {
        if (!deviceLocal) {
            std::memcpy(buffer.mapped, initialData, desc.size);
        } else {
            VkBufferCreateInfo sci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            sci.size        = desc.size;
            sci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            sci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VmaAllocationCreateInfo saci{};
            saci.usage = VMA_MEMORY_USAGE_AUTO;
            saci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                         VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VkBuffer staging = VK_NULL_HANDLE;
            VmaAllocation stagingAlloc = VK_NULL_HANDLE;
            VmaAllocationInfo stagingInfo{};
            VK_CHECK(vmaCreateBuffer(m_allocator, &sci, &saci, &staging, &stagingAlloc, &stagingInfo));
            std::memcpy(stagingInfo.pMappedData, initialData, desc.size);
            immediateSubmit([&](VkCommandBuffer cmd) {
                VkBufferCopy copy{0, 0, desc.size};
                vkCmdCopyBuffer(cmd, staging, buffer.buffer, 1, &copy);
            });
            vmaDestroyBuffer(m_allocator, staging, stagingAlloc);
        }
    }
    return m_buffers.create(buffer);
}

void VulkanDevice::destroyBuffer(BufferHandle h) {
    if (VulkanBuffer* b = m_buffers.get(h)) {
        vmaDestroyBuffer(m_allocator, b->buffer, b->allocation);
        m_buffers.destroy(h);
    }
}

void VulkanDevice::updateBuffer(BufferHandle h, const void* data, u64 size, u64 offset) {
    VulkanBuffer* b = m_buffers.get(h);
    if (!b) return;
    VORTEX_ASSERT(b->mapped != nullptr, "updateBuffer requires an Upload-domain (mapped) buffer");
    VORTEX_ASSERT(offset + size <= b->size, "updateBuffer write exceeds buffer size");
    std::memcpy(static_cast<std::byte*>(b->mapped) + offset, data, size);
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& desc, const void* pixels) {
    VulkanTexture tex{};
    tex.format = toVkFormat(desc.format);
    tex.extent = {desc.width, desc.height};

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (pixels) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasFlag(desc.usage, TextureUsage::RenderTarget))
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType   = VK_IMAGE_TYPE_2D;
    ici.format      = tex.format;
    ici.extent      = {desc.width, desc.height, 1};
    ici.mipLevels   = 1;
    ici.arrayLayers = 1;
    ici.samples     = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling      = VK_IMAGE_TILING_OPTIMAL;
    ici.usage       = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateImage(m_allocator, &ici, &aci, &tex.image, &tex.allocation, nullptr));

    if (pixels) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(desc.width) * desc.height * 4;
        VkBufferCreateInfo sci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        sci.size  = bytes;
        sci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo saci{};
        saci.usage = VMA_MEMORY_USAGE_AUTO;
        saci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer staging = VK_NULL_HANDLE;
        VmaAllocation stagingAlloc = VK_NULL_HANDLE;
        VmaAllocationInfo stagingInfo{};
        VK_CHECK(vmaCreateBuffer(m_allocator, &sci, &saci, &staging, &stagingAlloc, &stagingInfo));
        std::memcpy(stagingInfo.pMappedData, pixels, bytes);

        immediateSubmit([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toDst.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            toDst.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toDst.image            = tex.image;
            toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            toDst.srcAccessMask    = 0;
            toDst.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent      = {desc.width, desc.height, 1};
            vkCmdCopyBufferToImage(cmd, staging, tex.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

            VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            toRead.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.newLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.image            = tex.image;
            toRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            toRead.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
            toRead.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
        });
        vmaDestroyBuffer(m_allocator, staging, stagingAlloc);
        tex.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image            = tex.image;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = tex.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_device, &vci, nullptr, &tex.view));

    return m_textures.create(tex);
}

void VulkanDevice::destroyTexture(TextureHandle h) {
    if (VulkanTexture* t = m_textures.get(h)) {
        if (t->view) vkDestroyImageView(m_device, t->view, nullptr);
        if (t->allocation) vmaDestroyImage(m_allocator, t->image, t->allocation);
        m_textures.destroy(h);
    }
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc& desc) {
    auto toVkFilter = [](Filter f) {
        return f == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    };
    auto toVkAddress = [](AddressMode m) {
        switch (m) {
            case AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        }
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    };

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.minFilter    = toVkFilter(desc.minFilter);
    sci.magFilter    = toVkFilter(desc.magFilter);
    sci.addressModeU = toVkAddress(desc.addressU);
    sci.addressModeV = toVkAddress(desc.addressV);
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VulkanSampler sampler{};
    VK_CHECK(vkCreateSampler(m_device, &sci, nullptr, &sampler.sampler));
    return m_samplers.create(sampler);
}

void VulkanDevice::destroySampler(SamplerHandle h) {
    if (VulkanSampler* s = m_samplers.get(h)) {
        vkDestroySampler(m_device, s->sampler, nullptr);
        m_samplers.destroy(h);
    }
}

BindGroupHandle VulkanDevice::createBindGroup(const BindGroupDesc& desc) {
    VulkanTexture* tex = m_textures.get(desc.texture);
    VulkanSampler* smp = m_samplers.get(desc.sampler);
    VORTEX_ASSERT(tex && smp, "createBindGroup with invalid texture or sampler handle");
    if (!tex || !smp) return {};

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool     = m_descriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_materialSetLayout;
    VulkanBindGroup group{};
    VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, &group.set));

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = smp->sampler;
    imageInfo.imageView   = tex->view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet          = group.set;
    write.dstBinding      = 0;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

    return m_bindGroups.create(group);
}

void VulkanDevice::destroyBindGroup(BindGroupHandle h) {
    if (VulkanBindGroup* g = m_bindGroups.get(h)) {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &g->set);
        m_bindGroups.destroy(h);
    }
}

PipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto makeModule = [&](const std::vector<std::byte>& code) -> VkShaderModule {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = code.size();
        ci.pCode    = reinterpret_cast<const u32*>(code.data());
        VkShaderModule m = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(m_device, &ci, nullptr, &m));
        return m;
    };
    VkShaderModule vs = makeModule(desc.vertexSpirv);
    VkShaderModule fs = makeModule(desc.fragmentSpirv);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName  = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding   = 0;
    binding.stride    = desc.vertexLayout.stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attrs;
    attrs.reserve(desc.vertexLayout.attributes.size());
    for (const VertexAttribute& a : desc.vertexLayout.attributes)
        attrs.push_back({a.location, 0, toVkFormat(a.format), a.offset});

    const bool hasVertices = desc.vertexLayout.stride > 0;
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount   = hasVertices ? 1u : 0u;
    vin.pVertexBindingDescriptions      = hasVertices ? &binding : nullptr;
    vin.vertexAttributeDescriptionCount = static_cast<u32>(attrs.size());
    vin.pVertexAttributeDescriptions    = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = toVkTopology(desc.topology);

    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = toVkCull(desc.cull);
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    cba.blendEnable = desc.alphaBlend ? VK_TRUE : VK_FALSE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments    = &cba;

    VkDynamicState dynStates[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates    = dynStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = desc.pushConstantSize;

    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (desc.hasMaterialTexture) {
        lci.setLayoutCount = 1;
        lci.pSetLayouts    = &m_materialSetLayout;
    }
    if (desc.pushConstantSize > 0) {
        lci.pushConstantRangeCount = 1;
        lci.pPushConstantRanges    = &pushRange;
    }
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(m_device, &lci, nullptr, &layout));

    VkFormat colorFormat = toVkFormat(desc.colorFormat);
    VkPipelineRenderingCreateInfo rci{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rci.colorAttachmentCount    = 1;
    rci.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext               = &rci;
    pci.stageCount          = 2;
    pci.pStages             = stages;
    pci.pVertexInputState   = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pColorBlendState    = &cb;
    pci.pDynamicState       = &ds;
    pci.layout              = layout;
    pci.renderPass          = VK_NULL_HANDLE;

    VulkanPipeline pipeline{};
    pipeline.layout = layout;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &pci, nullptr, &pipeline.pipeline));

    vkDestroyShaderModule(m_device, vs, nullptr);
    vkDestroyShaderModule(m_device, fs, nullptr);
    return m_pipelines.create(pipeline);
}

void VulkanDevice::destroyPipeline(PipelineHandle h) {
    if (VulkanPipeline* p = m_pipelines.get(h)) {
        vkDestroyPipeline(m_device, p->pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, p->layout, nullptr);
        m_pipelines.destroy(h);
    }
}

std::unique_ptr<ISwapchain> VulkanDevice::createSwapchain(const SwapchainDesc& desc, pf::IWindow&) {
    return std::make_unique<VulkanSwapchain>(*this, desc);
}

TextureHandle VulkanDevice::registerTexture(const VulkanTexture& tex) {
    return m_textures.create(tex);
}

void VulkanDevice::unregisterTexture(TextureHandle h) {
    m_textures.destroy(h);
}

FrameContext VulkanDevice::beginFrame(ISwapchain& scBase) {
    auto& sc = static_cast<VulkanSwapchain&>(scBase);
    FrameContext fc{};
    FrameData& frame = m_frames[m_currentFrame];

    VK_CHECK(vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX));

    if (sc.needsRecreate()) {
        vkDeviceWaitIdle(m_device);
        if (!sc.recreate()) return fc;
    }

    u32 imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(m_device, sc.handle(), UINT64_MAX,
                                             frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        sc.markOutOfDate();
        return fc;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        VORTEX_ERROR("RHI", "vkAcquireNextImageKHR failed (%d)", static_cast<int>(acquire));
        return fc;
    }

    VK_CHECK(vkResetFences(m_device, 1, &frame.inFlight));
    VK_CHECK(vkResetCommandBuffer(frame.cmd, 0));

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(frame.cmd, &bi));

    m_cmdList.bind(this, frame.cmd);
    m_frameSwapchain  = &sc;
    m_frameImageIndex = imageIndex;
    m_frameActive     = true;

    VkExtent2D ext = sc.extent();
    fc.cmd        = &m_cmdList;
    fc.backbuffer = sc.textureForImage(imageIndex);
    fc.width      = ext.width;
    fc.height     = ext.height;
    fc.valid      = true;
    return fc;
}

void VulkanDevice::endFrame() {
    if (!m_frameActive) return;
    FrameData& frame = m_frames[m_currentFrame];
    VulkanSwapchain& sc = *m_frameSwapchain;

    m_cmdList.transitionToPresent(sc.textureForImage(m_frameImageIndex));
    VK_CHECK(vkEndCommandBuffer(frame.cmd));

    VkSemaphore waitSem   = frame.imageAvailable;
    VkSemaphore signalSem = sc.renderFinishedSemaphore(m_frameImageIndex);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &waitSem;
    si.pWaitDstStageMask    = &waitStage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &frame.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &signalSem;
    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &si, frame.inFlight));

    VkSwapchainKHR swap = sc.handle();
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &signalSem;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &swap;
    pi.pImageIndices      = &m_frameImageIndex;
    VkResult present = vkQueuePresentKHR(m_presentQueue, &pi);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR)
        sc.markOutOfDate();
    else
        VK_CHECK(present);

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
    m_frameActive  = false;
}

void VulkanDevice::waitIdle() {
    if (m_device) vkDeviceWaitIdle(m_device);
}

}

namespace vortex::rhi {

std::unique_ptr<IGraphicsDevice> createDevice(GraphicsAPI api, pf::IWindow& window) {
    VORTEX_ASSERT(api == GraphicsAPI::Vulkan, "Only the Vulkan backend is implemented");
    (void)api;
    return std::make_unique<vk::VulkanDevice>(window);
}

}
