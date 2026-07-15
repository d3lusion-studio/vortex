#include "vortex/renderer/camera_controller.hpp"

#include <cmath>

namespace vortex::renderer {

// ---------------------------------------------------------------------------
// OrbitController
// ---------------------------------------------------------------------------

Vec3 OrbitController::position() const {
    // Spherical about the target: yaw 0, pitch 0 puts the camera on +Z looking down -Z,
    // matching the fly camera's convention so switching rigs does not snap the view.
    const f32 cp = std::cos(pitch), sp = std::sin(pitch);
    const f32 cy = std::cos(yaw),   sy = std::sin(yaw);
    return target + Vec3{cp * sy, sp, cp * cy} * distance;
}

void OrbitController::pan(f32 deltaRight, f32 deltaUp) {
    // The view plane's axes, from the same angles as position() — panning must slide
    // along what the user SEES as right and up, or diagonal drags curve.
    const f32 cy = std::cos(yaw), sy = std::sin(yaw);
    const Vec3 right{cy, 0.0f, -sy};
    const Vec3 fwd  = normalize(target - position());
    const Vec3 up   = cross(right, fwd);
    target = target + right * deltaRight + up * deltaUp;
}

void OrbitController::apply(Camera& cam) const {
    cam.position = position();
    cam.target   = target;
    cam.up       = {0.0f, 1.0f, 0.0f};
}

// ---------------------------------------------------------------------------
// FlyController
// ---------------------------------------------------------------------------

Vec3 FlyController::forward() const {
    const f32 cp = std::cos(pitch), sp = std::sin(pitch);
    return {-std::sin(yaw) * cp, sp, -std::cos(yaw) * cp};
}

Vec3 FlyController::right() const {
    // Perpendicular to forward in the horizontal plane; independent of pitch, so strafing
    // stays level while looking up.
    return {std::cos(yaw), 0.0f, -std::sin(yaw)};
}

void FlyController::move(Vec3 local, f32 dt, f32 boost) {
    // Forward/right from the view, up from the WORLD: holding "up" while pitched down must
    // gain altitude, not burrow. That asymmetry is what makes a fly camera feel right.
    const Vec3 dir = right() * local.x + Vec3{0.0f, 1.0f, 0.0f} * local.y +
                     forward() * local.z;
    position = position + dir * (moveSpeed * boost * dt);
}

void FlyController::apply(Camera& cam) const {
    cam.position = position;
    cam.target   = position + forward();
    cam.up       = {0.0f, 1.0f, 0.0f};
}

// ---------------------------------------------------------------------------
// PanController2D
// ---------------------------------------------------------------------------

void PanController2D::drag(Camera2D& cam, Vec2 deltaPixels) {
    // Screen Y grows downward, world Y upward; dragging right moves the camera LEFT —
    // the world follows the hand, the camera goes the other way.
    cam.position.x -= deltaPixels.x / cam.zoom;
    cam.position.y += deltaPixels.y / cam.zoom;
}

void PanController2D::zoomAt(Camera2D& cam, Vec2 screenPoint, f32 factor,
                             f32 minZoom, f32 maxZoom) {
    const f32 newZoom = clamp(cam.zoom * factor, minZoom, maxZoom);
    if (newZoom == cam.zoom) return;

    // Pin the world point under the cursor: solve screenToWorld(p, zoom') == anchor for
    // the new position.
    const Vec2 anchor = cam.screenToWorld(screenPoint.x, screenPoint.y);
    cam.zoom = newZoom;
    cam.position.x = anchor.x - (screenPoint.x - cam.viewportWidth  * 0.5f) / newZoom;
    cam.position.y = anchor.y + (screenPoint.y - cam.viewportHeight * 0.5f) / newZoom;
}

// ---------------------------------------------------------------------------
// FollowController2D
// ---------------------------------------------------------------------------

void FollowController2D::update(Camera2D& cam, Vec2 target, f32 dt) const {
    // Only the part of the error outside the deadzone attracts the camera. Chasing the
    // WINDOW EDGE (not the target) is what makes the camera hold still while the player
    // shuffles in place.
    Vec2 error = target - cam.position;
    error.x = error.x > deadzone.x ? error.x - deadzone.x
            : error.x < -deadzone.x ? error.x + deadzone.x : 0.0f;
    error.y = error.y > deadzone.y ? error.y - deadzone.y
            : error.y < -deadzone.y ? error.y + deadzone.y : 0.0f;

    // exp2(-dt/halfLife): the same fraction of the remaining distance closes per unit
    // TIME regardless of frame rate. `lerp(pos, target, k*dt)` — the obvious version —
    // tracks tighter at higher FPS, and a game that feels different at 144 Hz is a bug.
    if (halfLife <= 0.0f) {
        cam.position += error;
        return;
    }
    const f32 remain = std::exp2(-dt / halfLife);
    cam.position += error * (1.0f - remain);
}

// ---------------------------------------------------------------------------
// ScreenShake
// ---------------------------------------------------------------------------

void ScreenShake::update(f32 dt) {
    m_time  += dt;
    m_trauma = clamp(m_trauma - decay * dt, 0.0f, 1.0f);
}

f32 ScreenShake::noise(f32 t, u32 seed) const {
    // Smooth value noise: random values at integer lattice points, cosine-eased between
    // them. Continuous in t — successive frames sample a curve, not a lottery.
    const auto lattice = [seed](i32 i) {
        u32 h = static_cast<u32>(i) * 0x9E3779B9u + seed * 0x85EBCA6Bu;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        return static_cast<f32>(h & 0xFFFFFFu) / static_cast<f32>(0xFFFFFFu) * 2.0f - 1.0f;
    };
    const f32 fl = std::floor(t);
    const i32 i  = static_cast<i32>(fl);
    const f32 f  = t - fl;
    const f32 u  = (1.0f - std::cos(f * kPi)) * 0.5f;
    return lattice(i) + (lattice(i + 1) - lattice(i)) * u;
}

Vec2 ScreenShake::offset() const {
    const f32 amp = sqr(m_trauma) * maxOffset;
    const f32 t   = m_time * frequency;
    return {noise(t, 1u) * amp, noise(t, 2u) * amp};
}

f32 ScreenShake::roll() const {
    return noise(m_time * frequency, 3u) * sqr(m_trauma) * maxRoll;
}

// ---------------------------------------------------------------------------
// zoomProjection
// ---------------------------------------------------------------------------

void zoomProjection(Camera& cam, f32 factor, f32 minFovYRadians, f32 maxFovYRadians) {
    if (factor <= 0.0f) return;

    switch (cam.mode) {
        case Camera::Mode::Perspective:
            // Zoom on the TANGENT, not the angle: halving tan(fov/2) doubles on-screen
            // size exactly, halving the angle only nearly does — and the error grows with
            // the FOV.
            cam.fovYRadians = clamp(
                2.0f * std::atan(std::tan(cam.fovYRadians * 0.5f) / factor),
                minFovYRadians, maxFovYRadians);
            break;
        case Camera::Mode::Orthographic3D:
            cam.orthoHeight = cam.orthoHeight / factor;
            break;
        case Camera::Mode::Orthographic:
            cam.zoom = cam.zoom * factor;
            break;
    }
}

}
