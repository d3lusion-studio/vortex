#pragma once
#include "vortex/app/plugin.hpp"
#include "vortex/core/settings.hpp"
#include "vortex/app/scene_manager.hpp"
#include "vortex/audio/audio.hpp"
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/ecs/serialize.hpp"
#include "vortex/platform/input_map.hpp"
#include "vortex/renderer/post_process.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <functional>
#include <memory>

namespace vortex::pf      { class IWindow; class IInputProvider; class IClock; class IFileSystem; }
namespace vortex::rhi     { class IGraphicsDevice; class ICommandList; }
namespace vortex::jobs    { class JobSystem; }
namespace vortex::assets  { class AssetManager; }
namespace vortex::audio   { class IAudioEngine; }
namespace vortex::physics { class PhysicsWorld; }
namespace vortex::renderer { class SpriteBatch; class Camera2D; class ParticleWorld; class Lighting2D; }

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

    // 2D lighting: an ambient colour the world is multiplied by, with lights added back
    // into it. Off by default — a scene at full daylight would pay for a buffer and a
    // full-screen multiply to change nothing.
    //
    // See renderer::Lighting2D. Fill lights() each frame; the loop renders them between the
    // world and the onUi() overlay, so the HUD never goes dark with the world.
    bool lighting2D    = false;
    u32  maxLights     = 1024;
    // Fraction of the framebuffer the light buffer is rendered at. Light is low frequency,
    // so a quarter of the pixels is four times less fill and reads the same.
    f32  lightBufferScale = 0.5f;

    // Re-load assets whose file changed on disk, while the game runs. Edit a sprite in
    // Aseprite, save, and it is in the running game — which is the difference between
    // tuning art in seconds and in build cycles.
    //
    // Off by default: the poll stats every loaded asset's file, so it is a developer tool
    // and not something a shipped build should be paying for. AssetManager has done the
    // work since it was written; nothing was calling it from the loop.
    bool hotReloadAssets = false;
    // Seconds between polls. Every frame would stat every asset 60 times a second to catch
    // an edit a human made; four times a second is imperceptible and ~15x less syscall.
    f32  hotReloadInterval = 0.25f;

    // The onUi() overlay's own budget. Separate from maxSprites, and far smaller,
    // because a HUD is tens of quads while a world is tens of thousands — and the two
    // are different draw lists with different pipelines, so they cannot share one.
    u32 maxUiSprites = 8192;

    // Fifo (the default) is vsync: present blocks until the display is ready, which caps the
    // frame rate at the refresh rate and is what a shipped game wants. Immediate does not
    // block — use it to benchmark, because with vsync on, the loop's wall clock measures the
    // MONITOR and tells you nothing about the engine. Mailbox is vsync without the block
    // (triple buffering), where the driver offers it.
    rhi::PresentMode presentMode = rhi::PresentMode::Fifo;

    // Render the scene into a floating-point target and run bloom + ACES tone mapping
    // on the way to the screen, instead of drawing straight to the backbuffer.
    //
    // This is what makes a sprite glow: with it on, colours are no longer clamped at 1,
    // so a sprite tinted {4, 3, 1, 1} is four times brighter than white and the bright
    // pass picks it up. With it off (the default) that tint just reads as white — there
    // is nowhere for the extra brightness to go. The cost is one extra full-screen target
    // and a handful of full-screen passes, which a flat 2D game has no use for.
    bool                            postProcess = false;
    renderer::PostProcess::Settings post{};

    // Spread world-matrix composition across the job system. Pays off once the
    // visible set reaches the thousands; below that the sync costs more.
    bool parallelExtract = false;
    u32  workerCount     = 0;   // 0 = hardware concurrency

    // Run the simulation on its own thread, one frame ahead of the renderer: the game
    // thread computes frame N+1 while the main thread records and submits frame N. Where
    // parallelExtract splits ONE step across cores, this overlaps two different steps —
    // the two stack, and a game that is CPU-bound on both halves wants both.
    //
    // The frame the player sees is one simulation step older than in the single-threaded
    // loop. That is the price of the pipeline and it is why this is opt-in.
    //
    // *** The thread contract, which the loop relies on and does not enforce ***
    //
    //   Game thread owns: the Scene (registry, camera, particles), physics, and the hooks
    //     onFixedUpdate / onUpdate / onRender. onRender only fills a CPU-side sprite list —
    //     SpriteBatch::end() is the only part that touches the GPU, and the loop keeps it.
    //   Main thread owns: the window and input (GLFW is main-thread-only), the RHI device,
    //     and every GPU upload.
    //
    // So: DO NOT call device-touching App APIs from an update or render hook when this is
    // on — loadTexture(), whiteTexture(), device(). They assert in debug builds. Load in
    // onStart, or through assets() (which is built for exactly this: its IO threads decode,
    // and the main thread uploads).
    //
    // Reading input() from a hook IS safe: the loop polls the window before it releases the
    // game thread, and touches GLFW nowhere else, so the two never overlap.
    bool threadedSimulation = false;

    // Stop after this many frames. 0 runs until the window closes.
    //
    // Left at 0, the VORTEX_MAX_FRAMES environment variable is honoured instead, so any App-based
    // game is runnable in CI without a line of code for it. An explicit non-zero value here wins:
    // a program that means to run for exactly N frames should not have that overridden by a shell.
    u64 maxFrames = 0;

    // Settings are loaded from the user's config directory under this name at construction, and
    // written back at shutdown. Null disables the file entirely — a demo does not need one.
    //
    // The load happens in the App CONSTRUCTOR, which means it is too late to use a saved
    // resolution to size the window. That is deliberate: read the Settings yourself first, fill
    // this AppConfig from it, and hand it over. A config that reads itself from a file it also
    // owns is a circle, and the version that resolves it silently always resolves it wrong.
    const char* settingsName = nullptr;

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
    // Runs on the main thread during command recording — see onRawRender.
    using RawRenderFn = std::function<void(App&, rhi::ICommandList&)>;

    explicit App(AppConfig config = {});
    ~App();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // Callbacks are chainable and APPEND: registering a second one does not delete the first,
    // and they run in registration order.
    //
    // This used to replace. It had to change for plugins to be possible at all: a debug overlay
    // and a gameplay system both want to run every frame, and under the old rule whichever
    // registered last silently deleted the other. A game that only registers each hook once —
    // which is every one in this repo — sees no difference.
    App& onStart(StartFn fn);
    App& onUpdate(UpdateFn fn);        // variable rate, once per frame
    App& onFixedUpdate(UpdateFn fn);   // fixed rate, 0..N times per frame
    App& onRender(RenderFn fn);        // extra sprites, batched with the scene's

    // The screen-space overlay: a HUD, a menu, anything that belongs to the viewport
    // rather than to the world.
    //
    // The batch handed here is bound to the framebuffer in PIXELS, origin at the centre,
    // +y up — so (0, 0) is the middle of the screen and {-halfW + 8, halfH - 8} is eight
    // pixels in from the top-left corner, whatever the camera is doing. Without this,
    // every HUD element on a zoomed or scrolling camera has to be converted back out of
    // world space by hand, which is scaffolding no game should be writing.
    //
    // Drawn after the world and, with postProcess on, after tone mapping — so the HUD is
    // never bloomed or graded. `layer` still sorts within the overlay.
    App& onUi(RenderFn fn);

    // Record straight into the frame's command list, inside the pass that drew everything
    // else, after the world and the onUi() overlay.
    //
    // This is the escape hatch for anything that draws with its OWN pipeline rather than
    // through a SpriteBatch — an ImGui layer, a custom full-screen effect. Everything the
    // loop already did is on the target; the pass is open; do not begin another one.
    //
    // Main thread only, always: unlike the other hooks this one runs during recording, not
    // during simulation.
    App& onRawRender(RawRenderFn fn);

    App& onShutdown(StartFn fn);

    // --- Plugins ---------------------------------------------------------------
    //
    // A plugin's build() runs immediately, not at run(): the App is already constructed, so the
    // device, assets and scene are there to be used. Deferring it would buy nothing and mean a
    // plugin could not report a failure until the game was already running.
    App& addPlugin(std::unique_ptr<IPlugin> plugin);

    template <typename T, typename... Args>
    App& addPlugin(Args&&... args) {
        return addPlugin(std::make_unique<T>(std::forward<Args>(args)...));
    }

    // Build every enabled plugin in the group, in order.
    App& addPlugins(PluginGroup group);

    // The names of the plugins that were built, in build order.
    [[nodiscard]] const std::vector<std::string>& plugins() const;

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

    // The 2D light list for this frame. Null unless AppConfig::lighting2D was set — the
    // buffer and its pipelines are built at construction, like post-processing.
    //
    // Submit into it from a fixed/variable update, not onRender: it is CPU state, and the
    // loop reads it on the main thread when it records the frame.
    [[nodiscard]] renderer::Lighting2D* lights();
    [[nodiscard]] pf::IWindow&             window();

    // The raw device. Reach for actions() first — a game that reads keys directly
    // cannot be rebound and will not see a gamepad.
    [[nodiscard]] pf::IInputProvider&      input();

    // Named actions over keys, mouse and gamepads. Sampled once per frame by the
    // loop, before onFixedUpdate and onUpdate, so every query in a frame agrees.
    [[nodiscard]] pf::InputMap&            actions();
    [[nodiscard]] rhi::IGraphicsDevice&    device();

    // The format of the surface onRawRender() records onto. Anything building its own
    // pipeline needs it — a pipeline belongs to exactly one target format — and without it
    // a caller has to guess, which works until the surface is negotiated as BGRA.
    [[nodiscard]] rhi::Format               surfaceFormat() const;
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

    // Named values that persist across runs — volume, keybinds, "have we shown the tutorial".
    // Empty and unbacked unless AppConfig::settingsName was set; written back at shutdown.
    [[nodiscard]] Settings& settings();

    // Live post-processing knobs — threshold, intensity, exposure, and whether bloom and
    // FXAA run at all. Read every frame, so a game can fade the bloom up as it likes.
    // Whether post-processing exists at all is fixed at construction (the pipelines are
    // built for the target's format), so this is null unless AppConfig::postProcess was on.
    [[nodiscard]] renderer::PostProcess::Settings* postSettings();

    // Write the next presented frame to `path` as a PNG. The capture happens after the
    // frame is submitted, so it is exactly what the player saw — post-processing, UI
    // and all — rather than a re-render that might not match.
    //
    // Costs a full GPU stall (readTexture waits for idle), so this is for screenshots
    // and render tests, never a per-frame path. Requesting again before the pending
    // capture fires replaces it: at most one frame is ever written per request.
    //
    // Main thread only, like the other device-touching calls: with threadedSimulation on,
    // an update hook runs on the game thread while render() is reading this request, and
    // the two would race. Call it from onStart, or from a hook only when the loop is
    // single-threaded.
    void requestScreenshot(std::string path);

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
    // The frame, cut into the three pieces the pipeline needs to hand between threads.
    // See AppConfig::threadedSimulation for which thread owns what.
    void pollPlatform();        // main thread: clock, input, window size
    void simulate(u32 slot);    // game thread when pipelined, main thread when not
    void render(u32 slot);      // main thread, always
    void endFrame();            // main thread: frame counter, fps, diagnostics

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
