// Roller — a complete little 3D game, and a test of whether the engine is ready for one.
//
// Roll a ball around a walled arena and collect every gem before the clock runs out. It is
// deliberately small: no physics engine, no meshes off disk, no textures — just the
// engine's primitive meshes, a sun with shadows, a follow camera, and a 2D HUD on top. If
// those hold together into something playable, the 3D path is real.
#pragma once

#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"

#include <vector>

namespace roller {

using namespace vortex;

// --- Arena --------------------------------------------------------------------

constexpr f32 kArenaHalf   = 14.0f;   // half the floor's side, world units
constexpr f32 kWallHeight  = 1.5f;
constexpr f32 kWallThick   = 1.0f;

// --- Player -------------------------------------------------------------------

constexpr f32 kBallRadius  = 0.8f;
constexpr f32 kAccel       = 42.0f;   // units/s^2 the input pushes
constexpr f32 kMaxSpeed    = 14.0f;
constexpr f32 kFriction    = 3.0f;    // velocity bleed per second when not pushing
constexpr f32 kRestitution = 0.45f;   // how much of the speed a wall gives back

// --- Gems ---------------------------------------------------------------------

constexpr f32 kGemRadius   = 0.55f;
constexpr f32 kGemFloatY   = 1.1f;    // how high a gem hovers
constexpr f32 kPickupRange = kBallRadius + kGemRadius + 0.2f;

struct Gem {
    Vec3 position;
    bool collected = false;
};

// --- The game -----------------------------------------------------------------

enum class Phase : u8 { Playing, Won, Lost };

constexpr f32 kRoundSeconds = 45.0f;

struct GameState {
    Vec3 ballPos{0.0f, kBallRadius, 0.0f};
    Vec3 ballVel{0.0f, 0.0f, 0.0f};

    // The ball's visible orientation, integrated from how far it has rolled. Cosmetic —
    // nothing depends on it — but a ball that slides without spinning looks broken.
    Vec3 rollAxis{1.0f, 0.0f, 0.0f};
    f32  rollAngle = 0.0f;

    std::vector<Gem> gems;
    i32              collected = 0;

    f32   timeLeft = kRoundSeconds;
    Phase phase    = Phase::Playing;

    // Follow camera state, smoothed toward the ball each frame.
    Vec3 camPos{0.0f, 9.0f, 16.0f};

    f32  bannerTimer = 0.0f;   // counts up once the round ends, for the HUD animation

    [[nodiscard]] i32 remaining() const { return static_cast<i32>(gems.size()) - collected; }
};

// Scatter the gems in a fixed pattern, so a round is the same every run — a scripted
// self-check needs a course it can predict.
void resetGame(GameState& state);

}
