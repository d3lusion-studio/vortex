#pragma once
#include "vk_common.hpp"
#include "vortex/core/handle.hpp"

#include <vector>

namespace vortex::rhi::vk {

struct VulkanBuffer {
    VkBuffer      buffer     = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void*         mapped     = nullptr;
    u64           size       = 0;
};

struct VulkanPipeline {
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout   = VK_NULL_HANDLE;
};

struct VulkanTexture {
    VkImage            image         = VK_NULL_HANDLE;
    VkImageView        view          = VK_NULL_HANDLE;
    VmaAllocation      allocation    = VK_NULL_HANDLE;
    VkFormat           format        = VK_FORMAT_UNDEFINED;
    Format             rhiFormat     = Format::Undefined;   // kept for the pixel pitch
    VkExtent2D         extent        = {0, 0};
    VkImageLayout      currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect        = VK_IMAGE_ASPECT_COLOR_BIT;
    bool          isSwapchainImage   = false;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanBindGroup {
    VkDescriptorSet set = VK_NULL_HANDLE;
};

template <typename T, typename Tag>
class Pool {
public:
    using HandleT = Handle<Tag>;

    [[nodiscard]] HandleT create(const T& value) {
        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
            m_slots[index].value = value;
            m_slots[index].alive = true;
        } else {
            index = static_cast<u32>(m_slots.size());
            m_slots.push_back({value, 0u, true});
        }
        return HandleT{.index = index, .generation = m_slots[index].generation};
    }

    [[nodiscard]] T* get(HandleT h) {
        if (h.index >= m_slots.size()) return nullptr;
        Slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation) return nullptr;
        return &s.value;
    }

    void destroy(HandleT h) {
        if (h.index >= m_slots.size()) return;
        Slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation) return;
        s.alive = false;
        ++s.generation;
        m_free.push_back(h.index);
    }

    template <typename Fn>
    void forEachAlive(Fn&& fn) {
        for (Slot& s : m_slots)
            if (s.alive) fn(s.value);
    }

private:
    struct Slot {
        T    value;
        u32  generation = 0;
        bool alive      = false;
    };
    std::vector<Slot> m_slots;
    std::vector<u32>  m_free;
};

}
