#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

// ---------------------------------------------------------------------------
// sRGB <-> linear
//
// A colour has two honest meanings and they are not interchangeable. The bytes in a PNG,
// the hex you type from a palette and the value a monitor shows are all *sRGB-encoded*:
// non-linear, weighted so that the 256 steps land where the eye can tell them apart.
// Light, on the other hand, adds up linearly — so blending, lighting, filtering and
// bloom are only correct on *linear* values.
//
// The engine renders in linear and converts at the two edges: textures decode on sample
// (the GPU does it, given an _SRGB format) and the frame encodes on write (the GPU again,
// given an sRGB backbuffer). These functions are the CPU end of the same boundary — they
// are what turns an authored colour into light.
//
// Get this wrong in either direction and everything looks washed out or muddy, which is
// the single most common way a renderer ends up subtly ugly.
// ---------------------------------------------------------------------------

[[nodiscard]] inline f32 srgbToLinear(f32 c) noexcept {
    // The exact piecewise sRGB curve, not the 2.2 gamma approximation: they disagree most
    // in the darks, which is where the eye is most sensitive and where the approximation
    // therefore does the most visible damage.
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] inline f32 linearToSrgb(f32 c) noexcept {
    return c <= 0.0031308f ? c * 12.92f
                           : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Alpha is a coverage fraction, not a colour, so it is never encoded and never converted.
[[nodiscard]] inline Vec4 srgbToLinear(Vec4 c) noexcept {
    return {srgbToLinear(c.x), srgbToLinear(c.y), srgbToLinear(c.z), c.w};
}

[[nodiscard]] inline Vec4 linearToSrgb(Vec4 c) noexcept {
    return {linearToSrgb(c.x), linearToSrgb(c.y), linearToSrgb(c.z), c.w};
}

// Linear, straight-alpha RGBA — and it really is linear, because every named constructor
// below decodes the sRGB values you hand it. fromRgb(0x808080) is not 0.5: mid grey on a
// screen is 0.216 of the light of white, and that is the number the renderer needs.
//
// To author a value that is ALREADY linear (an HDR intensity, a physical measurement),
// use fromLinear() or brace-initialise the members directly — those two paths are the only
// ones that do not convert.
struct Color {
    f32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    constexpr operator Vec4() const noexcept { return {r, g, b, a}; }   // NOLINT(*-explicit-*)

    constexpr Color operator+(Color c) const noexcept { return {r + c.r, g + c.g, b + c.b, a + c.a}; }
    constexpr Color operator-(Color c) const noexcept { return {r - c.r, g - c.g, b - c.b, a - c.a}; }
    constexpr Color operator*(f32 s)   const noexcept { return {r * s, g * s, b * s, a * s}; }
    constexpr Color operator*(Color c) const noexcept { return {r * c.r, g * c.g, b * c.b, a * c.a}; }

    constexpr bool operator==(const Color&) const noexcept = default;

    [[nodiscard]] constexpr Color withAlpha(f32 alpha) const noexcept { return {r, g, b, alpha}; }

    // Components that are already linear light. The one constructor that does not convert.
    [[nodiscard]] static constexpr Color fromLinear(f32 rr, f32 gg, f32 bb, f32 aa = 1.0f) noexcept {
        return {rr, gg, bb, aa};
    }

    // 0xRRGGBBAA, as you would write it from a palette or a design tool — i.e. sRGB.
    [[nodiscard]] static Color fromHex(u32 rgba) noexcept {
        return fromBytes(static_cast<u8>((rgba >> 24) & 0xFFu),
                         static_cast<u8>((rgba >> 16) & 0xFFu),
                         static_cast<u8>((rgba >>  8) & 0xFFu),
                         static_cast<u8>((rgba >>  0) & 0xFFu));
    }

    // 0xRRGGBB, alpha forced opaque.
    [[nodiscard]] static Color fromRgb(u32 rgb) noexcept {
        return fromHex((rgb << 8) | 0xFFu);
    }

    // The eight-bit values of an image or a colour picker: sRGB, so they are decoded.
    [[nodiscard]] static Color fromBytes(u8 rr, u8 gg, u8 bb, u8 aa = 255) noexcept {
        return {srgbToLinear(static_cast<f32>(rr) / 255.0f),
                srgbToLinear(static_cast<f32>(gg) / 255.0f),
                srgbToLinear(static_cast<f32>(bb) / 255.0f),
                static_cast<f32>(aa) / 255.0f};
    }

    // hue in [0, 360) degrees, saturation and value in [0, 1]. HSV is a way of picking an
    // sRGB colour, so the result is decoded like any other authored value.
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
        return {srgbToLinear(rr + m), srgbToLinear(gg + m), srgbToLinear(bb + m), alpha};
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
    // `v` is how bright the grey LOOKS, in [0,1] — gray(0.5) is mid grey on screen, which
    // carries 21.6% of white's light. The constants above need no such treatment: 0 and 1
    // are the two values the two encodings agree on.
    [[nodiscard]] static Color gray(f32 v) noexcept {
        const f32 l = srgbToLinear(v);
        return {l, l, l, 1.0f};
    }
};

[[nodiscard]] constexpr Color operator*(f32 s, Color c) noexcept { return c * s; }

}
