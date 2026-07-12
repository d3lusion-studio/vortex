#pragma once
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

inline constexpr f32 kPi      = 3.14159265358979323846f;
inline constexpr f32 kTwoPi   = kPi * 2.0f;
inline constexpr f32 kHalfPi  = kPi * 0.5f;
inline constexpr f32 kDegToRad = kPi / 180.0f;
inline constexpr f32 kRadToDeg = 180.0f / kPi;
inline constexpr f32 kEpsilon = 1e-6f;

[[nodiscard]] constexpr f32 radians(f32 degrees) noexcept { return degrees * kDegToRad; }
[[nodiscard]] constexpr f32 degrees(f32 radians) noexcept { return radians * kRadToDeg; }

[[nodiscard]] constexpr f32 sqr(f32 x) noexcept { return x * x; }

[[nodiscard]] constexpr f32 sign(f32 x) noexcept {
    return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
}

[[nodiscard]] inline bool nearlyZero(f32 x, f32 epsilon = kEpsilon) noexcept {
    return std::fabs(x) <= epsilon;
}

[[nodiscard]] inline bool nearlyEqual(f32 a, f32 b, f32 epsilon = kEpsilon) noexcept {
    return std::fabs(a - b) <= epsilon;
}

// ---------------------------------------------------------------- clamping

template <class T>
[[nodiscard]] constexpr T clamp(T v, T lo, T hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] constexpr f32 saturate(f32 v) noexcept { return clamp(v, 0.0f, 1.0f); }

// ----------------------------------------------------------- interpolation

// Works for any type with (a + b), (a - b) and (a * f32): f32, Vec2, Vec3, Vec4.
template <class T>
[[nodiscard]] constexpr T lerp(const T& a, const T& b, f32 t) noexcept {
    return a + (b - a) * t;
}

template <class T>
[[nodiscard]] constexpr T lerpClamped(const T& a, const T& b, f32 t) noexcept {
    return lerp(a, b, saturate(t));
}

// Position of v within [a, b], the inverse of lerp. Returns 0 for a degenerate range.
[[nodiscard]] constexpr f32 inverseLerp(f32 a, f32 b, f32 v) noexcept {
    const f32 range = b - a;
    return range == 0.0f ? 0.0f : (v - a) / range;
}

// --------------------------------------------------------------- mapping

// Rescale v from [inMin, inMax] onto [outMin, outMax], extrapolating past the ends.
[[nodiscard]] constexpr f32 remap(f32 v, f32 inMin, f32 inMax, f32 outMin, f32 outMax) noexcept {
    return lerp(outMin, outMax, inverseLerp(inMin, inMax, v));
}

[[nodiscard]] constexpr f32 remapClamped(f32 v, f32 inMin, f32 inMax,
                                         f32 outMin, f32 outMax) noexcept {
    return lerpClamped(outMin, outMax, inverseLerp(inMin, inMax, v));
}

[[nodiscard]] constexpr f32 smoothstep(f32 edge0, f32 edge1, f32 x) noexcept {
    const f32 t = saturate(inverseLerp(edge0, edge1, x));
    return t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] constexpr f32 smootherstep(f32 edge0, f32 edge1, f32 x) noexcept {
    const f32 t = saturate(inverseLerp(edge0, edge1, x));
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Fold x into [lo, hi) — the repeating counterpart of clamp.
[[nodiscard]] inline f32 wrap(f32 x, f32 lo, f32 hi) noexcept {
    const f32 range = hi - lo;
    if (range <= 0.0f) return lo;
    f32 r = std::fmod(x - lo, range);
    if (r < 0.0f) r += range;
    return lo + r;
}

// Bounce x back and forth over [0, length].
[[nodiscard]] inline f32 pingPong(f32 x, f32 length) noexcept {
    if (length <= 0.0f) return 0.0f;
    const f32 t = wrap(x, 0.0f, length * 2.0f);
    return length - std::fabs(t - length);
}

// ------------------------------------------------------------- easing-ish

// Step current towards target by at most maxDelta.
[[nodiscard]] inline f32 moveTowards(f32 current, f32 target, f32 maxDelta) noexcept {
    const f32 delta = target - current;
    if (std::fabs(delta) <= maxDelta) return target;
    return current + sign(delta) * maxDelta;
}

// Framerate-independent exponential approach. lambda is the rate; higher is snappier.
// Unlike lerp(a, b, dt) this converges identically at any dt.
template <class T>
[[nodiscard]] T damp(const T& a, const T& b, f32 lambda, f32 dt) noexcept {
    return lerp(a, b, 1.0f - std::exp(-lambda * dt));
}

// Critically damped spring, the standard camera-follow smoother. velocity is carried
// across calls by the caller and must persist for the lifetime of the motion.
[[nodiscard]] inline f32 smoothDamp(f32 current, f32 target, f32& velocity,
                                    f32 smoothTime, f32 dt,
                                    f32 maxSpeed = 1e30f) noexcept {
    if (dt <= 0.0f) return current;
    smoothTime = smoothTime > 1e-4f ? smoothTime : 1e-4f;
    const f32 omega = 2.0f / smoothTime;
    const f32 x     = omega * dt;
    const f32 decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    f32       delta    = current - target;
    const f32 maxDelta = maxSpeed * smoothTime;
    delta = clamp(delta, -maxDelta, maxDelta);

    const f32 goal = current - delta;
    const f32 temp = (velocity + omega * delta) * dt;
    velocity = (velocity - omega * temp) * decay;
    f32 result = goal + (delta + temp) * decay;

    // Never overshoot past the target.
    if ((target - current > 0.0f) == (result > target)) {
        result   = target;
        velocity = (result - target) / dt;
    }
    return result;
}

// ----------------------------------------------------------------- angles

// Shortest signed rotation from a to b, in (-pi, pi].
[[nodiscard]] inline f32 deltaAngle(f32 a, f32 b) noexcept {
    return wrap(b - a + kPi, 0.0f, kTwoPi) - kPi;
}

[[nodiscard]] inline f32 lerpAngle(f32 a, f32 b, f32 t) noexcept {
    return a + deltaAngle(a, b) * t;
}

[[nodiscard]] inline f32 moveTowardsAngle(f32 current, f32 target, f32 maxDelta) noexcept {
    const f32 delta = deltaAngle(current, target);
    if (std::fabs(delta) <= maxDelta) return current + delta;
    return current + sign(delta) * maxDelta;
}

}
