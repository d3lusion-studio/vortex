#include "game_api.h"

#include <cmath>
#include <cstring>

namespace {

constexpr int kCount = 24;

struct State {
    bool  initialized;
    float px[kCount];
    float py[kCount];
    float phase[kCount];
    float playerX, playerY;
};

State* state(GameContext* ctx) {
    return static_cast<State*>(ctx->memory);
}

void on_load(GameContext* ctx) {
    State* s = state(ctx);
    if (s->initialized) return;            // preserve positions across reloads
    s->initialized = true;
    s->playerX = 0.0f;
    s->playerY = 0.0f;
    for (int i = 0; i < kCount; ++i) {
        const float t = static_cast<float>(i) / kCount;
        s->px[i] = (t - 0.5f) * 900.0f;
        s->py[i] = 0.0f;
        s->phase[i] = t * 6.2831853f;
    }
}

void on_unload(GameContext*) {}

int update(GameContext* ctx) {
    State* s = state(ctx);

    constexpr float kPlayerSpeed = 420.0f;
    constexpr float kBobAmplitude = 160.0f;
    constexpr float kBobSpeed = 2.0f;
    const float     kBoxColor[3] = {0.35f, 0.75f, 1.0f};

    const float dx = static_cast<float>(ctx->inRight - ctx->inLeft);
    const float dy = static_cast<float>(ctx->inUp - ctx->inDown);
    s->playerX += dx * kPlayerSpeed * ctx->dt;
    s->playerY += dy * kPlayerSpeed * ctx->dt;

    int n = 0;
    for (int i = 0; i < kCount && n < ctx->boxCap; ++i) {
        const float y = s->py[i] +
                        std::sin(ctx->time * kBobSpeed + s->phase[i]) * kBobAmplitude;
        GameBox& b = ctx->boxes[n++];
        b.x = s->px[i]; b.y = y;
        b.w = 30.0f;    b.h = 30.0f;
        b.r = kBoxColor[0]; b.g = kBoxColor[1]; b.b = kBoxColor[2]; b.a = 1.0f;
    }

    // Player box (white), movable with arrow keys.
    if (n < ctx->boxCap) {
        GameBox& p = ctx->boxes[n++];
        p.x = s->playerX; p.y = s->playerY;
        p.w = 48.0f; p.h = 48.0f;
        p.r = 1.0f; p.g = 0.9f; p.b = 0.3f; p.a = 1.0f;
    }

    ctx->boxCount = n;
    return 1;
}

const GameApi g_api = {
    VORTEX_GAME_API_VERSION,
    on_load,
    on_unload,
    update,
};

}

extern "C" const GameApi* vortex_game_api(void) { return &g_api; }
