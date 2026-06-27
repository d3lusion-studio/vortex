#pragma once
#include "vortex/core/types.hpp"

namespace vortex::rhi {

enum class GraphicsAPI { Vulkan };

enum class Format {
    Undefined,
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_SRGB,
    R32G32_SFLOAT,
    R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT,
    D32_SFLOAT,
};

enum class PresentMode { Fifo, Mailbox, Immediate };

enum class BufferUsage : u32 {
    Vertex  = 1u << 0,
    Index   = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    Staging = 1u << 4,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<BufferUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
[[nodiscard]] constexpr bool hasFlag(BufferUsage value, BufferUsage flag) noexcept {
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
}

enum class MemoryDomain {
    Device,  // GPU-only, fastest for the GPU; upload via staging
    Upload,  // CPU-writable, GPU-readable (host-visible); good for per-frame data
};

enum class LoadOp  { Load, Clear, DontCare };
enum class StoreOp { Store, DontCare };

enum class PrimitiveTopology { TriangleList, TriangleStrip, LineList, PointList };

enum class IndexType { U16, U32 };

enum class VertexFormat { Float1, Float2, Float3, Float4, UNorm4x8 };

enum class CullMode { None, Front, Back };

enum class TextureUsage : u32 {
    Sampled      = 1u << 0,
    RenderTarget = 1u << 1,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
[[nodiscard]] constexpr bool hasFlag(TextureUsage value, TextureUsage flag) noexcept {
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
}

enum class Filter      { Nearest, Linear };
enum class AddressMode { Repeat, ClampToEdge, MirroredRepeat };

inline constexpr u32 kMaxFramesInFlight = 2;

}
