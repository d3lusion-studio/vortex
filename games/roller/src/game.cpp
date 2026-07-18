#include "game.hpp"

#include "vortex/core/math/math.hpp"

#include <cmath>

namespace roller {

namespace {

// The gem course: a ring plus a cross, so a route through them exists and the layout looks
// arranged rather than sprinkled. Fixed positions, so every round is the same one — a
// scripted self-check needs a course it can predict.
void placeGems(GameState& state) {
    state.gems.clear();
    const f32 r = kArenaHalf * 0.62f;
    for (i32 i = 0; i < 8; ++i) {
        const f32 a = static_cast<f32>(i) / 8.0f * 6.2831853f;
        state.gems.push_back({{std::cos(a) * r, kGemFloatY, std::sin(a) * r}, false});
    }
    for (const f32 d : {-6.0f, 6.0f}) {
        state.gems.push_back({{d, kGemFloatY, 0.0f}, false});
        state.gems.push_back({{0.0f, kGemFloatY, d}, false});
    }
}

}   // namespace

void resetGame(GameState& state) {
    state = GameState{};
    placeGems(state);
}

}
