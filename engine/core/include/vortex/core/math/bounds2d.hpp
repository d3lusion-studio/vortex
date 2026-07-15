#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

// A rigid 2D transform — rotate, then translate — with no scale. It is the pose a
// bounding volume is measured under: give a primitive an Isometry2D and ask for its
// world-space AABB or bounding circle. Kept separate from Mat4 because a bounding
// query never needs a full matrix and a struct of three floats copies for free.
struct Isometry2D {
    Vec2 translation{0.0f, 0.0f};
    f32  rotation = 0.0f;   // radians, counter-clockwise

    [[nodiscard]] static Isometry2D fromTranslation(Vec2 t) noexcept { return {t, 0.0f}; }
    [[nodiscard]] static Isometry2D fromRotation(f32 r) noexcept { return {{0.0f, 0.0f}, r}; }

    // Map a point from local space into world space.
    [[nodiscard]] Vec2 apply(Vec2 local) const noexcept {
        return translation + rotate(local, rotation);
    }
    // Rotate a direction (no translation) — for extents and normals.
    [[nodiscard]] Vec2 applyDir(Vec2 local) const noexcept { return rotate(local, rotation); }
};

// Axis-aligned bounding box stored as min/max corners — the natural form for the
// merge-and-test math of broad-phase culling and bounding queries. Rect is the
// same shape stored as origin+size and pulls double duty as a UV/screen window;
// convert with fromRect()/toRect() when a value needs to cross between the two.
struct Aabb2D {
    Vec2 min{0.0f, 0.0f};
    Vec2 max{0.0f, 0.0f};

    [[nodiscard]] constexpr Vec2 center()   const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] constexpr Vec2 size()     const noexcept { return max - min; }
    [[nodiscard]] constexpr Vec2 halfSize() const noexcept { return (max - min) * 0.5f; }

    [[nodiscard]] static constexpr Aabb2D fromCenterHalf(Vec2 c, Vec2 half) noexcept {
        return {c - half, c + half};
    }
    [[nodiscard]] static constexpr Aabb2D fromRect(const Rect& r) noexcept {
        return {r.min(), r.max()};
    }
    [[nodiscard]] constexpr Rect toRect() const noexcept {
        return Rect::fromMinMax(min, max);
    }

    // Smallest AABB containing every point. Returns an empty box at the origin for
    // an empty span, so a caller can merge into it safely.
    [[nodiscard]] static Aabb2D fromPoints(const Vec2* pts, usize count) noexcept {
        if (count == 0) return {};
        Vec2 lo = pts[0], hi = pts[0];
        for (usize i = 1; i < count; ++i) {
            lo.x = pts[i].x < lo.x ? pts[i].x : lo.x;
            lo.y = pts[i].y < lo.y ? pts[i].y : lo.y;
            hi.x = pts[i].x > hi.x ? pts[i].x : hi.x;
            hi.y = pts[i].y > hi.y ? pts[i].y : hi.y;
        }
        return {lo, hi};
    }

    [[nodiscard]] constexpr bool contains(Vec2 p) const noexcept {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }
    [[nodiscard]] constexpr bool contains(const Aabb2D& o) const noexcept {
        return o.min.x >= min.x && o.min.y >= min.y && o.max.x <= max.x && o.max.y <= max.y;
    }
    [[nodiscard]] constexpr Aabb2D merged(const Aabb2D& o) const noexcept {
        return {{min.x < o.min.x ? min.x : o.min.x, min.y < o.min.y ? min.y : o.min.y},
                {max.x > o.max.x ? max.x : o.max.x, max.y > o.max.y ? max.y : o.max.y}};
    }
    [[nodiscard]] constexpr Aabb2D expanded(f32 amount) const noexcept {
        return {{min.x - amount, min.y - amount}, {max.x + amount, max.y + amount}};
    }
};

// A circle used as a bounding volume: cheaper to test than an AABB and rotation-
// invariant, which is why it wins for anything that spins.
struct BoundingCircle {
    Vec2 center{0.0f, 0.0f};
    f32  radius = 0.0f;

