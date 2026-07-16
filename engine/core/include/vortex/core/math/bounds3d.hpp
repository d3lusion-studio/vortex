#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

// The 3D counterpart of bounds2d: ray casts and bounding volumes for picking,
// culling and collision broad-phase. Distances come back in the units of the ray's
// direction — pass a unit direction and t is world distance; pass an un-normalized
// one (as happens after transforming a ray into an object's local space) and t
// still lines up with the world ray it came from.

namespace vortex {

struct Ray3D {
    Vec3 origin{0.0f, 0.0f, 0.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f};

    [[nodiscard]] Vec3 at(f32 t) const noexcept { return origin + direction * t; }
};

struct Aabb3D {
    Vec3 min{0.0f, 0.0f, 0.0f};
    Vec3 max{0.0f, 0.0f, 0.0f};

    [[nodiscard]] constexpr Vec3 center()   const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] constexpr Vec3 size()     const noexcept { return max - min; }
    [[nodiscard]] constexpr Vec3 halfSize() const noexcept { return (max - min) * 0.5f; }

    [[nodiscard]] static constexpr Aabb3D fromCenterHalf(Vec3 c, Vec3 half) noexcept {
        return {c - half, c + half};
    }
    [[nodiscard]] static Aabb3D fromPoints(const Vec3* pts, usize count) noexcept {
        if (count == 0) return {};
        Vec3 lo = pts[0], hi = pts[0];
        for (usize i = 1; i < count; ++i) {
            lo = {std::fmin(lo.x, pts[i].x), std::fmin(lo.y, pts[i].y), std::fmin(lo.z, pts[i].z)};
            hi = {std::fmax(hi.x, pts[i].x), std::fmax(hi.y, pts[i].y), std::fmax(hi.z, pts[i].z)};
        }
        return {lo, hi};
    }

    [[nodiscard]] constexpr bool contains(Vec3 p) const noexcept {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
    [[nodiscard]] constexpr Aabb3D merged(const Aabb3D& o) const noexcept {
        return {{std::fmin(min.x, o.min.x), std::fmin(min.y, o.min.y), std::fmin(min.z, o.min.z)},
                {std::fmax(max.x, o.max.x), std::fmax(max.y, o.max.y), std::fmax(max.z, o.max.z)}};
    }
};

struct Sphere3D {
    Vec3 center{0.0f, 0.0f, 0.0f};
    f32  radius = 0.0f;

    [[nodiscard]] bool contains(Vec3 p) const noexcept {
        return dot(p - center, p - center) <= radius * radius;
    }
};

// ---------------------------------------------------------------- overlap tests

[[nodiscard]] constexpr bool intersects(const Aabb3D& a, const Aabb3D& b) noexcept {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

[[nodiscard]] inline bool intersects(const Sphere3D& a, const Sphere3D& b) noexcept {
    const Vec3 d = b.center - a.center;
    const f32  r = a.radius + b.radius;
    return dot(d, d) <= r * r;
}

// ---------------------------------------------------------------- ray casts
// Each returns the distance t >= 0 to the first hit (in units of ray.direction), or
// a negative value for a miss. A ray starting inside a volume hits at t = 0.

[[nodiscard]] inline f32 raycast(const Ray3D& ray, const Aabb3D& box) noexcept {
    const Vec3 inv{1.0f / ray.direction.x, 1.0f / ray.direction.y, 1.0f / ray.direction.z};
    const f32 tx1 = (box.min.x - ray.origin.x) * inv.x, tx2 = (box.max.x - ray.origin.x) * inv.x;
    const f32 ty1 = (box.min.y - ray.origin.y) * inv.y, ty2 = (box.max.y - ray.origin.y) * inv.y;
    const f32 tz1 = (box.min.z - ray.origin.z) * inv.z, tz2 = (box.max.z - ray.origin.z) * inv.z;
    const f32 tmin = std::fmax(std::fmax(std::fmin(tx1, tx2), std::fmin(ty1, ty2)), std::fmin(tz1, tz2));
    const f32 tmax = std::fmin(std::fmin(std::fmax(tx1, tx2), std::fmax(ty1, ty2)), std::fmax(tz1, tz2));
    if (tmax < 0.0f || tmin > tmax) return -1.0f;
    return tmin >= 0.0f ? tmin : 0.0f;
}

[[nodiscard]] inline f32 raycast(const Ray3D& ray, const Sphere3D& sphere) noexcept {
    const Vec3 oc = ray.origin - sphere.center;
    const f32  a  = dot(ray.direction, ray.direction);
    const f32  b  = dot(oc, ray.direction);
    const f32  c  = dot(oc, oc) - sphere.radius * sphere.radius;
    if (c > 0.0f && b > 0.0f) return -1.0f;
    const f32 disc = b * b - a * c;
    if (disc < 0.0f) return -1.0f;
    const f32 t = (-b - std::sqrt(disc)) / a;
    return t >= 0.0f ? t : 0.0f;
}

// Möller–Trumbore. Hits either face (no back-face cull), which is what a picker
// wants. `outNormal`, when given, receives the geometric normal (not area-weighted).
[[nodiscard]] inline f32 rayTriangle(const Ray3D& ray, Vec3 v0, Vec3 v1, Vec3 v2,
                                     Vec3* outNormal = nullptr) noexcept {
    const Vec3 e1 = v1 - v0;
    const Vec3 e2 = v2 - v0;
    const Vec3 p  = cross(ray.direction, e2);
    const f32  det = dot(e1, p);
    if (std::fabs(det) < 1e-8f) return -1.0f;           // ray parallel to the triangle
    const f32  invDet = 1.0f / det;
    const Vec3 tvec = ray.origin - v0;
    const f32  u = dot(tvec, p) * invDet;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    const Vec3 q = cross(tvec, e1);
    const f32  v = dot(ray.direction, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    const f32 t = dot(e2, q) * invDet;
    if (t < 0.0f) return -1.0f;
    if (outNormal) *outNormal = normalize(cross(e1, e2));
    return t;
}

}
