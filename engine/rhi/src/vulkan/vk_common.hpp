#pragma once

#include "vortex/core/log.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <vector>

namespace vortex::rhi::vk {

#define VK_CHECK(expr)                                                          \
    do {                                                                       \
        VkResult vkr_ = (expr);                                                \
        if (vkr_ != VK_SUCCESS) {                                              \
            ::vortex::log(::vortex::LogLevel::Error, "RHI",                    \
                "%s:%d  Vulkan call failed (%d): %s",                          \
                __FILE__, __LINE__, static_cast<int>(vkr_), #expr);           \
        }                                                                      \
    } while (0)

[[nodiscard]] inline VkFormat toVkFormat(Format f) {
    switch (f) {
        case Format::R8G8B8A8_UNORM:       return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::B8G8R8A8_UNORM:       return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::R8G8B8A8_SRGB:        return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::B8G8R8A8_SRGB:        return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::R32G32_SFLOAT:        return VK_FORMAT_R32G32_SFLOAT;
        case Format::R32G32B32_SFLOAT:     return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::R32G32B32A32_SFLOAT:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R16G16B16A16_SFLOAT:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::D32_SFLOAT:           return VK_FORMAT_D32_SFLOAT;
        case Format::Undefined:            return VK_FORMAT_UNDEFINED;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] inline Format fromVkFormat(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R8G8B8A8_UNORM:  return Format::R8G8B8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_UNORM:  return Format::B8G8R8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:   return Format::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_SRGB:   return Format::B8G8R8A8_SRGB;
        default:                        return Format::Undefined;
    }
}

[[nodiscard]] inline VkFormat toVkFormat(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float1:   return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2:   return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3:   return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4:   return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UNorm4x8: return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::UInt1:    return VK_FORMAT_R32_UINT;
        case VertexFormat::UInt4x8:  return VK_FORMAT_R8G8B8A8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] inline VkSampleCountFlagBits toVkSampleCount(u32 samples) {
    switch (samples) {
        case 2:  return VK_SAMPLE_COUNT_2_BIT;
        case 4:  return VK_SAMPLE_COUNT_4_BIT;
        case 8:  return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

[[nodiscard]] inline VkPresentModeKHR toVkPresentMode(PresentMode m) {
    switch (m) {
        case PresentMode::Fifo:      return VK_PRESENT_MODE_FIFO_KHR;
        case PresentMode::Mailbox:   return VK_PRESENT_MODE_MAILBOX_KHR;
        case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

[[nodiscard]] inline VkPrimitiveTopology toVkTopology(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

[[nodiscard]] inline VkCullModeFlags toVkCull(CullMode c) {
    switch (c) {
        case CullMode::None:  return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

[[nodiscard]] inline VkAttachmentLoadOp toVkLoadOp(LoadOp op) {
    switch (op) {
        case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
        case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
        case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
}

[[nodiscard]] inline VkCompareOp toVkCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never:        return VK_COMPARE_OP_NEVER;
        case CompareOp::Less:         return VK_COMPARE_OP_LESS;
        case CompareOp::Equal:        return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater:      return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always:       return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}

[[nodiscard]] inline bool isDepthFormat(Format f) { return f == Format::D32_SFLOAT; }

[[nodiscard]] inline u32 bytesPerPixel(Format f) {
    switch (f) {
        case Format::R8G8B8A8_UNORM:
        case Format::B8G8R8A8_UNORM:
        case Format::R8G8B8A8_SRGB:
        case Format::B8G8R8A8_SRGB:
        case Format::D32_SFLOAT:          return 4;
        case Format::R32G32_SFLOAT:       return 8;
        case Format::R32G32B32_SFLOAT:    return 12;
        case Format::R32G32B32A32_SFLOAT: return 16;
        case Format::R16G16B16A16_SFLOAT: return 8;
        case Format::Undefined:           return 0;
    }
    return 0;
}

[[nodiscard]] inline VkImageLayout toVkLayout(ResourceState s) {
    switch (s) {
        case ResourceState::Undefined:    return VK_IMAGE_LAYOUT_UNDEFINED;
        case ResourceState::RenderTarget: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ResourceState::DepthTarget:  return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        case ResourceState::ShaderRead:   return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceState::Present:      return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

[[nodiscard]] inline VkAttachmentStoreOp toVkStoreOp(StoreOp op) {
    switch (op) {
        case StoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
        case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_STORE;
}

inline constexpr u32 kFramesInFlight = kMaxFramesInFlight;

}
