#pragma once
#include "vortex/core/types.hpp"
#include <cmath>

namespace vortex {

struct Vec3 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3 operator+(Vec3 r) const noexcept { return {x + r.x, y + r.y, z + r.z}; }
    constexpr Vec3 operator-(Vec3 r) const noexcept { return {x - r.x, y - r.y, z - r.z}; }
    constexpr Vec3 operator*(f32 s)  const noexcept { return {x * s, y * s, z * s}; }
    constexpr bool operator==(const Vec3&) const noexcept = default;
};

constexpr f32 dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline    f32 length(Vec3 v)       noexcept { return std::sqrt(dot(v, v)); }

}
