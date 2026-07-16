#pragma once
#include "vortex/core/math/bounds3d.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/gizmos3d.hpp"

#include <cmath>

// An interactive translate gizmo: three axis handles the user grabs and drags to
// move a target along X, Y or Z. It is the editor tool built from two pieces already
// in the engine — Gizmos3D draws the handles, and a ray/line test decides which one
// the cursor is on. Feed it the mouse ray each frame (Camera::viewportToWorld) and it
// returns the target's new position; ask it to draw itself, highlighting the axis in
// play. The interaction is pure math, so it is testable with no GPU.

namespace vortex::renderer {

// Closest approach between an infinite line L(s)=lp+s*ld and a ray R(t)=rp+t*rd.
struct LineRayClosest {
    f32 s = 0.0f;      // parameter along the line
    f32 t = 0.0f;      // parameter along the ray
    f32 distance = 0.0f;
};

[[nodiscard]] inline LineRayClosest closestLineRay(Vec3 lp, Vec3 ld, Vec3 rp, Vec3 rd) {
    const Vec3 r = lp - rp;
    const f32  A = dot(ld, ld), B = dot(ld, rd), C = dot(rd, rd);
    const f32  D = dot(ld, r),  E = dot(rd, r);
    const f32  denom = A * C - B * B;
    LineRayClosest out;
    if (std::fabs(denom) < 1e-8f) {          // near-parallel: pin the line at s = 0
        out.s = 0.0f;
        out.t = (C > 1e-8f) ? E / C : 0.0f;
    } else {
        out.s = (B * E - C * D) / denom;
        out.t = (A * E - B * D) / denom;
    }
    const Vec3 pl = lp + ld * out.s;
    const Vec3 pr = rp + rd * out.t;
    out.distance = length(pl - pr);
    return out;
}

class TransformGizmo {
public:
    enum class Axis { None, X, Y, Z };

    f32 handleLength = 1.5f;   // world length of each axis handle
    f32 pickRadius   = 0.18f;  // how close the ray must pass to grab a handle

    [[nodiscard]] Axis activeAxis() const { return m_active; }
    [[nodiscard]] bool dragging()  const { return m_dragging; }

    // Which handle the ray is over (None if outside every handle). Public so a caller
    // can highlight or change the cursor without committing to a drag.
    [[nodiscard]] Axis hovered(Vec3 origin, const Ray3D& ray) const {
        Axis best     = Axis::None;
        f32  bestDist = pickRadius;
        for (Axis a : {Axis::X, Axis::Y, Axis::Z}) {
            const Vec3 dir = axisDir(a);
            const LineRayClosest c = closestLineRay(origin, dir, ray.origin, ray.direction);
            if (c.t > 0.0f && c.s >= 0.0f && c.s <= handleLength && c.distance < bestDist) {
                bestDist = c.distance;
                best     = a;
            }
        }
        return best;
    }

    // Drive one frame. Returns the (possibly moved) origin.
    //   pressed  — the button went down this frame
    //   down     — the button is held
    //   released — the button went up this frame
    [[nodiscard]] Vec3 interact(Vec3 origin, const Ray3D& ray, bool pressed, bool down, bool released) {
        if (released) { m_dragging = false; }

        if (m_dragging && down) {
            // Track the ray along the axis anchored where the drag began, so the grab
            // point stays under the cursor and cursor motion off the axis is ignored.
            const Vec3 dir = axisDir(m_active);
            const f32  s   = closestLineRay(m_anchor, dir, ray.origin, ray.direction).s;
            return m_anchor + dir * (s - m_grabS);
        }

        if (pressed) {
            m_active = hovered(origin, ray);
            if (m_active != Axis::None) {
                m_dragging = true;
                m_anchor   = origin;
                m_grabS    = closestLineRay(origin, axisDir(m_active), ray.origin, ray.direction).s;
            }
            return origin;
        }

        if (!m_dragging) m_active = hovered(origin, ray);   // hover highlight only
        return origin;
    }

    // Draw the three handles, brightening the active one.
    void draw(Gizmos3D& giz, Vec3 origin) const {
        drawAxis(giz, origin, Axis::X, {1.0f, 0.25f, 0.25f, 1.0f});
        drawAxis(giz, origin, Axis::Y, {0.25f, 1.0f, 0.3f, 1.0f});
        drawAxis(giz, origin, Axis::Z, {0.35f, 0.55f, 1.0f, 1.0f});
    }

    static Vec3 axisDir(Axis a) {
        switch (a) {
        case Axis::X: return {1.0f, 0.0f, 0.0f};
        case Axis::Y: return {0.0f, 1.0f, 0.0f};
        case Axis::Z: return {0.0f, 0.0f, 1.0f};
        default:      return {0.0f, 0.0f, 0.0f};
        }
    }

private:
    void drawAxis(Gizmos3D& giz, Vec3 origin, Axis a, Vec4 color) const {
        const Vec4 c = (a == m_active) ? Vec4{1.0f, 1.0f, 0.4f, 1.0f} : color;   // active -> yellow
        const Vec3 dir = axisDir(a);
        const Vec3 tip = origin + dir * handleLength;
        giz.line(origin, tip, c);
        // A small box on the tip as the grab head.
        giz.box(tip, {0.08f, 0.08f, 0.08f}, c);
    }

    Axis m_active   = Axis::None;
    bool m_dragging = false;
    Vec3 m_anchor{0.0f, 0.0f, 0.0f};
    f32  m_grabS = 0.0f;
};

}
