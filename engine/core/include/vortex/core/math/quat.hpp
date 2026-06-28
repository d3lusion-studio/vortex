#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

struct Quat {
    f32 x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;   // identity

    [[nodiscard]] static constexpr Quat identity() noexcept { return {}; }

    [[nodiscard]] static Quat fromAxisAngle(Vec3 axis, f32 radians) noexcept {
        const Vec3 n = normalize(axis);
        const f32  h = radians * 0.5f;
        const f32  s = std::sin(h);
        return {n.x * s, n.y * s, n.z * s, std::cos(h)};
    }

    [[nodiscard]] constexpr Quat operator*(const Quat& r) const noexcept {
        return {
            w * r.x + x * r.w + y * r.z - z * r.y,
            w * r.y - x * r.z + y * r.w + z * r.x,
            w * r.z + x * r.y - y * r.x + z * r.w,
            w * r.w - x * r.x - y * r.y - z * r.z,
        };
    }

    [[nodiscard]] Quat normalized() const noexcept {
        const f32 len = std::sqrt(x * x + y * y + z * z + w * w);
        if (len <= 0.0f) return identity();
        const f32 inv = 1.0f / len;
        return {x * inv, y * inv, z * inv, w * inv};
    }

    // Rotation matrix for this quaternion (assumed normalized).
    [[nodiscard]] Mat4 toMat4() const noexcept {
        const f32 xx = x * x, yy = y * y, zz = z * z;
        const f32 xy = x * y, xz = x * z, yz = y * z;
        const f32 wx = w * x, wy = w * y, wz = w * z;
        Mat4 r;
        r.at(0, 0) = 1.0f - 2.0f * (yy + zz);
        r.at(0, 1) = 2.0f * (xy - wz);
        r.at(0, 2) = 2.0f * (xz + wy);
        r.at(1, 0) = 2.0f * (xy + wz);
        r.at(1, 1) = 1.0f - 2.0f * (xx + zz);
        r.at(1, 2) = 2.0f * (yz - wx);
        r.at(2, 0) = 2.0f * (xz - wy);
        r.at(2, 1) = 2.0f * (yz + wx);
        r.at(2, 2) = 1.0f - 2.0f * (xx + yy);
        return r;
    }
};

}
