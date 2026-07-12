#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::renderer {

// A window onto a texture: the UV rect plus the page it lives on. This is what a
// sprite actually draws, and what an animation frame resolves to.
struct TextureRegion {
    rhi::TextureHandle texture;
    Rect               uv = kFullUV;
    Vec2               sizePixels{0.0f, 0.0f};   // source size, handy as a default quad size

    [[nodiscard]] constexpr bool valid() const noexcept { return texture.valid(); }
};

// Convert a pixel rect on a texture into normalised UVs.
[[nodiscard]] constexpr Rect pixelsToUV(Rect pixels, u32 textureWidth, u32 textureHeight) noexcept {
    if (textureWidth == 0u || textureHeight == 0u) return kFullUV;
    const f32 invW = 1.0f / static_cast<f32>(textureWidth);
    const f32 invH = 1.0f / static_cast<f32>(textureHeight);
    return {pixels.x * invW, pixels.y * invH, pixels.width * invW, pixels.height * invH};
}

// Uniform grid of frames, the common sprite-sheet layout. Frames are indexed
// row-major from the top-left. `margin` is the border around the whole sheet and
// `spacing` the gutter between cells, both in pixels — leave them at 0 for a
// tightly packed sheet.
struct SpriteSheet {
    rhi::TextureHandle texture;
    u32 textureWidth  = 0;
    u32 textureHeight = 0;
    u32 columns = 1;
    u32 rows    = 1;
    u32 margin  = 0;
    u32 spacing = 0;

    [[nodiscard]] constexpr u32 frameCount() const noexcept { return columns * rows; }

    [[nodiscard]] constexpr Vec2 cellSize() const noexcept {
        if (columns == 0u || rows == 0u) return {};
        const f32 usableW = static_cast<f32>(textureWidth)  - static_cast<f32>(margin * 2u)
                          - static_cast<f32>(spacing * (columns - 1u));
        const f32 usableH = static_cast<f32>(textureHeight) - static_cast<f32>(margin * 2u)
                          - static_cast<f32>(spacing * (rows - 1u));
        return {usableW / static_cast<f32>(columns), usableH / static_cast<f32>(rows)};
    }

    [[nodiscard]] constexpr Rect framePixels(u32 index) const noexcept {
        if (columns == 0u) return {};
        const u32  col  = index % columns;
        const u32  row  = index / columns;
        const Vec2 cell = cellSize();
        return {static_cast<f32>(margin) + static_cast<f32>(col) * (cell.x + static_cast<f32>(spacing)),
                static_cast<f32>(margin) + static_cast<f32>(row) * (cell.y + static_cast<f32>(spacing)),
                cell.x, cell.y};
    }

    [[nodiscard]] constexpr Rect frameUV(u32 index) const noexcept {
        return pixelsToUV(framePixels(index), textureWidth, textureHeight);
    }

    [[nodiscard]] constexpr TextureRegion region(u32 index) const noexcept {
        return {texture, frameUV(index), cellSize()};
    }
};

}
