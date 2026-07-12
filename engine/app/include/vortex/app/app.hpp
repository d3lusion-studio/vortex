#pragma once
#include "vortex/core/math/color.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <functional>
#include <memory>

namespace vortex::pf      { class IWindow; class IInputProvider; class IClock; class IFileSystem; }
namespace vortex::rhi     { class IGraphicsDevice; }
namespace vortex::jobs    { class JobSystem; }
namespace vortex::assets  { class AssetManager; }
namespace vortex::renderer { class SpriteBatch; class Camera2D; class ParticleWorld; }

namespace vortex::app {

struct AppConfig {
    int         width  = 1280;
    int         height = 720;
    const char* title  = "Vortex";

    Color clearColor = Color::fromRgb(0x0A0D17);

    // Fixed-rate step for gameplay and physics. onFixedUpdate runs zero or more
    // times per frame so simulation stays deterministic when the frame rate moves.
    f32 fixedTimeStep = 1.0f / 60.0f;

    // Longest frame the loop will simulate. A frame slower than this (a debugger
    // breakpoint, a stalled load) is truncated rather than replayed as a burst of
    // fixed steps that would take even longer — the classic spiral of death.
    f32 maxFrameTime = 0.25f;

    u32 maxSprites = 100000;

    // Spread world-matrix composition across the job system. Pays off once the
    // visible set reaches the thousands; below that the sync costs more.
    bool parallelExtract = false;
    u32  workerCount     = 0;   // 0 = hardware concurrency

    // Stop after this many frames. 0 runs until the window closes; anything else
    // is for headless/CI runs.
    u64 maxFrames = 0;
};

// Owns the window, device, swapchain, batcher, job system, assets and Scene, and
// drives them in one loop. Everything it holds is reachable through the accessors,
// so dropping to the raw RHI stays possible without abandoning the loop.
//
//   App app;
//   app.onStart([](App& a) { a.scene().spawn(); })
//      .onUpdate([](App& a, f32 dt) { ... });
//   return app.run();
class App {
public:
    using StartFn  = std::function<void(App&)>;
    using UpdateFn = std::function<void(App&, f32 dt)>;
    using RenderFn = std::function<void(App&, renderer::SpriteBatch&)>;

    explicit App(AppConfig config = {});
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Callbacks are chainable and each replaces the previously registered one.
    App& onStart(StartFn fn);
    App& onUpdate(UpdateFn fn);        // variable rate, once per frame
    App& onFixedUpdate(UpdateFn fn);   // fixed rate, 0..N times per frame
    App& onRender(RenderFn fn);        // extra sprites, batched with the scene's
    App& onShutdown(StartFn fn);

    // Runs until the window closes, quit() is called, or maxFrames is reached.
    int  run();
    void quit();

    [[nodiscard]] ecs::Scene&              scene();
    [[nodiscard]] ecs::Registry&           registry();
    [[nodiscard]] renderer::Camera2D&      camera();
    [[nodiscard]] renderer::ParticleWorld& particles();
    [[nodiscard]] pf::IWindow&             window();
    [[nodiscard]] pf::IInputProvider&      input();
    [[nodiscard]] rhi::IGraphicsDevice&    device();
    [[nodiscard]] assets::AssetManager&    assets();
    [[nodiscard]] jobs::JobSystem&         jobs();

    // Load (or reuse) a texture and resolve it straight to a GPU handle.
    [[nodiscard]] rhi::TextureHandle loadTexture(const char* path);

    // A 1x1 opaque white pixel, created on first use. Tint it to draw flat-coloured
    // quads — particles, UI fills, debug rectangles — without an asset on disk.
    [[nodiscard]] rhi::TextureHandle whiteTexture();

    [[nodiscard]] f32   time()      const;   // seconds since run() began
    [[nodiscard]] f32   deltaTime() const;   // clamped to maxFrameTime
    [[nodiscard]] f32   fps()       const;

    // Fraction of a fixed step left unsimulated, in [0, 1). Use it to interpolate
    // rendering between the last two fixed states and avoid visible stutter.
    [[nodiscard]] f32 fixedAlpha() const;

    [[nodiscard]] u64   frameCount()     const;
    [[nodiscard]] u32   drawCalls()      const;
    [[nodiscard]] usize visibleSprites() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
