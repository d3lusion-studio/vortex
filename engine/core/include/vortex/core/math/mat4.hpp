#pragma once
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

struct Mat4 {
    f32 m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };

    [[nodiscard]] constexpr f32&       at(int r, int c)       noexcept { return m[c * 4 + r]; }
    [[nodiscard]] constexpr const f32& at(int r, int c) const noexcept { return m[c * 4 + r]; }

    [[nodiscard]] static constexpr Mat4 identity() noexcept { return Mat4{}; }

    [[nodiscard]] static constexpr Mat4 translation(f32 x, f32 y, f32 z) noexcept {
        Mat4 r;
        r.m[12] = x;
        r.m[13] = y;
        r.m[14] = z;
        return r;
    }

    [[nodiscard]] static constexpr Mat4 scaling(f32 x, f32 y, f32 z) noexcept {
        Mat4 r;
        r.m[0]  = x;
        r.m[5]  = y;
        r.m[10] = z;
        return r;
    }

    // Rotation about the +Z axis (counter-clockwise), the 2D rotation.
    [[nodiscard]] static Mat4 rotationZ(f32 radians) noexcept {
        const f32 c = std::cos(radians);
        const f32 s = std::sin(radians);
        Mat4 r;
        r.at(0, 0) = c; r.at(0, 1) = -s;
        r.at(1, 0) = s; r.at(1, 1) =  c;
        return r;
    }

    [[nodiscard]] static constexpr Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top,
                                              f32 nearZ, f32 farZ) noexcept {
        Mat4 r;
        r.at(0, 0) = 2.0f / (right - left);
        r.at(1, 1) = -2.0f / (top - bottom);
        r.at(2, 2) = 1.0f / (farZ - nearZ);
        r.at(0, 3) = -(right + left) / (right - left);
        r.at(1, 3) = (top + bottom) / (top - bottom);
        r.at(2, 3) = -nearZ / (farZ - nearZ);
        r.at(3, 3) = 1.0f;
        return r;
    }

    [[nodiscard]] constexpr Mat4 operator*(const Mat4& b) const noexcept {
        Mat4 out;
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                f32 sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += at(r, k) * b.at(k, c);
                out.at(r, c) = sum;
            }
        }
        return out;
    }

    [[nodiscard]] constexpr Vec4 operator*(const Vec4& v) const noexcept {
        return {
            at(0, 0) * v.x + at(0, 1) * v.y + at(0, 2) * v.z + at(0, 3) * v.w,
            at(1, 0) * v.x + at(1, 1) * v.y + at(1, 2) * v.z + at(1, 3) * v.w,
            at(2, 0) * v.x + at(2, 1) * v.y + at(2, 2) * v.z + at(2, 3) * v.w,
            at(3, 0) * v.x + at(3, 1) * v.y + at(3, 2) * v.z + at(3, 3) * v.w,
        };
    }
};

}
