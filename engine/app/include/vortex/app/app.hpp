#pragma once
#include "vortex/app/scene_manager.hpp"
#include "vortex/audio/audio.hpp"
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/ecs/serialize.hpp"
#include "vortex/platform/input_map.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <functional>
#include <memory>

namespace vortex::pf      { class IWindow; class IInputProvider; class IClock; class IFileSystem; }
namespace vortex::rhi     { class IGraphicsDevice; }
namespace vortex::jobs    { class JobSystem; }
namespace vortex::assets  { class AssetManager; }
namespace vortex::audio   { class IAudioEngine; }
namespace vortex::physics { class PhysicsWorld; }
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

    // Handed to every PhysicsWorld App creates. fixedStep is overwritten with
    // fixedTimeStep above, so the solver and the game loop cannot drift apart.
    //
    // The solver works in metres, so gravity is m/s^2 — not pixels. pixelsPerMeter
    // is what bridges the two: at 100, a 100-pixel sprite is a 1-metre body.
    Vec2 gravity        = {0.0f, -9.81f};
    f32  pixelsPerMeter = 100.0f;
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

    // The active scene. A game that never creates a second one never has to know
    // SceneManager exists.
    [[nodiscard]] ecs::Scene&              scene();
    [[nodiscard]] SceneManager&            scenes();
    [[nodiscard]] ecs::Registry&           registry();
    [[nodiscard]] renderer::Camera2D&      camera();
    [[nodiscard]] renderer::ParticleWorld& particles();
    [[nodiscard]] pf::IWindow&             window();

    // The raw device. Reach for actions() first — a game that reads keys directly
    // cannot be rebound and will not see a gamepad.
    [[nodiscard]] pf::IInputProvider&      input();

    // Named actions over keys, mouse and gamepads. Sampled once per frame by the
    // loop, before onFixedUpdate and onUpdate, so every query in a frame agrees.
    [[nodiscard]] pf::InputMap&            actions();
    [[nodiscard]] rhi::IGraphicsDevice&    device();
    [[nodiscard]] assets::AssetManager&    assets();
    [[nodiscard]] pf::IFileSystem&         fileSystem();
    [[nodiscard]] jobs::JobSystem&         jobs();

    // The physics world for the ACTIVE scene, created the first time it is asked
    // for. Each scene gets its own — sharing one would let entity indices from two
    // different registries collide on the same body. Once a scene has a world, the
    // loop steps it inside every fixed update, before onFixedUpdate runs; a scene
    // that never calls this pays nothing.
    [[nodiscard]] physics::PhysicsWorld& physics();
    [[nodiscard]] bool                   hasPhysics() const;   // for the active scene

    // Created on first use. Null only if no audio device could be opened, which is
    // normal in CI and on headless machines — check before you use it.
    [[nodiscard]] audio::IAudioEngine* audio();

    // Load (or reuse) a sound by path, and play it. A no-op with no audio device.
    void playSound(const char* path, bool loop = false);

    // Load (or reuse) a texture and resolve it straight to a GPU handle.
    [[nodiscard]] rhi::TextureHandle loadTexture(const char* path);

    // A 1x1 opaque white pixel, created on first use. Tint it to draw flat-coloured
    // quads — particles, UI fills, debug rectangles — without an asset on disk.
    [[nodiscard]] rhi::TextureHandle whiteTexture();

    // --- Persistence -------------------------------------------------------------
    //
    // Textures are written as the paths they were loaded from, and resolved back
    // through the AssetManager on load, so a saved scene refers to assets rather
    // than to GPU handles. A texture created directly on the device (not through
    // assets()) has no path and will come back unset — load it through
    // loadTexture() if it must survive a save.

    // Serialize the active scene to `path`. False on a write failure.
    bool saveScene(const char* path);

    // Replace the active scene with the one in `path`. False if the file is missing
    // or malformed, in which case the active scene is left EMPTY, not untouched.
    bool loadScene(const char* path);

    // The handle-to-name mapping the two calls above use. Take a copy of it to drive
    // ecs::savePrefab / ecs::instantiate yourself.
    [[nodiscard]] const ecs::SerializeContext& serializeContext() const;

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
