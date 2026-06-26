#pragma once
#include "vortex/core/types.hpp"
#include <cmath>

namespace vortex {
    struct Vec2 {
        f32 x = 0.0f, y = 0.0f;

        constexpr Vec2 operator+(Vec2 r) const noexcept { return {x + r.x, y + r.y}; }
        constexpr Vec2 operator-(Vec2 r) const noexcept { return {x - r.x, y - r.y}; }
        constexpr Vec2 operator*(f32 s)  const noexcept { return {x * s, y * s}; }
        constexpr bool operator==(const Vec2&) const noexcept = default;
    };

    constexpr f32 dot(Vec2 a, Vec2 b) noexcept { return a.x * b.x + a.y * b.y; }
    inline    f32 length(Vec2 v)       noexcept { return std::sqrt(dot(v, v)); }
}
