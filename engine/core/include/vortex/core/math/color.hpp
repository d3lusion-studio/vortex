#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

// Linear, straight-alpha RGBA. Converts implicitly to Vec4 so it can be handed to
// any renderer API that already takes a Vec4 colour.
struct Color {
    f32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr operator Vec4() const noexcept { return {r, g, b, a}; }   // NOLINT(*-explicit-*)

    constexpr Color operator+(Color c) const noexcept { return {r + c.r, g + c.g, b + c.b, a + c.a}; }
    constexpr Color operator-(Color c) const noexcept { return {r - c.r, g - c.g, b - c.b, a - c.a}; }
    constexpr Color operator*(f32 s)   const noexcept { return {r * s, g * s, b * s, a * s}; }
    constexpr Color operator*(Color c) const noexcept { return {r * c.r, g * c.g, b * c.b, a * c.a}; }

    constexpr bool operator==(const Color&) const noexcept = default;

    [[nodiscard]] constexpr Color withAlpha(f32 alpha) const noexcept { return {r, g, b, alpha}; }

    // 0xRRGGBBAA
    [[nodiscard]] static constexpr Color fromHex(u32 rgba) noexcept {
        return {static_cast<f32>((rgba >> 24) & 0xFFu) / 255.0f,
                static_cast<f32>((rgba >> 16) & 0xFFu) / 255.0f,
                static_cast<f32>((rgba >>  8) & 0xFFu) / 255.0f,
                static_cast<f32>((rgba >>  0) & 0xFFu) / 255.0f};
    }

    // 0xRRGGBB, alpha forced opaque.
    [[nodiscard]] static constexpr Color fromRgb(u32 rgb) noexcept {
        return fromHex((rgb << 8) | 0xFFu);
    }

    [[nodiscard]] static constexpr Color fromBytes(u8 rr, u8 gg, u8 bb, u8 aa = 255) noexcept {
        return {static_cast<f32>(rr) / 255.0f, static_cast<f32>(gg) / 255.0f,
                static_cast<f32>(bb) / 255.0f, static_cast<f32>(aa) / 255.0f};
    }

    // hue in [0, 360) degrees, saturation and value in [0, 1].
    [[nodiscard]] static Color fromHsv(f32 hue, f32 saturation, f32 value, f32 alpha = 1.0f) noexcept {
        const f32 h = wrap(hue, 0.0f, 360.0f) / 60.0f;
        const f32 s = saturate(saturation);
        const f32 v = saturate(value);

        const f32 c = v * s;
        const f32 x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
        const f32 m = v - c;

        f32 rr = 0.0f, gg = 0.0f, bb = 0.0f;
        switch (static_cast<int>(h)) {
            case 0:  rr = c; gg = x; break;
            case 1:  rr = x; gg = c; break;
            case 2:  gg = c; bb = x; break;
            case 3:  gg = x; bb = c; break;
            case 4:  rr = x; bb = c; break;
            default: rr = c; bb = x; break;
        }
        return {rr + m, gg + m, bb + m, alpha};
    }

    [[nodiscard]] static constexpr Color white()       noexcept { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color black()       noexcept { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color transparent() noexcept { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    [[nodiscard]] static constexpr Color red()         noexcept { return {1.0f, 0.0f, 0.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color green()       noexcept { return {0.0f, 1.0f, 0.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color blue()        noexcept { return {0.0f, 0.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color yellow()      noexcept { return {1.0f, 1.0f, 0.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color cyan()        noexcept { return {0.0f, 1.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color magenta()     noexcept { return {1.0f, 0.0f, 1.0f, 1.0f}; }
    [[nodiscard]] static constexpr Color gray(f32 v)   noexcept { return {v, v, v, 1.0f}; }
};

[[nodiscard]] constexpr Color operator*(f32 s, Color c) noexcept { return c * s; }

}
