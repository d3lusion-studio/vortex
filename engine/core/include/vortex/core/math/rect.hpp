#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

namespace vortex {

// Axis-aligned rectangle stored as origin + extent. Doubles as a UV window and as
// a world-space AABB for broad-phase culling; `width`/`height` are assumed
// non-negative, so min is always (x, y).
struct Rect {
    f32 x = 0.0f, y = 0.0f;
    f32 width = 0.0f, height = 0.0f;

    [[nodiscard]] constexpr f32 left()   const noexcept { return x; }
    [[nodiscard]] constexpr f32 right()  const noexcept { return x + width; }
    [[nodiscard]] constexpr f32 top()    const noexcept { return y; }
    [[nodiscard]] constexpr f32 bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr Vec2 min()      const noexcept { return {x, y}; }
    [[nodiscard]] constexpr Vec2 max()      const noexcept { return {x + width, y + height}; }
    [[nodiscard]] constexpr Vec2 position() const noexcept { return {x, y}; }
    [[nodiscard]] constexpr Vec2 size()     const noexcept { return {width, height}; }
    [[nodiscard]] constexpr Vec2 center()   const noexcept {
        return {x + width * 0.5f, y + height * 0.5f};
    }

    [[nodiscard]] constexpr f32  area()    const noexcept { return width * height; }
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return width <= 0.0f || height <= 0.0f; }

    [[nodiscard]] constexpr bool contains(f32 px, f32 py) const noexcept {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    [[nodiscard]] constexpr bool contains(Vec2 p) const noexcept { return contains(p.x, p.y); }

    [[nodiscard]] constexpr bool contains(const Rect& r) const noexcept {
        return r.x >= x && r.y >= y && r.right() <= right() && r.bottom() <= bottom();
    }

    // Touching edges count as overlapping, matching contains() on the boundary.
    [[nodiscard]] constexpr bool intersects(const Rect& r) const noexcept {
        return !(r.x > right() || r.right() < x || r.y > bottom() || r.bottom() < y);
    }

    // Overlapping region; empty (zero-size) when the two do not intersect.
    [[nodiscard]] constexpr Rect intersection(const Rect& r) const noexcept {
        const f32 x0 = x > r.x ? x : r.x;
        const f32 y0 = y > r.y ? y : r.y;
        const f32 x1 = right()  < r.right()  ? right()  : r.right();
        const f32 y1 = bottom() < r.bottom() ? bottom() : r.bottom();
        if (x1 <= x0 || y1 <= y0) return Rect{};
        return {x0, y0, x1 - x0, y1 - y0};
    }

    // Smallest rect containing both.
    [[nodiscard]] constexpr Rect merged(const Rect& r) const noexcept {
        const f32 x0 = x < r.x ? x : r.x;
        const f32 y0 = y < r.y ? y : r.y;
        const f32 x1 = right()  > r.right()  ? right()  : r.right();
        const f32 y1 = bottom() > r.bottom() ? bottom() : r.bottom();
        return {x0, y0, x1 - x0, y1 - y0};
    }

    // Grow on every side by `amount` (negative shrinks).
    [[nodiscard]] constexpr Rect expanded(f32 amount) const noexcept {
        return {x - amount, y - amount, width + amount * 2.0f, height + amount * 2.0f};
    }

    [[nodiscard]] constexpr Rect translated(Vec2 offset) const noexcept {
        return {x + offset.x, y + offset.y, width, height};
    }

    [[nodiscard]] static constexpr Rect fromMinMax(Vec2 lo, Vec2 hi) noexcept {
        return {lo.x, lo.y, hi.x - lo.x, hi.y - lo.y};
    }

    [[nodiscard]] static constexpr Rect fromCenter(Vec2 c, Vec2 extent) noexcept {
        return {c.x - extent.x * 0.5f, c.y - extent.y * 0.5f, extent.x, extent.y};
    }

    constexpr bool operator==(const Rect&) const noexcept = default;
};

inline constexpr Rect kFullUV{0.0f, 0.0f, 1.0f, 1.0f};

}
