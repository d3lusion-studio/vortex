#pragma once
#include "vortex/core/types.hpp"

namespace vortex {

struct Vec4 {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

    constexpr Vec4 operator+(Vec4 r) const noexcept { return {x + r.x, y + r.y, z + r.z, w + r.w}; }
    constexpr Vec4 operator-(Vec4 r) const noexcept { return {x - r.x, y - r.y, z - r.z, w - r.w}; }
    constexpr Vec4 operator*(f32 s)  const noexcept { return {x * s, y * s, z * s, w * s}; }
    constexpr bool operator==(const Vec4&) const noexcept = default;
};

}
