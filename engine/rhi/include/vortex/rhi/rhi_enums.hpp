#pragma once
#include "vortex/core/types.hpp"

namespace vortex::rhi {

enum class GraphicsAPI { Vulkan, WebGPU };

enum class Format {
    Undefined,
    R8G8B8A8_UNORM,
    B8G8R8A8_UNORM,
    R8G8B8A8_SRGB,
    B8G8R8A8_SRGB,
    R32G32_SFLOAT,
    R32G32B32_SFLOAT,
    R32G32B32A32_SFLOAT,
    // The HDR render-target format. Half floats hold values well past 1.0 while staying
    // blendable and filterable everywhere — unlike RGBA32F, which WebGPU refuses to blend
    // into and will not filter without an optional feature. Prefer this for scene targets.
    R16G16B16A16_SFLOAT,
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

enum class VertexFormat {
    Float1, Float2, Float3, Float4, UNorm4x8, UInt1,
    // Four 8-bit integers, delivered to the shader as a uvec4 WITHOUT being normalised.
    // Skinning joint indices: they are indices, and dividing them by 255 would be absurd.
    UInt4x8,
};

enum class CullMode { None, Front, Back };

// How a pipeline's fragment output is combined with what is already in the target.
// `Opaque` writes over it; the rest are the blends a 3D scene needs for glass,
// glow and shadow-like darkening.
enum class BlendMode {
    Opaque,         // no blending
    Alpha,          // src.a * src + (1 - src.a) * dst  — straight alpha
    Premultiplied,  // src + (1 - src.a) * dst
    Additive,       // src + dst — fire, glow, bloom composite
    Multiply,       // src * dst — tint/darken
};

enum class TextureUsage : u32 {
    Sampled      = 1u << 0,
    RenderTarget = 1u << 1,
    DepthStencil = 1u << 2,
    // Writable by updateTexture() after creation. A texture created with initial
    // pixels gets this implicitly; one that is written later must ask for it.
    CopyDst      = 1u << 3,
    // Readable by readTexture(). Needed to pull rendered pixels back to the CPU —
    // screenshots, image export, and comparing a render against a reference.
    CopySrc      = 1u << 4,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}
[[nodiscard]] constexpr bool hasFlag(TextureUsage value, TextureUsage flag) noexcept {
    return (static_cast<u32>(value) & static_cast<u32>(flag)) != 0;
}

enum class Filter      { Nearest, Linear };
enum class AddressMode { Repeat, ClampToEdge, MirroredRepeat };

enum class CompareOp {
    Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always
};

enum class ResourceState {
    Undefined,
    RenderTarget,
    DepthTarget,
    ShaderRead,
    Present,
};

inline constexpr u32 kMaxFramesInFlight = 2;

}
