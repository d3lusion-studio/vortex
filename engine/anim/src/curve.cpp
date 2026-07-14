#include "vortex/anim/curve.hpp"

#include <algorithm>
#include <cmath>

namespace vortex::anim {

namespace {

// --- Oklab ----------------------------------------------------------------
//
// A colour space built so that the distance between two colours matches how different they look.
// That is the whole trick: interpolating in it produces a ramp with no dead spot in the middle,
// which is what "a straight line between two colours" is supposed to mean and what RGB does not
// deliver. (Björn Ottosson, 2020.)

struct Oklab { f32 L, a, b; };

Oklab linearToOklab(Vec3 c) {
    const f32 l = 0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z;
    const f32 m = 0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z;
    const f32 s = 0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z;

    const f32 l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
            1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
            0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
}

Vec3 oklabToLinear(Oklab c) {
    const f32 l_ = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
    const f32 m_ = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
    const f32 s_ = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;

    const f32 l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return { 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
}

// --- HSV ------------------------------------------------------------------

struct Hsv { f32 h, s, v; };   // h in [0,1)

Hsv rgbToHsv(Vec3 c) {
    const f32 max = std::max({c.x, c.y, c.z});
    const f32 min = std::min({c.x, c.y, c.z});
    const f32 d   = max - min;

    Hsv out{0.0f, max > 0.0f ? d / max : 0.0f, max};
    if (d <= 0.0f) return out;

    if (max == c.x)      out.h = std::fmod((c.y - c.z) / d, 6.0f);
    else if (max == c.y) out.h = (c.z - c.x) / d + 2.0f;
    else                 out.h = (c.x - c.y) / d + 4.0f;

    out.h /= 6.0f;
    if (out.h < 0.0f) out.h += 1.0f;
    return out;
}

Vec3 hsvToRgb(Hsv c) {
    const f32 h = c.h * 6.0f;
    const f32 f = h - std::floor(h);
    const f32 p = c.v * (1.0f - c.s);
    const f32 q = c.v * (1.0f - c.s * f);
    const f32 t = c.v * (1.0f - c.s * (1.0f - f));

    switch (static_cast<int>(std::floor(h)) % 6) {
        case 0:  return {c.v, t, p};
        case 1:  return {q, c.v, p};
        case 2:  return {p, c.v, t};
        case 3:  return {p, q, c.v};
        case 4:  return {t, p, c.v};
        default: return {c.v, p, q};
    }
}

f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

} // namespace

Color mixColor(Color a, Color b, f32 t, ColorSpace space) {
    t = std::clamp(t, 0.0f, 1.0f);
    const f32 alpha = lerp(a.a, b.a, t);   // alpha is coverage, not colour: always a plain lerp

    const Vec3 ca{a.r, a.g, a.b};
    const Vec3 cb{b.r, b.g, b.b};

    switch (space) {
        case ColorSpace::LinearRgb: {
            const Vec3 c = ca + (cb - ca) * t;
            return {c.x, c.y, c.z, alpha};
        }

        case ColorSpace::Srgb: {
            // Encode, lerp, decode. This is what a naive lerp of two hex codes does, and it is
            // here to be COMPARED against, not recommended.
            const Vec4 sa = linearToSrgb(Vec4{ca.x, ca.y, ca.z, 1.0f});
            const Vec4 sb = linearToSrgb(Vec4{cb.x, cb.y, cb.z, 1.0f});
            const Vec4 s{lerp(sa.x, sb.x, t), lerp(sa.y, sb.y, t), lerp(sa.z, sb.z, t), 1.0f};
            const Vec4 c = srgbToLinear(s);
            return {c.x, c.y, c.z, alpha};
        }

        case ColorSpace::Hsv: {
            const Hsv ha = rgbToHsv(ca);
            const Hsv hb = rgbToHsv(cb);

            // Hue is an ANGLE. Lerping 0.95 to 0.05 the long way sweeps the entire colour wheel
            // — red to red, via green — which is spectacular and never what was asked for.
            f32 dh = hb.h - ha.h;
            if (dh > 0.5f)  dh -= 1.0f;
            if (dh < -0.5f) dh += 1.0f;

            f32 h = ha.h + dh * t;
            h -= std::floor(h);

            const Vec3 c = hsvToRgb({h, lerp(ha.s, hb.s, t), lerp(ha.v, hb.v, t)});
            return {c.x, c.y, c.z, alpha};
        }

        case ColorSpace::Oklab:
        default: {
            const Oklab la = linearToOklab(ca);
            const Oklab lb = linearToOklab(cb);
            const Vec3  c  = oklabToLinear({lerp(la.L, lb.L, t),
                                            lerp(la.a, lb.a, t),
                                            lerp(la.b, lb.b, t)});
            return {c.x, c.y, c.z, alpha};
        }
    }
}

}
