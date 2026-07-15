#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/camera2d.hpp"

namespace vortex::renderer {

// Camera controllers: the hand that moves the camera, kept out of the camera.
//
// Camera and Camera2D stay what they are — a projection and a view, no opinions about
// input. Each controller here owns one STYLE of movement as plain state + math, takes
// deltas (not key codes — binding input to intent is the game's decision), and writes the
// camera on apply()/update(). That split is what lets the same Camera be driven by an
// orbit rig in a tool, a fly rig in a debug build, and a cutscene track in the game,
// without any of the three knowing about the others.

// --- Orbit: the inspection camera -----------------------------------------------------------
//
// Yaw/pitch/distance around a target — the tool camera, the character viewer, the RTS
// camera with a tilt. Stored as angles, not as a position: the pole clamp, the zoom range,
// and "where is the camera" all fall out of three numbers that mean something.
class OrbitController {
public:
    Vec3 target{0.0f, 0.0f, 0.0f};
    f32  yaw      = 0.0f;               // radians about +Y; 0 looks down -Z
    f32  pitch    = 0.3f;               // radians; positive looks down on the target
    f32  distance = 10.0f;

    f32 minDistance = 0.5f;
    f32 maxDistance = 500.0f;
    // Just short of the poles: AT the pole the view direction and `up` align and lookAt
    // degenerates — the screen flips. Nobody wants the last half-degree anyway.
    f32 maxPitch = kHalfPi - 0.01f;

    void rotate(f32 deltaYaw, f32 deltaPitch) {
        yaw   += deltaYaw;
        pitch  = clamp(pitch + deltaPitch, -maxPitch, maxPitch);
    }

    // Multiplicative, because zoom is perceived in ratios: one wheel notch should feel the
    // same at 2 m as at 200 m, and `distance - 1` does not.
    void dolly(f32 factor) { distance = clamp(distance * factor, minDistance, maxDistance); }

    // Slide the target in the view plane, in world units. Scale pixel deltas by
    // distance-per-pixel before calling, so a drag matches the ground moving under the
    // cursor at any zoom.
    void pan(f32 deltaRight, f32 deltaUp);

    [[nodiscard]] Vec3 position() const;

    void apply(Camera&) const;
};

// --- Fly: the free camera --------------------------------------------------------------------
//
// Position + yaw/pitch, moved along its own axes: WASD + mouse look, the level-debug
// camera. Angles rather than a quaternion on purpose — a fly camera must not roll, and
// storing the two angles that can change is how roll stays impossible instead of merely
// corrected.
class FlyController {
public:
    Vec3 position{0.0f, 0.0f, 5.0f};
    f32  yaw   = 0.0f;                  // radians about +Y; 0 looks down -Z
    f32  pitch = 0.0f;

    f32 moveSpeed       = 5.0f;         // world units per second, before `boost`
    f32 lookSensitivity = 0.0025f;      // radians per pixel
    f32 maxPitch        = kHalfPi - 0.01f;

    // Mouse deltas in pixels. Y positive = mouse moved down = look down.
    void look(f32 deltaXPixels, f32 deltaYPixels) {
        yaw   -= deltaXPixels * lookSensitivity;
        pitch  = clamp(pitch - deltaYPixels * lookSensitivity, -maxPitch, maxPitch);
    }

    // `local` is the intent in the camera's own frame: x right, y up (world up — flying
    // "up" means up, not wherever the nose points), z forward. Pass the WASD state as
    // ±1 components; it is scaled by moveSpeed, `boost` and dt here.
    void move(Vec3 local, f32 dt, f32 boost = 1.0f);

    [[nodiscard]] Vec3 forward() const;
    [[nodiscard]] Vec3 right() const;

    void apply(Camera&) const;
};

// --- Pan: the 2D drag-and-wheel camera -------------------------------------------------------
//
// Map viewers, level editors, strategy games: drag moves the world with the cursor, the
// wheel zooms about the cursor. Stateless — Camera2D already holds position and zoom;
// these are the two adjustments everyone rederives and gets subtly wrong.
struct PanController2D {
    // Drag by a screen-space delta (pixels, Y down). The world under the cursor stays
    // under the cursor — that is the definition of a drag, and it means dividing by zoom.
    static void drag(Camera2D&, Vec2 deltaPixels);

    // Zoom by `factor` keeping the world point under `screenPoint` fixed. Zooming about
    // the screen centre instead is what makes a map viewer feel broken: the thing being
    // pointed at slides away.
    static void zoomAt(Camera2D&, Vec2 screenPoint, f32 factor,
                       f32 minZoom = 0.05f, f32 maxZoom = 50.0f);
};

// --- Follow: the top-down/platformer tracking camera -----------------------------------------
//
// Exponential approach toward a target: frame-rate independent (the half-life is a time,
// not a per-frame fraction) and asymptotic, so arrival has no visible "stop". The deadzone
// is the classic platformer window — inside it the camera holds still, because a camera
// that mirrors every idle-animation wobble makes the player seasick.
class FollowController2D {
public:
    f32  halfLife = 0.12f;              // seconds to close half the remaining distance
    Vec2 deadzone{0.0f, 0.0f};          // half-extents, world units; zero = always track

    void update(Camera2D&, Vec2 target, f32 dt) const;
};

// --- Screen shake ----------------------------------------------------------------------------
//
// Trauma-based (the Squirrel Eiserloh model): impacts add trauma, trauma decays linearly,
// and the shake amplitude is trauma SQUARED — so big hits feel violent, the tail fades
// gently, and three small hits stack into something bigger without clipping. The offset is
// smooth noise, not per-frame random: at high frame rates white noise turns into a buzz.
class ScreenShake {
public:
    f32 decay      = 1.0f;              // trauma lost per second
    f32 frequency  = 18.0f;             // shake oscillations per second
    f32 maxOffset  = 20.0f;             // world units (2D) at full trauma
    f32 maxRoll    = 0.06f;             // radians at full trauma

    // `amount` 0..1; capped at 1. A hit adds, it does not set — stacking is the feature.
    void addTrauma(f32 amount) { m_trauma = clamp(m_trauma + amount, 0.0f, 1.0f); }

    void update(f32 dt);

    [[nodiscard]] Vec2 offset() const;
    [[nodiscard]] f32  roll() const;
    [[nodiscard]] f32  trauma() const { return m_trauma; }
    [[nodiscard]] bool active() const { return m_trauma > 0.0f; }

private:
    [[nodiscard]] f32 noise(f32 t, u32 seed) const;

    f32 m_trauma = 0.0f;
    f32 m_time   = 0.0f;
};

// --- Projection zoom, 3D ---------------------------------------------------------------------
//
// The 3D counterpart of Camera2D::zoom: narrow the FOV (perspective) or the view box
// (Orthographic3D) by `factor` > 1 to zoom in. Kept as a helper because which field means
// "zoom" depends on the projection, and callers should not have to switch on the mode.
void zoomProjection(Camera&, f32 factor,
                    f32 minFovYRadians = 0.05f, f32 maxFovYRadians = 2.8f);

}
