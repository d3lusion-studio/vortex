// Particles: a steady smoke plume, a gravity-fed fountain, and a burst fired at
// the mouse. Every emitter lives in the scene's ParticleWorld, so the app loop
// updates and draws them without a line of per-frame code here.

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/particles.hpp"

#include <cstdlib>

using namespace vortex;

int main() {
    app::AppConfig config;
    config.title           = "Vortex Particles";
    config.parallelExtract = true;
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);

    renderer::EmitterHandle burst;

    app.onStart([&burst](app::App& a) {
        const rhi::TextureHandle dot = a.whiteTexture();

        // Smoke: slow, wide, fading upward.
        renderer::ParticleEmitterDesc smoke;
        smoke.texture          = dot;
        smoke.rate             = 900.0f;
        smoke.lifetime         = {2.0f, 3.5f};
        smoke.speed            = {30.0f, 70.0f};
        smoke.direction        = {radians(60.0f), radians(120.0f)};
        smoke.size             = {10.0f, 22.0f};
        smoke.angularVelocity  = {-1.5f, 1.5f};
        smoke.spawnHalfExtents = {40.0f, 4.0f};
        smoke.gravity          = {0.0f, 12.0f};
        smoke.drag             = 0.6f;
        smoke.startColor       = Color::fromRgb(0xE8734A);
        smoke.endColor         = Color{0.25f, 0.25f, 0.30f, 0.0f};
        smoke.startScale       = 0.6f;
        smoke.endScale         = 2.4f;
        smoke.scaleCurve       = easing::Ease::OutQuad;
        smoke.capacity         = 4096;
        a.particles().get(a.particles().add(smoke))->position = {-320.0f, -180.0f};

        // Fountain: fast, narrow, falling back down under gravity.
        renderer::ParticleEmitterDesc fountain;
        fountain.texture     = dot;
        fountain.rate        = 1200.0f;
        fountain.lifetime    = {1.6f, 2.2f};
        fountain.speed       = {380.0f, 460.0f};
        fountain.direction   = {radians(78.0f), radians(102.0f)};
        fountain.size        = {4.0f, 8.0f};
        fountain.gravity     = {0.0f, -600.0f};
        fountain.startColor  = Color::fromRgb(0x5AC8FA);
        fountain.endColor    = Color{0.10f, 0.35f, 0.85f, 0.0f};
        fountain.colorCurve  = easing::Ease::InQuad;
        fountain.capacity    = 4096;
        a.particles().get(a.particles().add(fountain))->position = {320.0f, -220.0f};

        // Burst: no steady rate; fired on click.
        renderer::ParticleEmitterDesc spark;
        spark.texture         = dot;
        spark.rate            = 0.0f;   // burst-only
        spark.lifetime        = {0.4f, 0.9f};
        spark.speed           = {120.0f, 520.0f};
        spark.size            = {3.0f, 7.0f};
        spark.gravity         = {0.0f, -420.0f};
        spark.drag            = 1.2f;
        spark.startColor      = Color::fromRgb(0xFFD166);
        spark.endColor        = Color{1.0f, 0.30f, 0.10f, 0.0f};
        spark.capacity        = 8192;
        burst = a.particles().add(spark);

        VORTEX_INFO("Particles", "Click to burst sparks. ESC quits.");
    });

    app.onUpdate([&burst](app::App& a, f32) {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();

        if (in.isMousePressed(pf::MouseButton::Left)) {
            f32 mx = 0.0f, my = 0.0f;
            in.mousePosition(mx, my);
            a.particles().get(burst)->emitAt(a.camera().screenToWorld(mx, my), 400);
        }

        if (a.frameCount() % 120 == 0 && a.frameCount() > 0)
            VORTEX_INFO("Particles", "%.0f FPS | %zu particles | %u draw calls",
                        static_cast<f64>(a.fps()), a.particles().aliveParticles(), a.drawCalls());
    });

    return app.run();
}
