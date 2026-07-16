// Interactive transform (translate) gizmo, headless: which axis a mouse ray grabs,
// and how dragging moves the target along exactly that axis. The interaction is pure
// math — Camera::viewportToWorld would supply the ray in a real app — so it verifies
// with no window or GPU, a CI regression test like the others.
//
//   1. Hover     — a ray near each handle selects that axis; away selects none
//   2. Drag      — dragging moves the target along the grabbed axis
//   3. Axis lock — cursor motion off the axis does not leak into other axes
//   4. Release   — ends the drag

#include "vortex/core/log.hpp"
#include "vortex/core/math/bounds3d.hpp"
#include "vortex/renderer/transform_gizmo.hpp"

#include <cmath>
#include <cstdio>

using namespace vortex;
using Axis = renderer::TransformGizmo::Axis;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool nearV(Vec3 a, Vec3 b, f32 eps = 1e-3f) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

// A ray shooting straight down -Z from (x, y).
Ray3D fromZ(f32 x, f32 y) { return {{x, y, 5.0f}, {0.0f, 0.0f, -1.0f}}; }

void testHover() {
    std::printf("Hover\n");
    renderer::TransformGizmo giz;
    const Vec3 origin{0.0f, 0.0f, 0.0f};

    check(giz.hovered(origin, fromZ(0.7f, 0.0f)) == Axis::X, "ray along the X handle selects X");
    check(giz.hovered(origin, fromZ(0.0f, 0.7f)) == Axis::Y, "ray along the Y handle selects Y");
    // Z handle: approach it sideways (a ray straight down Z would be degenerate).
    check(giz.hovered(origin, Ray3D{{5.0f, 0.0f, 0.7f}, {-1.0f, 0.0f, 0.0f}}) == Axis::Z,
          "ray across the Z handle selects Z");
    check(giz.hovered(origin, fromZ(3.0f, 3.0f)) == Axis::None, "ray far from every handle selects none");
    check(giz.hovered(origin, fromZ(0.0f, 2.0f)) == Axis::None, "ray past the end of a handle selects none");
}

void testDrag() {
    std::printf("Drag along an axis\n");
    renderer::TransformGizmo giz;
    Vec3 origin{0.0f, 0.0f, 0.0f};

    // Press on the X handle at x = 0.5.
    origin = giz.interact(origin, fromZ(0.5f, 0.0f), /*pressed*/true, /*down*/true, /*released*/false);
    check(giz.dragging() && giz.activeAxis() == Axis::X, "pressing an X handle begins an X drag");

    // Drag the cursor to x = 1.2: the target should slide +0.7 along X.
    origin = giz.interact(origin, fromZ(1.2f, 0.0f), false, true, false);
    check(nearV(origin, {0.7f, 0.0f, 0.0f}), "target follows the cursor along X");

    // Now move the cursor sideways in Y as well: X still tracks, Y/Z stay put.
    origin = giz.interact(origin, fromZ(1.7f, 0.9f), false, true, false);
    check(near(origin.x, 1.2f) && near(origin.y, 0.0f) && near(origin.z, 0.0f),
          "cursor motion off the axis does not leak into Y or Z");

    origin = giz.interact(origin, fromZ(1.7f, 0.9f), false, false, /*released*/true);
    check(!giz.dragging(), "releasing ends the drag");
}

void testDragOtherAxis() {
    std::printf("Drag along Y\n");
    renderer::TransformGizmo giz;
    Vec3 origin{0.0f, 0.0f, 0.0f};

    origin = giz.interact(origin, fromZ(0.0f, 0.4f), true, true, false);
    check(giz.activeAxis() == Axis::Y, "pressing a Y handle begins a Y drag");
    origin = giz.interact(origin, fromZ(0.0f, 1.1f), false, true, false);
    check(nearV(origin, {0.0f, 0.7f, 0.0f}), "target follows the cursor along Y only");
}

void testNoGrabOffAxis() {
    std::printf("Press off any handle\n");
    renderer::TransformGizmo giz;
    Vec3 origin{0.0f, 0.0f, 0.0f};
    origin = giz.interact(origin, fromZ(3.0f, 3.0f), true, true, false);
    check(!giz.dragging(), "pressing away from every handle starts no drag");
    origin = giz.interact(origin, fromZ(3.5f, 3.5f), false, true, false);
    check(nearV(origin, {0.0f, 0.0f, 0.0f}), "with no drag active the target does not move");
}

}

int main() {
    testHover();
    testDrag();
    testDragOtherAxis();
    testNoGrabOffAxis();

    if (g_failures == 0) {
        std::printf("\nAll transform-gizmo checks passed.\n");
        return 0;
    }
    std::printf("\n%d transform-gizmo check(s) FAILED.\n", g_failures);
    return 1;
}
