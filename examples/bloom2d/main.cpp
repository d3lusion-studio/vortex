// 2D bloom, through App.
//
// Turning on AppConfig::postProcess renders the sprites into a float target and runs
// bloom + ACES tone mapping on the way to the screen. The only thing a game then has to
// do differently is tint the sprites it wants to glow ABOVE 1.0 — a colour of {6,3,1,1}
// is six times brighter than white, and the bright pass is what finds it.
//
// The dim sprites here are tinted below 1 and do not bloom; the bright ones are tinted
// far above it and do. Press SPACE to toggle bloom off and watch the glow disappear
// while the sprites themselves stay exactly where they are.

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_batch.hpp"

#include <cmath>
#include <cstdlib>

using namespace vortex;

int main() {
    app::AppConfig config;
    config.title       = "Vortex 2D Bloom";
    config.postProcess = true;
    config.post.bloomThreshold = 1.0f;   // anything brighter than white blooms
    config.post.bloomIntensity = 0.9f;
    config.clearColor          = Color::fromRgb(0x05060B);

    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);

    rhi::TextureHandle white{};
    f32 time = 0.0f;
    bool bloomOn = true;

    app.onStart([&](app::App& a) {
        white = a.whiteTexture();
        VORTEX_INFO("App", "SPACE toggles bloom. ESC quits.");
    });

    app.onUpdate([&](app::App& a, f32 dt) {
        time += dt;
        if (a.input().isKeyPressed(pf::Key::Escape)) a.quit();
        if (a.input().isKeyPressed(pf::Key::Space)) {
            bloomOn = !bloomOn;
            if (renderer::PostProcess::Settings* p = a.postSettings()) p->bloom = bloomOn;
            VORTEX_INFO("App", "bloom %s", bloomOn ? "ON" : "OFF");
        }
    });

    app.onRender([&](app::App&, renderer::SpriteBatch& batch) {
        // Row 1: ordinary sprites, tinted at or below white. Nothing to bloom.
        for (int i = 0; i < 5; ++i) {
            const f32 v = 0.3f + static_cast<f32>(i) * 0.175f;   // 0.3 .. 1.0
            batch.drawSprite(white, {-380.0f + static_cast<f32>(i) * 120.0f, 140.0f},
                             {84.0f, 84.0f}, {v, v * 0.7f, v * 0.4f, 1.0f});
        }

        // Row 2: the same sprites, but tinted well past 1.0. Same pixels on screen —
        // the extra energy is what spills into the glow.
        for (int i = 0; i < 5; ++i) {
            const f32 v = 1.5f + static_cast<f32>(i) * 1.6f;     // 1.5 .. 7.9
            batch.drawSprite(white, {-380.0f + static_cast<f32>(i) * 120.0f, -60.0f},
                             {84.0f, 84.0f}, {v, v * 0.55f, v * 0.2f, 1.0f});
        }

        // A pulsing core, to show the glow tracking brightness over time.
        const f32 pulse = 3.0f + 3.0f * std::sin(time * 2.0f);
        batch.drawSprite(white, {0.0f, -240.0f}, {60.0f, 60.0f},
                         {0.3f * pulse, 0.8f * pulse, pulse, 1.0f});
    });

    return app.run();
}
