// The player: walking, facing, and the swing that turns an item into a change in
// the world.
#pragma once

#include "assets.hpp"
#include "farm.hpp"
#include "world.hpp"

namespace vortex::app { class App; }
namespace vortex::ecs { class Scene; }

namespace farm {

// Creates the player entity, and installs the observer that turns the swing clips' "hit"
// event into a change in the world.
void spawnPlayer(vortex::ecs::Scene& scene, const Assets& assets, GameState& state,
                 World& world);

// Read input, move, and start swings. Runs at the fixed rate.
void updatePlayer(vortex::app::App& app, const Assets& assets, World& world, GameState& state,
                  f32 dt);

// The tile the player is facing — what a tool acts on.
void facingTile(const GameState& state, i32& tx, i32& ty);

// Start a swing, if the held item has anything to do with the tile ahead. Silent
// when it does not, so mashing the key on bare grass costs nothing.
void beginToolUse(const Assets& assets, const World& world, GameState& state);

// Apply the swing's committed action to the tile it was aimed at. Split out from the
// swing so the effect can land mid-animation, which is when the hoe hits the ground.
void applyToolUse(vortex::ecs::Scene& scene, const Assets& assets, World& world,
                  GameState& state);

}