    [[nodiscard]] constexpr bool contains(Vec2 p) const noexcept {
        return lengthSquared(p - center) <= radius * radius;
    }
    [[nodiscard]] constexpr Aabb2D aabb() const noexcept {
        return Aabb2D::fromCenterHalf(center, {radius, radius});
    }

    // A circle enclosing every point: centred on the points' AABB and grown to the
    // farthest one. Not the minimal enclosing circle (that is Welzl's algorithm),
    // but tight enough for culling and computed in a single pass.
    [[nodiscard]] static BoundingCircle fromPoints(const Vec2* pts, usize count) noexcept {
        if (count == 0) return {};
        const Vec2 c = Aabb2D::fromPoints(pts, count).center();
        f32 r2 = 0.0f;
        for (usize i = 0; i < count; ++i) {
            const f32 d2 = lengthSquared(pts[i] - c);
            if (d2 > r2) r2 = d2;
        }
        return {c, std::sqrt(r2)};
    }
};

// A half-line: origin plus a direction that callers are expected to keep normalized
// (the raycast math assumes unit length). t is distance along dir.
struct Ray2D {
    Vec2 origin{0.0f, 0.0f};
    Vec2 direction{1.0f, 0.0f};

    [[nodiscard]] constexpr Vec2 at(f32 t) const noexcept { return origin + direction * t; }
};

// ---------------------------------------------------------------- overlap tests

[[nodiscard]] constexpr bool intersects(const Aabb2D& a, const Aabb2D& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y;
}

[[nodiscard]] constexpr bool intersects(const BoundingCircle& a, const BoundingCircle& b) noexcept {
    const f32 r = a.radius + b.radius;
    return lengthSquared(b.center - a.center) <= r * r;
}

// Circle vs AABB: compare the circle centre against its closest point on the box.
[[nodiscard]] inline bool intersects(const BoundingCircle& c, const Aabb2D& box) noexcept {
    const Vec2 closest{clamp(c.center.x, box.min.x, box.max.x),
                       clamp(c.center.y, box.min.y, box.max.y)};
    return lengthSquared(c.center - closest) <= c.radius * c.radius;
}
[[nodiscard]] inline bool intersects(const Aabb2D& box, const BoundingCircle& c) noexcept {
    return intersects(c, box);
}

// ---------------------------------------------------------------- ray casts
//
// Each returns the distance t >= 0 to the first hit, or a negative value for a miss.
// An origin already inside the volume hits at t = 0.

// Slab method. A component-wise divide by a zero direction yields +/-inf, which the
// min/max below handle correctly, so an axis-aligned ray needs no special case.
[[nodiscard]] inline f32 raycast(const Ray2D& ray, const Aabb2D& box) noexcept {
    const Vec2 inv{1.0f / ray.direction.x, 1.0f / ray.direction.y};
    const f32 tx1 = (box.min.x - ray.origin.x) * inv.x;
    const f32 tx2 = (box.max.x - ray.origin.x) * inv.x;
    const f32 ty1 = (box.min.y - ray.origin.y) * inv.y;
    const f32 ty2 = (box.max.y - ray.origin.y) * inv.y;
    const f32 tmin = std::fmax(std::fmin(tx1, tx2), std::fmin(ty1, ty2));
    const f32 tmax = std::fmin(std::fmax(tx1, tx2), std::fmax(ty1, ty2));
    if (tmax < 0.0f || tmin > tmax) return -1.0f;   // behind, or misses
    return tmin >= 0.0f ? tmin : 0.0f;              // inside -> 0
}

[[nodiscard]] inline f32 raycast(const Ray2D& ray, const BoundingCircle& circle) noexcept {
    const Vec2 oc = ray.origin - circle.center;
    const f32  b  = dot(oc, ray.direction);
    const f32  c  = lengthSquared(oc) - circle.radius * circle.radius;
    if (c > 0.0f && b > 0.0f) return -1.0f;         // outside and pointing away
    const f32 disc = b * b - c;                     // direction is unit, so a = 1
    if (disc < 0.0f) return -1.0f;
    const f32 t = -b - std::sqrt(disc);
    return t >= 0.0f ? t : 0.0f;                    // inside -> 0
}

}
