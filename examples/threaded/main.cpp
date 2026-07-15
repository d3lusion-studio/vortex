// The pipelined game loop: simulation on its own thread, one frame ahead of the renderer.
//
//   single-threaded:  [sim N][render N][sim N+1][render N+1] ...   frame = sim + render
//   pipelined:        [sim N+1        ]                            frame = max(sim, render)
//                     [render N       ]
//
// Set AppConfig::threadedSimulation and the loop does the rest. The contract it asks of the
// game in return is one line long: don't touch the GPU from an update hook (see the comment
// on the flag). This example holds to it — every texture is loaded in onStart.
//
// Two things are worth proving, and this prints both:
//
//   1. THE SIMULATION IS UNCHANGED. Threading a game loop is only worth anything if the game
//      still plays the same. The world here is driven by a tick COUNTER rather than by
//      wall-clock dt, so its state after N ticks is a fixed number — and that number is
//      printed. Run both modes; if the checksums differ, the pipeline corrupted the sim.
//
//   2. THE OVERLAP IS REAL. sim.ms and render.ms are measured by the loop itself. In the
//      single-threaded run, frame.ms ~= sim.ms + render.ms. Pipelined, it drops toward
//      max(sim.ms, render.ms) — the two halves are running at once.
//
//   VORTEX_THREADED=1 ./threaded      (default; 0 for the single-threaded loop)
//   VORTEX_ENTITIES=20000             how much simulation there is to hide
//   VORTEX_MAX_FRAMES=400             for a fixed-length, comparable run

#include "vortex/app/app.hpp"
#include "vortex/core/diagnostics.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/ecs/components.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

u64 envU64(const char* name, u64 fallback) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::strtoull(v, nullptr, 10) : fallback;
}

// The tick the world is checksummed at. Both modes reach it; the totals they run to need
// not match (the pipeline simulates one frame further than it renders), so comparing a
// FIXED tick is the only comparison that means anything.
constexpr u64 kCheckTick = 200;

} // namespace

int main() {
    const bool  threaded = envU64("VORTEX_THREADED", 1) != 0;
    const usize count    = static_cast<usize>(envU64("VORTEX_ENTITIES", 20000));

    app::AppConfig config;
    config.title              = threaded ? "Vortex — pipelined" : "Vortex — single-threaded";
    config.threadedSimulation = threaded;
    config.parallelExtract    = envU64("VORTEX_PARALLEL_EXTRACT", 1) != 0;
    config.workerCount        = static_cast<u32>(envU64("VORTEX_WORKERS", 0));
    config.maxSprites         = 100000;
    // Vsync off. With it on, the loop's wall clock measures the MONITOR: present blocks
    // until the display is ready, both loops block identically, and the comparison this
    // example exists to make disappears into the refresh rate.
    config.presentMode        = rhi::PresentMode::Immediate;
    // The pipeline can only hide as much as the render half actually costs, and a flat 2D
    // sprite batch costs almost nothing. Bloom + tone mapping is what a real frame looks
    // like, and it is what makes the overlap worth having.
    config.postProcess        = envU64("VORTEX_POST", 1) != 0;

    app::App app(config);

    std::vector<ecs::Entity> entities;
    u64 tick      = 0;
    f64 checksum  = 0.0;
    bool captured = false;

    app.onStart([&](app::App& a) {
        // Every GPU touch happens HERE, on the main thread, before the game thread exists.
        // That is the contract, and it costs a game nothing to honour.
        const rhi::TextureHandle white = a.whiteTexture();

        entities.reserve(count);
        for (usize i = 0; i < count; ++i) {
            const ecs::Entity e = a.scene().spawn();
            a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
                .texture = white,
                .color   = {0.35f + 0.65f * static_cast<f32>(i % 7) / 6.0f,
                            0.45f, 0.85f, 1.0f},
                .size    = {6.0f, 6.0f},
            });
            entities.push_back(e);
        }
        VORTEX_INFO("App", "%zu entities, %s loop", count,
                    threaded ? "pipelined" : "single-threaded");
    });

    app.onUpdate([&](app::App& a, f32) {
        // Driven by the TICK, not by dt: the state after N ticks is then a fixed number, so
        // the two loops can be compared at all. A real game would use dt here — and would
        // then have no reproducible checksum to compare, which is the only reason this does
        // not.
        ++tick;
        const f32 t = static_cast<f32>(tick) * 0.01f;

        ecs::Registry& reg = a.registry();
        for (usize i = 0; i < entities.size(); ++i) {
            const f32 phase = static_cast<f32>(i) * 0.013f;
            const f32 r     = 60.0f + static_cast<f32>(i % 400) * 0.9f;
            ecs::Transform2D& tr = reg.get<ecs::Transform2D>(entities[i]);
            tr.position = {std::cos(t + phase) * r, std::sin(t * 1.3f + phase) * r * 0.55f};
            tr.rotation = t + phase;
        }

        if (tick == kCheckTick && !captured) {
            captured = true;
            for (const ecs::Entity e : entities) {
                const ecs::Transform2D& tr = reg.get<ecs::Transform2D>(e);
                checksum += static_cast<f64>(tr.position.x) +
                            static_cast<f64>(tr.position.y) * 3.0 +
                            static_cast<f64>(tr.rotation) * 7.0;
            }
        }
    });

    const int rc = app.run();

    // sim.ms and render.ms are recorded by the loop itself, on the main thread, whichever
    // mode it ran in.
    const auto ms = [](const char* name) {
        const diag::Diagnostic* d = diag::find(name);
        return d != nullptr ? d->average() : 0.0;
    };
    const f64 sim = ms("sim.ms"), render = ms("render.ms"), frame = ms("frame.ms");
    const f64 serial = sim + render;
    const f64 ideal  = sim > render ? sim : render;

    std::printf("\n== %s ==\n", threaded ? "PIPELINED" : "SINGLE-THREADED");
    std::printf("  entities        %zu\n", count);
    std::printf("  frames          %llu (ticks simulated: %llu)\n",
                static_cast<unsigned long long>(app.frameCount()),
                static_cast<unsigned long long>(tick));
    std::printf("  sim             %6.2f ms\n", sim);
    std::printf("  render          %6.2f ms  (of which %.2f waiting on the swapchain)\n",
                render, ms("present.wait.ms"));
    std::printf("  frame           %6.2f ms   (serial would be %.2f, ideal overlap %.2f)\n",
                frame, serial, ideal);
    // The load-bearing number. It is zero for the single-threaded loop by construction, and
    // it cannot be faked by a fast GPU or hidden by a slow one.
    std::printf("  OVERLAP         %6.2f ms of sim and render ran at the same time\n",
                ms("overlap.ms"));
    std::printf("  checksum@tick%llu  %.6f\n",
                static_cast<unsigned long long>(kCheckTick), checksum);

    if (!captured)
        std::printf("  (ran fewer than %llu ticks — no checksum; raise VORTEX_MAX_FRAMES)\n",
                    static_cast<unsigned long long>(kCheckTick));

    return rc;
}
