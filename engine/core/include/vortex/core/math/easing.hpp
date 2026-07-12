#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

// Easing curves mapping t in [0, 1] to an eased [0, 1]. All are clamped at the
// ends, so callers may pass an unbounded progress value.

namespace vortex::easing {

[[nodiscard]] constexpr f32 linear(f32 t) noexcept { return saturate(t); }

[[nodiscard]] constexpr f32 inQuad(f32 t)  noexcept { t = saturate(t); return t * t; }
[[nodiscard]] constexpr f32 outQuad(f32 t) noexcept { t = saturate(t); return 1.0f - (1.0f - t) * (1.0f - t); }
[[nodiscard]] constexpr f32 inOutQuad(f32 t) noexcept {
    t = saturate(t);
    return t < 0.5f ? 2.0f * t * t : 1.0f - sqr(-2.0f * t + 2.0f) * 0.5f;
}

[[nodiscard]] constexpr f32 inCubic(f32 t)  noexcept { t = saturate(t); return t * t * t; }
[[nodiscard]] constexpr f32 outCubic(f32 t) noexcept {
    t = saturate(t);
    const f32 u = 1.0f - t;
    return 1.0f - u * u * u;
}
[[nodiscard]] constexpr f32 inOutCubic(f32 t) noexcept {
    t = saturate(t);
    if (t < 0.5f) return 4.0f * t * t * t;
    const f32 u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * 0.5f;
}

[[nodiscard]] constexpr f32 inQuart(f32 t)  noexcept { t = saturate(t); return t * t * t * t; }
[[nodiscard]] constexpr f32 outQuart(f32 t) noexcept {
    t = saturate(t);
    const f32 u = 1.0f - t;
    return 1.0f - u * u * u * u;
}
[[nodiscard]] constexpr f32 inOutQuart(f32 t) noexcept {
    t = saturate(t);
    if (t < 0.5f) return 8.0f * t * t * t * t;
    const f32 u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * u * 0.5f;
}

[[nodiscard]] constexpr f32 inQuint(f32 t)  noexcept { t = saturate(t); return t * t * t * t * t; }
[[nodiscard]] constexpr f32 outQuint(f32 t) noexcept {
    t = saturate(t);
    const f32 u = 1.0f - t;
    return 1.0f - u * u * u * u * u;
}
[[nodiscard]] constexpr f32 inOutQuint(f32 t) noexcept {
    t = saturate(t);
    if (t < 0.5f) return 16.0f * t * t * t * t * t;
    const f32 u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * u * u * 0.5f;
}

[[nodiscard]] inline f32 inSine(f32 t)  noexcept { return 1.0f - std::cos(saturate(t) * kHalfPi); }
[[nodiscard]] inline f32 outSine(f32 t) noexcept { return std::sin(saturate(t) * kHalfPi); }
[[nodiscard]] inline f32 inOutSine(f32 t) noexcept {
    return -(std::cos(kPi * saturate(t)) - 1.0f) * 0.5f;
}

[[nodiscard]] inline f32 inExpo(f32 t) noexcept {
    t = saturate(t);
    return t <= 0.0f ? 0.0f : std::exp2(10.0f * t - 10.0f);
}
[[nodiscard]] inline f32 outExpo(f32 t) noexcept {
    t = saturate(t);
    return t >= 1.0f ? 1.0f : 1.0f - std::exp2(-10.0f * t);
}
[[nodiscard]] inline f32 inOutExpo(f32 t) noexcept {
    t = saturate(t);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t < 0.5f ? std::exp2(20.0f * t - 10.0f) * 0.5f
                    : (2.0f - std::exp2(-20.0f * t + 10.0f)) * 0.5f;
}

[[nodiscard]] inline f32 inCirc(f32 t)  noexcept { t = saturate(t); return 1.0f - std::sqrt(1.0f - t * t); }
[[nodiscard]] inline f32 outCirc(f32 t) noexcept { t = saturate(t); return std::sqrt(1.0f - sqr(t - 1.0f)); }
[[nodiscard]] inline f32 inOutCirc(f32 t) noexcept {
    t = saturate(t);
    return t < 0.5f ? (1.0f - std::sqrt(1.0f - sqr(2.0f * t))) * 0.5f
                    : (std::sqrt(1.0f - sqr(-2.0f * t + 2.0f)) + 1.0f) * 0.5f;
}

// Overshoots slightly past the start/end, the classic "anticipate" curve.
[[nodiscard]] constexpr f32 inBack(f32 t) noexcept {
    constexpr f32 c1 = 1.70158f, c3 = c1 + 1.0f;
    t = saturate(t);
    return c3 * t * t * t - c1 * t * t;
}
[[nodiscard]] constexpr f32 outBack(f32 t) noexcept {
    constexpr f32 c1 = 1.70158f, c3 = c1 + 1.0f;
    t = saturate(t);
    const f32 u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
[[nodiscard]] constexpr f32 inOutBack(f32 t) noexcept {
    constexpr f32 c1 = 1.70158f, c2 = c1 * 1.525f;
    t = saturate(t);
    if (t < 0.5f) return sqr(2.0f * t) * ((c2 + 1.0f) * 2.0f * t - c2) * 0.5f;
    return (sqr(2.0f * t - 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
}

[[nodiscard]] inline f32 inElastic(f32 t) noexcept {
    constexpr f32 c4 = kTwoPi / 3.0f;
    t = saturate(t);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return -std::exp2(10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}
[[nodiscard]] inline f32 outElastic(f32 t) noexcept {
    constexpr f32 c4 = kTwoPi / 3.0f;
    t = saturate(t);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return std::exp2(-10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}
[[nodiscard]] inline f32 inOutElastic(f32 t) noexcept {
    constexpr f32 c5 = kTwoPi / 4.5f;
    t = saturate(t);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const f32 s = std::sin((20.0f * t - 11.125f) * c5);
    return t < 0.5f ? -(std::exp2(20.0f * t - 10.0f) * s) * 0.5f
                    :  (std::exp2(-20.0f * t + 10.0f) * s) * 0.5f + 1.0f;
}

[[nodiscard]] constexpr f32 outBounce(f32 t) noexcept {
    constexpr f32 n1 = 7.5625f, d1 = 2.75f;
    t = saturate(t);
    if (t < 1.0f / d1)      return n1 * t * t;
    if (t < 2.0f / d1)      { t -= 1.5f  / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1)      { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}
[[nodiscard]] constexpr f32 inBounce(f32 t) noexcept { return 1.0f - outBounce(1.0f - saturate(t)); }
[[nodiscard]] constexpr f32 inOutBounce(f32 t) noexcept {
    t = saturate(t);
    return t < 0.5f ? (1.0f - outBounce(1.0f - 2.0f * t)) * 0.5f
                    : (1.0f + outBounce(2.0f * t - 1.0f)) * 0.5f;
}

// Runtime-selectable curve, for data-driven tweens.
enum class Ease : u8 {
    Linear,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InQuart, OutQuart, InOutQuart,
    InQuint, OutQuint, InOutQuint,
    InSine, OutSine, InOutSine,
    InExpo, OutExpo, InOutExpo,
    InCirc, OutCirc, InOutCirc,
    InBack, OutBack, InOutBack,
    InElastic, OutElastic, InOutElastic,
    InBounce, OutBounce, InOutBounce,
};

[[nodiscard]] inline f32 evaluate(Ease curve, f32 t) noexcept {
    switch (curve) {
        case Ease::Linear:       return linear(t);
        case Ease::InQuad:       return inQuad(t);
        case Ease::OutQuad:      return outQuad(t);
        case Ease::InOutQuad:    return inOutQuad(t);
        case Ease::InCubic:      return inCubic(t);
        case Ease::OutCubic:     return outCubic(t);
        case Ease::InOutCubic:   return inOutCubic(t);
        case Ease::InQuart:      return inQuart(t);
        case Ease::OutQuart:     return outQuart(t);
        case Ease::InOutQuart:   return inOutQuart(t);
        case Ease::InQuint:      return inQuint(t);
        case Ease::OutQuint:     return outQuint(t);
        case Ease::InOutQuint:   return inOutQuint(t);
        case Ease::InSine:       return inSine(t);
        case Ease::OutSine:      return outSine(t);
        case Ease::InOutSine:    return inOutSine(t);
        case Ease::InExpo:       return inExpo(t);
        case Ease::OutExpo:      return outExpo(t);
        case Ease::InOutExpo:    return inOutExpo(t);
        case Ease::InCirc:       return inCirc(t);
        case Ease::OutCirc:      return outCirc(t);
        case Ease::InOutCirc:    return inOutCirc(t);
        case Ease::InBack:       return inBack(t);
        case Ease::OutBack:      return outBack(t);
        case Ease::InOutBack:    return inOutBack(t);
        case Ease::InElastic:    return inElastic(t);
        case Ease::OutElastic:   return outElastic(t);
        case Ease::InOutElastic: return inOutElastic(t);
        case Ease::InBounce:     return inBounce(t);
        case Ease::OutBounce:    return outBounce(t);
        case Ease::InOutBounce:  return inOutBounce(t);
    }
    return linear(t);
}

// Eased interpolation: ease(a, b, t, Ease::OutCubic) on any lerp-able type.
template <class T>
[[nodiscard]] T ease(const T& a, const T& b, f32 t, Ease curve) noexcept {
    return lerp(a, b, evaluate(curve, t));
}

}
