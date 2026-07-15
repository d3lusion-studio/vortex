// Camera controllers, headless: every rig is pure math, so every promise it makes is
// checkable without a window.
//
//   Orbit    — rotate/dolly/pan around a target; the radius is an invariant.
//   Fly      — WASD + mouse-look; strafing stays level, "up" means world up.
//   Pan 2D   — drag keeps the world glued to the cursor; wheel zooms about the cursor.
//   Follow2D — frame-rate-independent smoothing + platformer deadzone.
//   Shake    — trauma-based: decays to exactly zero, offsets are smooth, not white noise.
//   Zoom     — projection zoom means the same thing in perspective and orthographic.
//
// Exits non-zero on the first broken promise.

#include "vortex/core/log.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/camera_controller.hpp"

#include <cmath>
#include <cstdio>

using namespace vortex;
using namespace vortex::renderer;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
bool near(Vec2 a, Vec2 b, f32 eps = 1e-3f) { return near(a.x, b.x, eps) && near(a.y, b.y, eps); }

} // namespace

int main() {
    std::printf("-- orbit: three numbers that always mean something --\n");
    {
        OrbitController orbit;
        orbit.target   = {2.0f, 1.0f, -3.0f};
        orbit.distance = 8.0f;
        orbit.rotate(1.3f, 0.4f);
        orbit.dolly(0.5f);
        check(near(length(orbit.position() - orbit.target), 4.0f),
              "rotate + dolly leave the camera exactly `distance` from the target");

        orbit.rotate(0.0f, 10.0f);   // yank straight up
        check(orbit.pitch < kHalfPi, "pitch clamps short of the pole (no view flip)");

        const Vec3 before = orbit.position() - orbit.target;
        orbit.pan(3.0f, -2.0f);
        const Vec3 after = orbit.position() - orbit.target;
        check(near(length(before - after), 0.0f),
              "pan moves the target and camera together, not the framing");

        Camera cam;
        cam.mode = Camera::Mode::Perspective;
        orbit.apply(cam);
        check(near(length(cam.position - orbit.position()), 0.0f) &&
              near(length(cam.target - orbit.target), 0.0f),
              "apply() writes position and target through");
    }

    std::printf("-- fly: no roll, level strafe, world-up climb --\n");
    {
        FlyController fly;
        fly.position = {0.0f, 2.0f, 0.0f};
        fly.look(200.0f, -150.0f);   // look somewhere non-trivial
        check(near(length(fly.forward()), 1.0f), "forward stays unit length");
        check(near(fly.right().y, 0.0f), "right stays level however far you look up");

        const Vec3 start = fly.position;
        fly.move({0.0f, 0.0f, 1.0f}, 2.0f);   // "W" for two seconds
        const Vec3 moved = fly.position - start;
        const Vec3 expect = fly.forward() * (fly.moveSpeed * 2.0f);
        check(near(length(moved - expect), 0.0f), "W moves along the view direction");

        fly.pitch = -1.0f;                      // aimed well below the horizon
        const f32 y0 = fly.position.y;
        fly.move({0.0f, 1.0f, 0.0f}, 1.0f);     // "space" = up
        check(fly.position.y > y0 + fly.moveSpeed * 0.99f,
              "climbing means WORLD up, even while looking down");
    }

    std::printf("-- pan 2D: the world sticks to the cursor --\n");
    {
        Camera2D cam;
        cam.viewportWidth  = 1280.0f;
        cam.viewportHeight = 720.0f;
        cam.zoom           = 2.0f;
        cam.position       = {40.0f, -10.0f};

        const Vec2 grabScreen{400.0f, 500.0f};
        const Vec2 grabbed = cam.screenToWorld(grabScreen.x, grabScreen.y);
        const Vec2 dragBy{123.0f, -77.0f};
        PanController2D::drag(cam, dragBy);
        const Vec2 nowUnderCursor =
            cam.screenToWorld(grabScreen.x + dragBy.x, grabScreen.y + dragBy.y);
        check(near(nowUnderCursor, grabbed), "dragging keeps the grab point under the cursor");

        const Vec2 wheelAt{900.0f, 200.0f};
        const Vec2 pinned = cam.screenToWorld(wheelAt.x, wheelAt.y);
        PanController2D::zoomAt(cam, wheelAt, 1.5f);
        check(near(cam.zoom, 3.0f), "wheel multiplies zoom");
        check(near(cam.screenToWorld(wheelAt.x, wheelAt.y), pinned),
              "zooming pins the world point under the cursor");
    }

    std::printf("-- follow 2D: same feel at any frame rate --\n");
    {
        const Vec2 target{100.0f, 50.0f};
        FollowController2D follow;

        Camera2D at30, at240;
        for (int i = 0; i < 30; ++i)  follow.update(at30, target, 1.0f / 30.0f);
        for (int i = 0; i < 240; ++i) follow.update(at240, target, 1.0f / 240.0f);
        check(near(at30.position, at240.position, 0.5f),
              "one simulated second lands in the same place at 30 and 240 fps");
        check(length(target - at30.position) < 0.5f,
              "and that place is essentially the target");

        follow.deadzone = {5.0f, 5.0f};
        Camera2D still;
        still.position = {0.0f, 0.0f};
        follow.update(still, {3.0f, -4.0f}, 1.0f);
        check(near(still.position, {0.0f, 0.0f}), "inside the deadzone the camera holds still");
    }

    std::printf("-- screen shake: violent, smooth, and finite --\n");
    {
        ScreenShake shake;
        shake.addTrauma(0.6f);
        shake.update(1.0f / 60.0f);
        check(length(shake.offset()) > 0.0f, "trauma produces an offset");

        const Vec2 a = shake.offset();
        shake.update(1.0f / 240.0f);
        const Vec2 b = shake.offset();
        check(length(b - a) < shake.maxOffset * 0.5f,
              "consecutive frames sample a curve, not a lottery");

        for (int i = 0; i < 120; ++i) shake.update(1.0f / 60.0f);
        check(!shake.active() && near(length(shake.offset()), 0.0f),
              "trauma decays to exactly zero — no everlasting jitter");
    }

    std::printf("-- projection zoom: one verb, three projections --\n");
    {
        Camera cam;
        cam.mode        = Camera::Mode::Perspective;
        cam.fovYRadians = radians(60.0f);
        const f32 tanBefore = std::tan(cam.fovYRadians * 0.5f);
        zoomProjection(cam, 2.0f);
        check(near(std::tan(cam.fovYRadians * 0.5f), tanBefore * 0.5f),
              "perspective: 2x zoom exactly halves the view tangent");

        cam.mode        = Camera::Mode::Orthographic3D;
        cam.orthoHeight = 10.0f;
        zoomProjection(cam, 2.0f);
        check(near(cam.orthoHeight, 5.0f), "orthographic: 2x zoom halves the view box");
    }

    std::printf("-- verdict --\n");
    std::printf(g_failures == 0 ? "  all checks passed\n" : "  %d check(s) FAILED\n",
                g_failures);
    return g_failures;
}
