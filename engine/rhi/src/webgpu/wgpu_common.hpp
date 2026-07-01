#pragma once
// The ONLY place (besides the other webgpu/ sources) allowed to include the
// wgpu-native headers. Everything above the RHI talks in rhi:: types.
#include <webgpu.h>
#include <wgpu.h>

#include "vortex/core/log.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_types.hpp"

namespace vortex::rhi::wgpu {

[[nodiscard]] inline WGPUTextureFormat toWGPUFormat(Format f) {
    switch (f) {
        case Format::R8G8B8A8_UNORM:       return WGPUTextureFormat_RGBA8Unorm;
        case Format::B8G8R8A8_UNORM:       return WGPUTextureFormat_BGRA8Unorm;
        case Format::R8G8B8A8_SRGB:        return WGPUTextureFormat_RGBA8UnormSrgb;
        case Format::B8G8R8A8_SRGB:        return WGPUTextureFormat_BGRA8UnormSrgb;
        case Format::R32G32_SFLOAT:        return WGPUTextureFormat_RG32Float;
        case Format::R32G32B32A32_SFLOAT:  return WGPUTextureFormat_RGBA32Float;
        case Format::D32_SFLOAT:           return WGPUTextureFormat_Depth32Float;
        case Format::R32G32B32_SFLOAT:     // no 3-component render format in WebGPU
        case Format::Undefined:            break;
    }
    return WGPUTextureFormat_Undefined;
}

[[nodiscard]] inline Format fromWGPUFormat(WGPUTextureFormat f) {
    switch (f) {
        case WGPUTextureFormat_RGBA8Unorm:     return Format::R8G8B8A8_UNORM;
        case WGPUTextureFormat_BGRA8Unorm:     return Format::B8G8R8A8_UNORM;
        case WGPUTextureFormat_RGBA8UnormSrgb: return Format::R8G8B8A8_SRGB;
        case WGPUTextureFormat_BGRA8UnormSrgb: return Format::B8G8R8A8_SRGB;
        case WGPUTextureFormat_Depth32Float:   return Format::D32_SFLOAT;
        default:                               return Format::Undefined;
    }
}

[[nodiscard]] inline bool isDepthFormat(Format f) { return f == Format::D32_SFLOAT; }

[[nodiscard]] inline WGPUVertexFormat toWGPUVertexFormat(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float1:   return WGPUVertexFormat_Float32;
        case VertexFormat::Float2:   return WGPUVertexFormat_Float32x2;
        case VertexFormat::Float3:   return WGPUVertexFormat_Float32x3;
        case VertexFormat::Float4:   return WGPUVertexFormat_Float32x4;
        case VertexFormat::UNorm4x8: return WGPUVertexFormat_Unorm8x4;
        case VertexFormat::UInt1:    return WGPUVertexFormat_Uint32;
    }
    return WGPUVertexFormat_Float32;
}

[[nodiscard]] inline WGPUPrimitiveTopology toWGPUTopology(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::TriangleList:  return WGPUPrimitiveTopology_TriangleList;
        case PrimitiveTopology::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
        case PrimitiveTopology::LineList:      return WGPUPrimitiveTopology_LineList;
        case PrimitiveTopology::PointList:     return WGPUPrimitiveTopology_PointList;
    }
    return WGPUPrimitiveTopology_TriangleList;
}

[[nodiscard]] inline WGPUCullMode toWGPUCull(CullMode c) {
    switch (c) {
        case CullMode::None:  return WGPUCullMode_None;
        case CullMode::Front: return WGPUCullMode_Front;
        case CullMode::Back:  return WGPUCullMode_Back;
    }
    return WGPUCullMode_None;
}

[[nodiscard]] inline WGPUCompareFunction toWGPUCompare(CompareOp c) {
    switch (c) {
        case CompareOp::Never:        return WGPUCompareFunction_Never;
        case CompareOp::Less:         return WGPUCompareFunction_Less;
        case CompareOp::Equal:        return WGPUCompareFunction_Equal;
        case CompareOp::LessEqual:    return WGPUCompareFunction_LessEqual;
        case CompareOp::Greater:      return WGPUCompareFunction_Greater;
        case CompareOp::NotEqual:     return WGPUCompareFunction_NotEqual;
        case CompareOp::GreaterEqual: return WGPUCompareFunction_GreaterEqual;
        case CompareOp::Always:       return WGPUCompareFunction_Always;
    }
    return WGPUCompareFunction_Always;
}

[[nodiscard]] inline WGPUFilterMode toWGPUFilter(Filter f) {
    return f == Filter::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
}
[[nodiscard]] inline WGPUMipmapFilterMode toWGPUMipFilter(Filter f) {
    return f == Filter::Linear ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
}

[[nodiscard]] inline WGPUAddressMode toWGPUAddress(AddressMode m) {
    switch (m) {
        case AddressMode::Repeat:         return WGPUAddressMode_Repeat;
        case AddressMode::ClampToEdge:    return WGPUAddressMode_ClampToEdge;
        case AddressMode::MirroredRepeat: return WGPUAddressMode_MirrorRepeat;
    }
    return WGPUAddressMode_ClampToEdge;
}

[[nodiscard]] inline WGPULoadOp toWGPULoadOp(LoadOp op) {
    return op == LoadOp::Clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
}
[[nodiscard]] inline WGPUStoreOp toWGPUStoreOp(StoreOp op) {
    return op == StoreOp::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
}

[[nodiscard]] inline WGPUPresentMode toWGPUPresentMode(PresentMode m) {
    switch (m) {
        case PresentMode::Fifo:      return WGPUPresentMode_Fifo;
        case PresentMode::Mailbox:   return WGPUPresentMode_Mailbox;
        case PresentMode::Immediate: return WGPUPresentMode_Immediate;
    }
    return WGPUPresentMode_Fifo;
}

}
