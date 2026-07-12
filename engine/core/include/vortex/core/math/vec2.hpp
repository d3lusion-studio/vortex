#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {
    struct Vec2 {
        f32 x = 0.0f, y = 0.0f;

        constexpr Vec2 operator+(Vec2 r) const noexcept { return {x + r.x, y + r.y}; }
        constexpr Vec2 operator-(Vec2 r) const noexcept { return {x - r.x, y - r.y}; }
        constexpr Vec2 operator*(f32 s)  const noexcept { return {x * s, y * s}; }
        constexpr Vec2 operator/(f32 s)  const noexcept { return {x / s, y / s}; }
        constexpr Vec2 operator-()       const noexcept { return {-x, -y}; }

        // Component-wise, for non-uniform scaling.
        constexpr Vec2 operator*(Vec2 r) const noexcept { return {x * r.x, y * r.y}; }
        constexpr Vec2 operator/(Vec2 r) const noexcept { return {x / r.x, y / r.y}; }

        constexpr Vec2& operator+=(Vec2 r) noexcept { x += r.x; y += r.y; return *this; }
        constexpr Vec2& operator-=(Vec2 r) noexcept { x -= r.x; y -= r.y; return *this; }
        constexpr Vec2& operator*=(f32 s)  noexcept { x *= s;   y *= s;   return *this; }
        constexpr Vec2& operator/=(f32 s)  noexcept { x /= s;   y /= s;   return *this; }
        constexpr Vec2& operator*=(Vec2 r) noexcept { x *= r.x; y *= r.y; return *this; }

        constexpr bool operator==(const Vec2&) const noexcept = default;

        [[nodiscard]] static constexpr Vec2 zero()  noexcept { return {0.0f, 0.0f}; }
        [[nodiscard]] static constexpr Vec2 one()   noexcept { return {1.0f, 1.0f}; }
        [[nodiscard]] static constexpr Vec2 unitX() noexcept { return {1.0f, 0.0f}; }
        [[nodiscard]] static constexpr Vec2 unitY() noexcept { return {0.0f, 1.0f}; }

        // Unit vector pointing at `radians`, measured counter-clockwise from +X.
        [[nodiscard]] static Vec2 fromAngle(f32 radians) noexcept {
            return {std::cos(radians), std::sin(radians)};
        }
    };

    [[nodiscard]] constexpr Vec2 operator*(f32 s, Vec2 v) noexcept { return {v.x * s, v.y * s}; }

    [[nodiscard]] constexpr f32 dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }

    // Z component of the 3D cross product; the signed area of the parallelogram.
    // Positive when b is counter-clockwise from a.
    [[nodiscard]] constexpr f32 cross(Vec2 a, Vec2 b) noexcept { return a.x * b.y - a.y * b.x; }

    [[nodiscard]] constexpr f32 lengthSquared(Vec2 v) noexcept { return dot(v, v); }
    [[nodiscard]] inline    f32 length(Vec2 v)        noexcept { return std::sqrt(dot(v, v)); }

    [[nodiscard]] constexpr f32 distanceSquared(Vec2 a, Vec2 b) noexcept { return lengthSquared(b - a); }
    [[nodiscard]] inline    f32 distance(Vec2 a, Vec2 b)        noexcept { return length(b - a); }

    // Returns the zero vector when v has no direction to preserve.
    [[nodiscard]] inline Vec2 normalize(Vec2 v) noexcept {
        const f32 len = length(v);
        return len > 0.0f ? v * (1.0f / len) : Vec2{};
    }

    [[nodiscard]] inline Vec2 normalizeOr(Vec2 v, Vec2 fallback) noexcept {
        const f32 len = length(v);
        return len > 0.0f ? v * (1.0f / len) : fallback;
    }

    // v rotated 90 degrees counter-clockwise; the 2D normal of a segment.
    [[nodiscard]] constexpr Vec2 perp(Vec2 v) noexcept { return {-v.y, v.x}; }

    [[nodiscard]] inline Vec2 rotate(Vec2 v, f32 radians) noexcept {
        const f32 c = std::cos(radians), s = std::sin(radians);
        return {v.x * c - v.y * s, v.x * s + v.y * c};
    }

    [[nodiscard]] inline Vec2 rotateAround(Vec2 v, Vec2 pivot, f32 radians) noexcept {
        return pivot + rotate(v - pivot, radians);
    }

    // Counter-clockwise angle from +X, in (-pi, pi].
    [[nodiscard]] inline f32 angle(Vec2 v) noexcept { return std::atan2(v.y, v.x); }

    // Signed angle that rotates a onto b, in (-pi, pi].
    [[nodiscard]] inline f32 angleBetween(Vec2 a, Vec2 b) noexcept {
        return std::atan2(cross(a, b), dot(a, b));
    }

    // Mirror v about the surface with unit normal n.
    [[nodiscard]] constexpr Vec2 reflect(Vec2 v, Vec2 n) noexcept {
        return v - n * (2.0f * dot(v, n));
    }

    // Component of v lying along onto. Zero when onto is degenerate.
    [[nodiscard]] constexpr Vec2 project(Vec2 v, Vec2 onto) noexcept {
        const f32 lenSq = lengthSquared(onto);
        return lenSq > 0.0f ? onto * (dot(v, onto) / lenSq) : Vec2{};
    }

    [[nodiscard]] inline Vec2 clampLength(Vec2 v, f32 maxLength) noexcept {
        const f32 lenSq = lengthSquared(v);
        if (lenSq <= maxLength * maxLength || lenSq == 0.0f) return v;
        return v * (maxLength / std::sqrt(lenSq));
    }

    [[nodiscard]] inline Vec2 moveTowards(Vec2 current, Vec2 target, f32 maxDelta) noexcept {
        const Vec2 delta = target - current;
        const f32  lenSq = lengthSquared(delta);
        if (lenSq == 0.0f || lenSq <= maxDelta * maxDelta) return target;
        return current + delta * (maxDelta / std::sqrt(lenSq));
    }

    [[nodiscard]] constexpr Vec2 minComponents(Vec2 a, Vec2 b) noexcept {
        return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y};
    }

    [[nodiscard]] constexpr Vec2 maxComponents(Vec2 a, Vec2 b) noexcept {
        return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y};
    }

    [[nodiscard]] inline Vec2 abs(Vec2 v) noexcept { return {std::fabs(v.x), std::fabs(v.y)}; }

    [[nodiscard]] inline bool nearlyEqual(Vec2 a, Vec2 b, f32 epsilon = kEpsilon) noexcept {
        return nearlyEqual(a.x, b.x, epsilon) && nearlyEqual(a.y, b.y, epsilon);
    }
}
