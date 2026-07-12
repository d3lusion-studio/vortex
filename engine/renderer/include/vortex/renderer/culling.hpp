#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_item.hpp"

#include <cmath>

namespace vortex::renderer {

// Visibility for 2D quads. A sprite's transform maps the unit quad (corners at
// +/-0.5) into world space, so the exact world AABB of an affine transform is the
// centre column plus the summed absolute row extents — no need to transform and
// compare four corners.

// AABB of the unit quad under `transform`.
[[nodiscard]] inline Rect quadBounds(const Mat4& transform) noexcept {
    const f32 cx = transform.at(0, 3);
    const f32 cy = transform.at(1, 3);
    const f32 hx = 0.5f * (std::fabs(transform.at(0, 0)) + std::fabs(transform.at(0, 1)));
    const f32 hy = 0.5f * (std::fabs(transform.at(1, 0)) + std::fabs(transform.at(1, 1)));
    return {cx - hx, cy - hy, hx * 2.0f, hy * 2.0f};
}

// AABB of the unit quad under `world * scale(size)`. Folding the scale in here
// means a culled sprite never pays for the 4x4 matrix multiply that composing
// those two would cost.
[[nodiscard]] inline Rect quadBounds(const Mat4& world, Vec2 size) noexcept {
    const f32 cx = world.at(0, 3);
    const f32 cy = world.at(1, 3);
    const f32 hx = 0.5f * (std::fabs(world.at(0, 0) * size.x) + std::fabs(world.at(0, 1) * size.y));
    const f32 hy = 0.5f * (std::fabs(world.at(1, 0) * size.x) + std::fabs(world.at(1, 1) * size.y));
    return {cx - hx, cy - hy, hx * 2.0f, hy * 2.0f};
}

// Overlap test written out rather than building a Rect, so the common reject
// short-circuits on the first failing axis.
[[nodiscard]] inline bool quadVisible(const Mat4& world, Vec2 size, const Rect& view) noexcept {
    const f32 cx = world.at(0, 3);
    const f32 hx = 0.5f * (std::fabs(world.at(0, 0) * size.x) + std::fabs(world.at(0, 1) * size.y));
    if (cx - hx > view.right() || cx + hx < view.left()) return false;

    const f32 cy = world.at(1, 3);
    const f32 hy = 0.5f * (std::fabs(world.at(1, 0) * size.x) + std::fabs(world.at(1, 1) * size.y));
    return !(cy - hy > view.bottom() || cy + hy < view.top());
}

[[nodiscard]] inline bool quadVisible(const Mat4& transform, const Rect& view) noexcept {
    return quadVisible(transform, Vec2::one(), view);
}

// Drop off-screen items from an already-built list, in place and order-preserving.
// Returns the surviving count; the caller resizes. Use this only for draw lists
// you did not build yourself — extraction culls earlier and cheaper.
[[nodiscard]] inline usize cullSprites(RenderItem* items, usize count, const Rect& view) noexcept {
    usize kept = 0;
    for (usize i = 0; i < count; ++i) {
        if (!quadVisible(items[i].transform, view)) continue;
        if (kept != i) items[kept] = items[i];
        ++kept;
    }
    return kept;
}

}
