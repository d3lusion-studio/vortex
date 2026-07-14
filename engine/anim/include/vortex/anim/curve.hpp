#pragma once
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/easing.hpp"
#include "vortex/core/math/quat.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"

#include <algorithm>
#include <vector>

namespace vortex::anim {

// The full set of easing curves already lives in core; a curve does not need its own.
using easing::Ease;

// A keyframed value over time — the thing that animates anything that is not a skeleton: a
// door's rotation, a UI panel's opacity, a colour, a camera's field of view.
//
// It is a template because the interpolation is the only part that differs by type, and that
// part is a single overload of `curveMix`. Everything else — where the keys are, how to find
// the one before `t`, how the easing bends the segment — is identical whether the value is a
// float or a quaternion, and writing it four times is how the four copies drift apart.

// How a segment between two keys is traversed.
enum class CurveInterp : u8 {
    Step,          // hold the previous key. Not a degenerate Linear — it is what you use when a
                   // value must NOT be interpolated (a state index, a visibility flag).
    Linear,
    CatmullRom,    // a spline through the keys. Smooth velocity across a key, which Linear is
                   // not: Linear gives every key a corner, and a camera flown along one visibly
                   // jerks at each waypoint.
};

// The interpolation of one value type. Overloads, not a template body, so a type the engine has
// no opinion about fails to compile rather than being silently lerped as if it were a number.
[[nodiscard]] inline f32  curveMix(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
[[nodiscard]] inline Vec2 curveMix(Vec2 a, Vec2 b, f32 t) {
    return {curveMix(a.x, b.x, t), curveMix(a.y, b.y, t)};
}
[[nodiscard]] inline Vec3 curveMix(Vec3 a, Vec3 b, f32 t) {
    return {curveMix(a.x, b.x, t), curveMix(a.y, b.y, t), curveMix(a.z, b.z, t)};
}
[[nodiscard]] inline Vec4 curveMix(Vec4 a, Vec4 b, f32 t) {
    return {curveMix(a.x, b.x, t), curveMix(a.y, b.y, t),
            curveMix(a.z, b.z, t), curveMix(a.w, b.w, t)};
}
// A rotation is not four numbers. See Quat::slerp.
[[nodiscard]] inline Quat curveMix(Quat a, Quat b, f32 t) { return Quat::slerp(a, b, t); }

// Catmull-Rom needs the neighbours on either side, and a way to scale and add — which a
// quaternion does not have. So a Quat curve falls back to slerp on a CatmullRom segment rather
// than producing a rotation that is not one.
[[nodiscard]] inline f32 curveSpline(f32 p0, f32 p1, f32 p2, f32 p3, f32 t) {
    const f32 t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                   (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}
[[nodiscard]] inline Vec3 curveSpline(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3, f32 t) {
    return {curveSpline(p0.x, p1.x, p2.x, p3.x, t),
            curveSpline(p0.y, p1.y, p2.y, p3.y, t),
            curveSpline(p0.z, p1.z, p2.z, p3.z, t)};
}
[[nodiscard]] inline Vec4 curveSpline(Vec4 p0, Vec4 p1, Vec4 p2, Vec4 p3, f32 t) {
    return {curveSpline(p0.x, p1.x, p2.x, p3.x, t),
            curveSpline(p0.y, p1.y, p2.y, p3.y, t),
            curveSpline(p0.z, p1.z, p2.z, p3.z, t),
            curveSpline(p0.w, p1.w, p2.w, p3.w, t)};
}
[[nodiscard]] inline Vec2 curveSpline(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, f32 t) {
    return {curveSpline(p0.x, p1.x, p2.x, p3.x, t), curveSpline(p0.y, p1.y, p2.y, p3.y, t)};
}
[[nodiscard]] inline Quat curveSpline(Quat, Quat p1, Quat p2, Quat, f32 t) {
    return Quat::slerp(p1, p2, t);
}

template <typename T>
class Curve {
public:
    struct Key {
        f32  time = 0.0f;
        T    value{};
        // The easing applied WITHIN the segment that starts at this key. Per-key, not per-curve,
        // because "ease out of this one, snap into the next" is a thing an animator wants and a
        // single curve-wide setting cannot express.
        Ease ease = Ease::Linear;
    };

    CurveInterp interp = CurveInterp::Linear;
    bool        loop   = false;

    Curve& add(f32 time, T value, Ease ease = Ease::Linear) {
        m_keys.push_back({time, value, ease});
        std::sort(m_keys.begin(), m_keys.end(),
                  [](const Key& a, const Key& b) { return a.time < b.time; });
        return *this;
    }

    [[nodiscard]] bool empty() const { return m_keys.empty(); }
    [[nodiscard]] usize size() const { return m_keys.size(); }
    [[nodiscard]] const std::vector<Key>& keys() const { return m_keys; }

    [[nodiscard]] f32 duration() const {
        return m_keys.empty() ? 0.0f : m_keys.back().time;
    }

    [[nodiscard]] T evaluate(f32 time) const {
        if (m_keys.empty()) return T{};
        if (m_keys.size() == 1) return m_keys[0].value;

        const f32 start = m_keys.front().time;
        const f32 end   = m_keys.back().time;

        if (loop && end > start) {
            const f32 span = end - start;
            time = start + std::fmod(time - start, span);
            if (time < start) time += span;
        }

        if (time <= start) return m_keys.front().value;
        if (time >= end)   return m_keys.back().value;

        // The key at or before `time`.
        usize i = 0;
        while (i + 1 < m_keys.size() && m_keys[i + 1].time <= time) ++i;

        const Key& a = m_keys[i];
        const Key& b = m_keys[i + 1];
        if (interp == CurveInterp::Step) return a.value;

        const f32 span = b.time - a.time;
        f32 t = span > 0.0f ? (time - a.time) / span : 0.0f;

        // Easing reshapes the parameter, not the values — so it composes with any interpolation
        // and any type, and an ease-in on a quaternion still comes out a rotation.
        t = evaluate_ease(a.ease, t);

        if (interp == CurveInterp::CatmullRom) {
            // Clamp at the ends rather than wrapping: a curve is not obliged to loop, and
            // reaching past the last key for a tangent would invent motion that is not there.
            const Key& p0 = m_keys[i > 0 ? i - 1 : i];
            const Key& p3 = m_keys[i + 2 < m_keys.size() ? i + 2 : i + 1];
            return curveSpline(p0.value, a.value, b.value, p3.value, t);
        }
        return curveMix(a.value, b.value, t);
    }

private:
    static f32 evaluate_ease(Ease e, f32 t) { return easing::evaluate(e, t); }

    std::vector<Key> m_keys;   // kept sorted by time
};

// ---------------------------------------------------------------------------
// Colour
// ---------------------------------------------------------------------------

// The space a colour is interpolated IN. It is not a detail: blending red to green through
// linear RGB passes through a muddy dark olive, because the straight line between them in RGB
// dips below both in perceived lightness. The same blend in Oklab stays bright the whole way,
// because Oklab is built so that a straight line looks straight to a human.
enum class ColorSpace : u8 {
    LinearRgb,   // physically correct for LIGHT (adding two lights), wrong-looking for PAINT
    Srgb,        // what a naive lerp of two hex codes does. Included so it can be compared.
    Oklab,       // perceptually uniform: use this to animate a colour a person will look at
    Hsv,         // hue takes the short way round the wheel — red to green goes through yellow
};

[[nodiscard]] Color mixColor(Color a, Color b, f32 t, ColorSpace space = ColorSpace::Oklab);

}
