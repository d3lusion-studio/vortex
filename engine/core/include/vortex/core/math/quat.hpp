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

    [[nodiscard]] constexpr f32 dot(const Quat& r) const noexcept {
        return x * r.x + y * r.y + z * r.z + w * r.w;
    }

    // Spherical interpolation: the shortest rotation from `a` to `b`, at constant angular
    // speed. This is the whole reason animation keeps rotations as quaternions — lerp a
    // pair of rotation matrices and the result is not a rotation at all, it is a shear.
    //
    // Two details that are not decoration:
    //  - q and -q are the SAME rotation, so if the two are more than 90 degrees apart in
    //    4D one is negated first. Skip that and a joint takes the long way round, spinning
    //    almost all the way about rather than a little way back.
    //  - when the two are nearly equal, sin(theta) goes to zero and the formula blows up;
    //    a plain lerp is indistinguishable there anyway.
    [[nodiscard]] static Quat slerp(Quat a, Quat b, f32 t) noexcept {
        f32 cosTheta = a.dot(b);
        if (cosTheta < 0.0f) {
            b = {-b.x, -b.y, -b.z, -b.w};
            cosTheta = -cosTheta;
        }

        if (cosTheta > 0.9995f) {
            return Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                        a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.normalized();
        }

        const f32 theta    = std::acos(cosTheta);
        const f32 sinTheta = std::sin(theta);
        const f32 wa = std::sin((1.0f - t) * theta) / sinTheta;
        const f32 wb = std::sin(t * theta) / sinTheta;
        return {a.x * wa + b.x * wb, a.y * wa + b.y * wb,
                a.z * wa + b.z * wb, a.w * wa + b.w * wb};
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
