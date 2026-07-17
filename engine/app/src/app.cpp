#include "vortex/app/app.hpp"

#include "vortex/asset/asset_manager.hpp"
#include "vortex/asset/image.hpp"
#include "vortex/audio/audio.hpp"
#include "vortex/core/json.hpp"
#include "vortex/core/log.hpp"

#include <cstdlib>
#include "vortex/core/settings.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/jobs/job_system.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/input_map.hpp"
#include "vortex/physics/physics_world.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/lighting2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/rhi/swapchain.hpp"

#include "vortex/core/diagnostics.hpp"

#include <chrono>
#include <semaphore>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vortex::app {

// The scene target's format when post-processing is on. Float, so a colour can exceed 1
// and still be a colour rather than a clamp — which is the whole premise of bloom. Half
// rather than full floats: RGBA32F cannot be alpha-blended into under WebGPU, and blending
// is exactly what a sprite does.
inline constexpr rhi::Format kHdrFormat = rhi::Format::R16G16B16A16_SFLOAT;

// The overlay's view-projection: framebuffer pixels, origin at the centre, +y up. This is
// Camera2D's projection at zoom 1 with the camera at the origin — stated directly rather
// than by parking a second Camera2D somewhere, because the overlay has no camera to move.
[[nodiscard]] Mat4 uiProjection(f32 width, f32 height) {
    return Mat4::ortho(-width * 0.5f, width * 0.5f, -height * 0.5f, height * 0.5f, -1.0f, 1.0f);
}

// Declaration order is destruction order reversed: the batcher and the asset
// manager both free GPU objects in their destructors, so the device must outlive
// them, and the swapchain must go before the device it was created from.
struct App::Impl {
    AppConfig config;

    // Non-empty for exactly one frame: render() captures the backbuffer into it and
    // clears it again, so a request can never fire twice.
    std::string screenshotPath;

    std::unique_ptr<pf::IWindow>        window;
    std::unique_ptr<pf::IInputProvider> input;
    std::unique_ptr<pf::IClock>         clock;
    std::unique_ptr<pf::IFileSystem>    fs;
    std::unique_ptr<jobs::JobSystem>    jobs;

    std::unique_ptr<rhi::IGraphicsDevice> device;
    std::unique_ptr<rhi::ISwapchain>      swapchain;

    std::unique_ptr<assets::AssetManager>  assets;

    // One frame's worth of work in flight: everything the renderer needs, and nothing that
    // points back into the Scene. The batcher lives HERE rather than being shared because
    // filling it is simulation (CPU only) and draining it is rendering — with one shared
    // batcher the game thread would be pushing frame N+1's sprites into the very vector the
    // main thread is still recording frame N from.
    struct FrameSlot {
        std::unique_ptr<renderer::SpriteBatch> batch;
        std::vector<renderer::RenderItem>      items;
        // The onUi() overlay. A separate batcher, not a layer of the one above: it is
        // bound to a different matrix (pixels, not world) and — when post-processing is
        // on — to a different target format, and a batcher is one of each.
        std::unique_ptr<renderer::SpriteBatch> uiBatch;
    };
    // [1] is only built when threadedSimulation is on: the single-threaded loop reuses [0]
    // every frame and must not pay for a second pipeline and instance buffer.
    FrameSlot slots[2];

    // Only built when AppConfig::postProcess is on. Without them the loop draws straight
    // to the backbuffer and pays for none of this.
    std::unique_ptr<renderer::RenderGraph> graph;
    std::unique_ptr<renderer::PostProcess> post;

    // Only built when AppConfig::lighting2D is on.
    std::unique_ptr<renderer::Lighting2D> lights;

    SceneManager          scenes;
    ecs::SerializeContext serialize;
    pf::InputMap          actions;

    // Keyed by scene, because a body is found by entity index and two scenes hand
    // out the same indices to entirely different entities.
    std::unordered_map<const ecs::Scene*, std::unique_ptr<physics::PhysicsWorld>> physics;

    std::unique_ptr<audio::IAudioEngine>              audio;
    bool                                              audioTried = false;
    std::unordered_map<std::string, audio::SoundHandle> sounds;

    // Lists, not single slots. See the note on App::onUpdate: with one slot, two plugins that
    // both want a frame hook cannot coexist, and the failure is silent.
    std::vector<App::StartFn>  startFns;
    std::vector<App::UpdateFn> updateFns;
    std::vector<App::UpdateFn> fixedUpdateFns;
    std::vector<App::RenderFn> renderFns;
    std::vector<App::RenderFn> uiFns;
    std::vector<App::RawRenderFn> rawRenderFns;
    std::vector<App::StartFn>  shutdownFns;

    std::vector<std::unique_ptr<IPlugin>> plugins;
    std::vector<std::string>              pluginNames;

    Settings    settings;
    std::string settingsPath;

    bool quitRequested = false;
    f32  elapsed       = 0.0f;
    f32  delta         = 0.0f;
    f32  accumulator   = 0.0f;
    u64  frames        = 0;

    f32 hotReloadTimer = 0.0f;

    f64 fpsAccum  = 0.0;
    u32 fpsFrames = 0;
    f32 fpsValue  = 0.0f;

    rhi::TextureHandle white;   // lazily created 1x1 white pixel

    int  lastWidth  = 0;
    int  lastHeight = 0;
    bool minimised  = false;

    // --- Pipelined simulation (threadedSimulation) --------------------------------------
    //
    // A strict two-phase handoff, not a free-running producer/consumer: the main thread
    // releases `simStart`, records and submits the previous frame, then blocks on `simDone`.
    // The overlap is exactly that middle stretch — which is where the fence wait and the
    // command recording live, and is therefore most of the main thread's frame.
    //
    // Two semaphores rather than a mutex + flag because that IS the whole protocol, and a
    // reader looking for a race should be able to see there is nowhere to put one: outside
    // the overlap, only one thread is running at all.
    std::thread             simThread;
    std::binary_semaphore   simStart{0};
    std::binary_semaphore   simDone{0};
    bool                    simStop = false;   // written before the last simStart.release()
    u32                     simSlot = 0;       // the slot the game thread is filling
    std::thread::id         simThreadId;

    // Written by the game thread before simDone.release(), read by the main thread after
    // simDone.acquire() — the release/acquire pair is what makes that safe, and is why
    // these are plain floats rather than atomics.
    f32 simMs    = 0.0f;
    f32 renderMs = 0.0f;
    // How long the swapchain made us wait. Broken out because it is not the engine's time —
    // with vsync on it is the monitor's, and it dwarfs everything else in a frame that is
    // not GPU-bound. Averaging it into render.ms is how you end up "optimising" the display.
    f32 acquireMs = 0.0f;

    // The two halves' wall-clock intervals, and how much of them genuinely coincided. This
    // is the direct evidence that the pipeline pipelines — unlike a frame-time comparison,
    // it cannot be masked by a GPU or compositor stall that hits both loops equally.
    using Clock = std::chrono::steady_clock;
    Clock::time_point simBegin, simEnd, renderBegin, renderEnd;
    f32               overlapMs = 0.0f;

    u32 lastDrawCalls = 0;

    [[nodiscard]] bool onSimThread() const {
        return simThread.joinable() && std::this_thread::get_id() == simThreadId;
    }
};

App::App(AppConfig config) : m_impl(std::make_unique<Impl>()) {
    Impl& s  = *m_impl;
    s.config = config;

    // The environment gets the first word on logging, before anything has a chance to log.
    initLogFromEnv();

    // A headless/CI frame cap, without every game having to parse it for itself.
    if (s.config.maxFrames == 0)
        if (const char* env = std::getenv("VORTEX_MAX_FRAMES"))
            s.config.maxFrames = std::strtoull(env, nullptr, 10);

    if (config.settingsName != nullptr) {
        s.settingsPath = Settings::defaultPath(config.settingsName);
        if (!s.settingsPath.empty()) {
            const bool found = s.settings.load(s.settingsPath.c_str());
            VORTEX_TRACE("Settings", "%s: %s", s.settingsPath.c_str(),
                         found ? "loaded" : "no file yet (first run)");
        }
    }

    s.window = pf::createWindow({.width  = config.width,
                                 .height = config.height,
                                 .title  = config.title});
    s.input  = pf::createInputProvider(*s.window);
    s.clock  = pf::createClock();
    s.fs     = pf::createFileSystem();
    s.jobs   = std::make_unique<jobs::JobSystem>(config.workerCount);
    s.device = rhi::createDevice(*s.window);

    s.window->getFramebufferSize(s.lastWidth, s.lastHeight);
    s.swapchain = s.device->createSwapchain({.width   = static_cast<u32>(s.lastWidth),
                                             .height  = static_cast<u32>(s.lastHeight),
                                             .present = config.presentMode},
                                            *s.window);

    s.assets = std::make_unique<assets::AssetManager>(*s.device, *s.fs);

    // With post-processing on, sprites are drawn into a float target rather than the
    // backbuffer, so the batcher's pipeline must be built for THAT format — a pipeline
    // is bound to the format of the target it renders to.
    const rhi::Format sceneFormat = config.postProcess ? kHdrFormat : s.swapchain->format();
    s.slots[0].batch =
        std::make_unique<renderer::SpriteBatch>(*s.device, sceneFormat, config.maxSprites);
    s.slots[0].uiBatch = std::make_unique<renderer::SpriteBatch>(
        *s.device, s.swapchain->format(), config.maxUiSprites);
    if (config.threadedSimulation) {
        s.slots[1].batch =
            std::make_unique<renderer::SpriteBatch>(*s.device, sceneFormat, config.maxSprites);
        s.slots[1].uiBatch = std::make_unique<renderer::SpriteBatch>(
            *s.device, s.swapchain->format(), config.maxUiSprites);
    }

    if (config.postProcess) {
        s.graph = std::make_unique<renderer::RenderGraph>(*s.device);
        s.post  = std::make_unique<renderer::PostProcess>(*s.device, kHdrFormat,
                                                          s.swapchain->format());
    }

    // The composite multiplies over whatever the world was drawn into — the HDR target
    // when post-processing is on, the backbuffer when it is not. Same reason the ui
    // batcher is built for the swapchain: a pipeline belongs to one format.
    if (config.lighting2D) {
        s.lights = std::make_unique<renderer::Lighting2D>(*s.device, sceneFormat,
                                                          config.maxLights);
        s.lights->setResolutionScale(config.lightBufferScale);
    }

    // Scene files name their textures; the asset manager is what turns a name into a
    // handle and back. Capturing the raw pointer is safe: the manager outlives every
    // save or load, both of which only ever run inside this App.
    assets::AssetManager* am = s.assets.get();
    s.serialize.textureName   = [am](rhi::TextureHandle h) { return am->pathOf(h); };
    s.serialize.textureByName = [am](const std::string& path) {
        return am->gpuTexture(am->loadTexture(path.c_str()));
    };

    s.scenes.active().camera.viewportWidth  = static_cast<f32>(s.lastWidth);
    s.scenes.active().camera.viewportHeight = static_cast<f32>(s.lastHeight);
}

App::~App() {
    // run() joins the game thread on its way out; this is the belt-and-braces path for an
    // App destroyed without ever running, or after a throw.
    if (m_impl->simThread.joinable()) {
        m_impl->simStop = true;
        m_impl->simStart.release();
        m_impl->simThread.join();
    }
    if (m_impl->white.valid()) {
        m_impl->device->waitIdle();
        m_impl->device->destroyTexture(m_impl->white);
    }
}

App& App::onStart(StartFn fn) {
    if (fn) m_impl->startFns.push_back(std::move(fn));
    return *this;
}
App& App::onUpdate(UpdateFn fn) {
    if (fn) m_impl->updateFns.push_back(std::move(fn));
    return *this;
}
App& App::onFixedUpdate(UpdateFn fn) {
    if (fn) m_impl->fixedUpdateFns.push_back(std::move(fn));
    return *this;
}
App& App::onRender(RenderFn fn) {
    if (fn) m_impl->renderFns.push_back(std::move(fn));
    return *this;
}

App& App::onUi(RenderFn fn) {
    if (fn) m_impl->uiFns.push_back(std::move(fn));
    return *this;
}

App& App::onRawRender(RawRenderFn fn) {
    if (fn) m_impl->rawRenderFns.push_back(std::move(fn));
    return *this;
}
App& App::onShutdown(StartFn fn) {
    if (fn) m_impl->shutdownFns.push_back(std::move(fn));
    return *this;
}

App& App::addPlugin(std::unique_ptr<IPlugin> plugin) {
    if (!plugin) return *this;

    const char* name = plugin->name();
    VORTEX_TRACE("Plugin", "building '%s'", name);

    // Build FIRST, then keep it: build() is where the plugin registers its hooks, and the App must
    // outlive them, which it does — the plugin list is destroyed with the App.
    plugin->build(*this);

    m_impl->pluginNames.emplace_back(name);
    m_impl->plugins.push_back(std::move(plugin));
    return *this;
}

App& App::addPlugins(PluginGroup group) {
    for (PluginGroup::Entry& e : group.entries()) {
        if (!e.enabled) {
            VORTEX_TRACE("Plugin", "skipping disabled '%s'", e.plugin->name());
            continue;
        }
        addPlugin(std::move(e.plugin));
    }
    return *this;
}

const std::vector<std::string>& App::plugins() const { return m_impl->pluginNames; }

Settings& App::settings() { return m_impl->settings; }

void App::quit() { m_impl->quitRequested = true; }

ecs::Scene&              App::scene()     { return m_impl->scenes.active(); }
SceneManager&            App::scenes()    { return m_impl->scenes; }
ecs::Registry&           App::registry()  { return m_impl->scenes.active().registry(); }
renderer::Camera2D&      App::camera()    { return m_impl->scenes.active().camera; }
renderer::ParticleWorld& App::particles() { return m_impl->scenes.active().particles; }
renderer::Lighting2D*    App::lights()    { return m_impl->lights.get(); }

rhi::Format App::surfaceFormat() const { return m_impl->swapchain->format(); }
pf::IWindow&             App::window()    { return *m_impl->window; }
pf::IInputProvider&      App::input()     { return *m_impl->input; }
pf::InputMap&            App::actions()   { return m_impl->actions; }
rhi::IGraphicsDevice&    App::device()    { return *m_impl->device; }
assets::AssetManager&    App::assets()    { return *m_impl->assets; }
pf::IFileSystem&         App::fileSystem() { return *m_impl->fs; }
jobs::JobSystem&         App::jobs()      { return *m_impl->jobs; }

physics::PhysicsWorld& App::physics() {
    Impl& s = *m_impl;
    const ecs::Scene* key = &s.scenes.active();

    auto it = s.physics.find(key);
    if (it == s.physics.end()) {
        // fixedStep is not the caller's to choose: the loop already decides how often
        // physics is stepped, and a solver on a different clock would stutter.
        const physics::PhysicsConfig config{.gravity        = s.config.gravity,
                                            .pixelsPerMeter = s.config.pixelsPerMeter,
                                            .fixedStep      = s.config.fixedTimeStep};
        it = s.physics.emplace(key, std::make_unique<physics::PhysicsWorld>(config)).first;
        VORTEX_INFO("Physics", "world created for scene '%.*s'",
                    static_cast<int>(s.scenes.activeName().size()), s.scenes.activeName().data());
    }
    return *it->second;
}

bool App::hasPhysics() const {
    return m_impl->physics.count(&m_impl->scenes.active()) != 0;
}

audio::IAudioEngine* App::audio() {
    Impl& s = *m_impl;
    if (!s.audioTried) {
        s.audioTried = true;   // a failed open is not retried every frame
        s.audio      = audio::createAudioEngine();
        if (!s.audio) VORTEX_WARN("Audio", "no audio device; sound is disabled");
    }
    return s.audio.get();
}

void App::playSound(const char* path, bool loop) {
    audio::IAudioEngine* engine = audio();
    if (engine == nullptr) return;

    auto it = m_impl->sounds.find(path);
    if (it == m_impl->sounds.end())
        it = m_impl->sounds.emplace(path, engine->load(path)).first;

    if (it->second.valid()) engine->play(it->second, loop);
}

rhi::TextureHandle App::loadTexture(const char* path) {
    // Uploads to the GPU, and the device belongs to the main thread. See the contract on
    // AppConfig::threadedSimulation: load in onStart, or go through assets() and let its
    // IO threads decode while the main thread uploads.
    VORTEX_ASSERT(!m_impl->onSimThread(),
                  "loadTexture() from the game thread — the device is the main thread's");
    return m_impl->assets->gpuTexture(m_impl->assets->loadTexture(path));
}

rhi::TextureHandle App::whiteTexture() {
    VORTEX_ASSERT(!m_impl->onSimThread(),
                  "whiteTexture() from the game thread — the device is the main thread's");
    if (!m_impl->white.valid()) {
        const u8 pixel[4] = {255, 255, 255, 255};
        m_impl->white = m_impl->device->createTexture(
            {.width = 1, .height = 1, .debugName = "app_white"}, pixel);
    }
    return m_impl->white;
}

const ecs::SerializeContext& App::serializeContext() const { return m_impl->serialize; }

bool App::saveScene(const char* path) {
    const json::Value doc  = ecs::saveScene(m_impl->scenes.active(), m_impl->serialize);
    const std::string text = json::write(doc);
    if (!m_impl->fs->writeFile(path, text.data(), text.size())) {
        VORTEX_ERROR("Scene", "could not write '%s'", path);
        return false;
    }
    VORTEX_INFO("Scene", "saved '%s' (%zu entities, %zu bytes)", path,
                m_impl->scenes.active().registry().aliveCount(), text.size());
    return true;
}

bool App::loadScene(const char* path) {
    const std::vector<std::byte> bytes = m_impl->fs->readFile(path);
    if (bytes.empty()) {
        VORTEX_ERROR("Scene", "could not read '%s'", path);
        return false;
    }

    std::string       error;
    const json::Value doc = json::parse(
        {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, &error);
    if (!error.empty()) {
        VORTEX_ERROR("Scene", "%s: %s", path, error.c_str());
        return false;
    }

    if (!ecs::loadScene(m_impl->scenes.active(), doc, m_impl->serialize)) return false;

    // The camera's viewport is a property of the window, not of the file.
    int width = 0, height = 0;
    m_impl->window->getFramebufferSize(width, height);
    m_impl->scenes.active().camera.viewportWidth  = static_cast<f32>(width);
    m_impl->scenes.active().camera.viewportHeight = static_cast<f32>(height);

    VORTEX_INFO("Scene", "loaded '%s' (%zu entities)", path,
                m_impl->scenes.active().registry().aliveCount());
    return true;
}

f32   App::time()           const { return m_impl->elapsed; }
f32   App::deltaTime()      const { return m_impl->delta; }
f32   App::fps()            const { return m_impl->fpsValue; }
u64   App::frameCount()     const { return m_impl->frames; }
renderer::PostProcess::Settings* App::postSettings() {
    return m_impl->post ? &m_impl->config.post : nullptr;
}

// Snapshotted after the last frame was recorded, not read live off the batcher: with the
// pipeline on, the batcher a caller would find is the one the GAME thread is refilling.
u32   App::drawCalls()      const { return m_impl->lastDrawCalls; }
usize App::visibleSprites() const { return m_impl->scenes.active().visibleSprites(); }

f32 App::fixedAlpha() const {
    const f32 step = m_impl->config.fixedTimeStep;
    return step > 0.0f ? m_impl->accumulator / step : 0.0f;
}

// Everything the main thread must do before the game thread is allowed to run: advance the
// clock, take the input snapshot, and settle the window size. GLFW is main-thread-only, and
// this is the ONLY place the loop touches it — which is precisely what makes it safe for an
// update hook on the game thread to read input(): by then this has already run and returned.
void App::pollPlatform() {
    Impl& s = *m_impl;

    s.clock->tick();
    s.input->newFrame();
    s.window->pollEvents();

    // After pollEvents, before anything reads input: every query this frame then sees one
    // consistent snapshot.
    s.actions.update(*s.input);

    s.delta = clamp(static_cast<f32>(s.clock->deltaTime()), 0.0f, s.config.maxFrameTime);
    s.elapsed += s.delta;

    int width = 0, height = 0;
    s.window->getFramebufferSize(width, height);
    if (width != s.lastWidth || height != s.lastHeight) {
        s.swapchain->requestResize(static_cast<u32>(width), static_cast<u32>(height));
        s.lastWidth  = width;
        s.lastHeight = height;
    }
    s.minimised = width == 0 || height == 0;
    if (s.minimised) return;

    // The camera's viewport belongs to the window, so it is set here — on the main thread,
    // before the game thread is released to read it.
    s.scenes.active().camera.viewportWidth  = static_cast<f32>(width);
    s.scenes.active().camera.viewportHeight = static_cast<f32>(height);
}

// The simulation half of a frame. Runs on the game thread when the pipeline is on, and
// inline on the main thread when it is not — it is the same code either way, which is the
// point: a bug that only appears threaded is a bug in the handoff, not in here.
//
// Touches: the Scene, physics, the hooks, and `slot`. Touches no GPU object except the
// batcher's CPU-side item list.
void App::simulate(u32 slotIndex) {
    Impl& s = *m_impl;
    const auto t0 = std::chrono::steady_clock::now();
    s.simBegin = t0;

    // Before anything reads the scene this frame. A switch queued mid-update lands here, so
    // no system ever sees the world change under it.
    s.scenes.applyPendingSwitch();

    // Physics steps inside the same fixed loop as gameplay, and BEFORE it, so onFixedUpdate
    // reads transforms the solver has already settled this step rather than last step's.
    const auto physicsIt = s.physics.find(&s.scenes.active());
    physics::PhysicsWorld* world =
        physicsIt != s.physics.end() ? physicsIt->second.get() : nullptr;

    if ((!s.fixedUpdateFns.empty() || world != nullptr) && s.config.fixedTimeStep > 0.0f) {
        VORTEX_PROFILE_ZONE("app.fixedUpdate");
        s.accumulator += s.delta;
        while (s.accumulator >= s.config.fixedTimeStep) {
            if (world != nullptr)
                world->step(s.scenes.active().registry(), s.config.fixedTimeStep);
            for (const auto& fn : s.fixedUpdateFns) fn(*this, s.config.fixedTimeStep);
            s.accumulator -= s.config.fixedTimeStep;
        }
    }

    {
        VORTEX_PROFILE_ZONE("app.update");
        for (const auto& fn : s.updateFns) fn(*this, s.delta);
        if (s.config.parallelExtract) s.scenes.active().update(s.delta, *s.jobs);
        else                          s.scenes.active().update(s.delta);
    }

    // Fill the batcher. begin/submit/draw are CPU-only — they build an item list and a
    // view-projection, and nothing more. end() is the GPU half, and it stays on the main
    // thread; that split is the whole reason this is safely movable off it.
    Impl::FrameSlot& slot = s.slots[slotIndex];
    slot.batch->begin(s.scenes.active().camera.viewProjection());
    {
        VORTEX_PROFILE_ZONE("app.extract");
        if (s.config.parallelExtract) s.scenes.active().extract(slot.items, *s.jobs);
        else                          s.scenes.active().extract(slot.items);
    }
    if (!slot.items.empty()) slot.batch->submit(slot.items.data(), slot.items.size());
    for (const auto& fn : s.renderFns) fn(*this, *slot.batch);

    // The overlay, in framebuffer pixels with the origin at the centre. Built every frame
    // from the CURRENT size, so a resized window moves the HUD with it and no game has to
    // notice the resize.
    slot.uiBatch->begin(uiProjection(static_cast<f32>(s.lastWidth), static_cast<f32>(s.lastHeight)));
    for (const auto& fn : s.uiFns) fn(*this, *slot.uiBatch);

    s.simEnd = std::chrono::steady_clock::now();
    const std::chrono::duration<f32, std::milli> dt = s.simEnd - t0;
    s.simMs = dt.count();
}

namespace {

// readTexture hands back the backbuffer in the swapchain's OWN format, and writePng
// wants RGBA8 — so which channels need swapping depends on what the surface negotiated.
// Hard-coding the BGRA case is wrong on any surface that reports RGBA first, and the
// symptom (a blue sky comes out orange) points nowhere near this function.
void captureBackbuffer(rhi::IGraphicsDevice& device, rhi::TextureHandle backbuffer, u32 width,
                       u32 height, rhi::Format format, const std::string& path) {
    std::vector<u8> pixels(static_cast<usize>(width) * height * 4);
    device.readTexture(backbuffer, pixels.data());

    if (format == rhi::Format::B8G8R8A8_UNORM || format == rhi::Format::B8G8R8A8_SRGB)
        for (usize i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);

    if (assets::writePng(path.c_str(), width, height, pixels.data()))
        VORTEX_INFO("App", "Screenshot written to %s (%ux%u)", path.c_str(), width, height);
    else
        VORTEX_ERROR("App", "Could not write screenshot to %s", path.c_str());
}

}   // namespace

void App::requestScreenshot(std::string path) { m_impl->screenshotPath = std::move(path); }

// The GPU half. Main thread only, always: it owns the device, the swapchain and every
// upload. It reads `slot` — which the game thread has finished with — and the Scene not at
// all.
void App::render(u32 slotIndex) {
    Impl& s = *m_impl;
    if (s.minimised) return;   // nothing to draw into

    const auto t0 = std::chrono::steady_clock::now();
    s.renderBegin = t0;

    s.assets->beginFrame();   // GPU uploads for whatever the IO threads decoded

    // Pick up edits made on disk while the game runs. Here on the main thread, next to the
    // uploads, because a reload IS an upload — doing it from the game thread would touch
    // the device from the one place the thread contract says must not.
    if (s.config.hotReloadAssets) {
        s.hotReloadTimer += s.delta;
        if (s.hotReloadTimer >= s.config.hotReloadInterval) {
            s.hotReloadTimer = 0.0f;
            if (const u32 reloaded = s.assets->pollHotReload(); reloaded > 0)
                VORTEX_INFO("App", "Hot-reloaded %u asset(s)", reloaded);
        }
    }

    // beginFrame() is where the swapchain blocks — on the frame-in-flight fence, and, with
    // vsync, on the display. Timed apart from the rest so a slow monitor cannot be mistaken
    // for a slow renderer.
    const auto tAcq = std::chrono::steady_clock::now();
    rhi::FrameContext frame = s.device->beginFrame(*s.swapchain);
    {
        const std::chrono::duration<f32, std::milli> d =
            std::chrono::steady_clock::now() - tAcq;
        s.acquireMs = d.count();
    }
    if (!frame.valid) {
        s.renderEnd = std::chrono::steady_clock::now();
        return;
    }

    Impl::FrameSlot& slot = s.slots[slotIndex];

    const Color& clear = s.config.clearColor;
    const f32 clearColor[4] = {clear.r, clear.g, clear.b, clear.a};
    const rhi::Viewport viewport{.x      = 0.0f,
                                 .y      = 0.0f,
                                 .width  = static_cast<f32>(frame.width),
                                 .height = static_cast<f32>(frame.height)};

    // The light buffer is a render target of its own, so it has to be filled while no pass
    // is open — before the world's, not inside it. The composite then lands on top of the
    // world, inside its pass.
    if (s.lights)
        s.lights->buildBuffer(*frame.cmd, s.scenes.active().camera.viewProjection(),
                              frame.width, frame.height);

    if (s.post) {
        // Sprites into an HDR target, then bloom and tone mapping resolve it to the screen.
        s.graph->beginFrame();
        const auto backbuffer = s.graph->importBackbuffer(frame.backbuffer,
                                                          frame.width, frame.height);
        const auto sceneHdr   = s.graph->colorTarget("scene_hdr", frame.width, frame.height,
                                                     kHdrFormat);

        s.graph->addPass("sprites",
            [&](renderer::RenderGraph::PassBuilder& b) { b.writeColor(sceneHdr, clearColor); },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport(viewport);
                cmd.setScissor(0, 0, frame.width, frame.height);
                slot.batch->end(cmd);
                // Inside the world's pass, so the lighting grades the world and nothing
                // else — and before tone mapping, so a lit scene still bloom.
                if (s.lights) s.lights->composite(cmd, frame.width, frame.height);
            });

        s.post->addPasses(*s.graph, sceneHdr, backbuffer, frame.width, frame.height,
                          s.config.post);

        // LoadOp::Load, not Clear: post has already written the backbuffer and the overlay
        // is drawing on top of it. This pass is also why the ui batcher is built for the
        // swapchain's format while the world's is built for the HDR target's.
        s.graph->addPass("ui",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.writeColor(backbuffer, clearColor, rhi::LoadOp::Load);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport(viewport);
                cmd.setScissor(0, 0, frame.width, frame.height);
                slot.uiBatch->end(cmd);
                for (const auto& fn : s.rawRenderFns) fn(*this, cmd);
            });

        s.graph->execute(*frame.cmd);
    } else {
        rhi::RenderPassDesc pass;
        pass.color.target        = frame.backbuffer;
        pass.color.loadOp        = rhi::LoadOp::Clear;
        pass.color.clearColor[0] = clear.r;
        pass.color.clearColor[1] = clear.g;
        pass.color.clearColor[2] = clear.b;
        pass.color.clearColor[3] = clear.a;
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport(viewport);
        frame.cmd->setScissor(0, 0, frame.width, frame.height);
        slot.batch->end(*frame.cmd);
        // Between the world and the HUD: the farm goes dark at midnight, the clock does not.
        if (s.lights) s.lights->composite(*frame.cmd, frame.width, frame.height);
        slot.uiBatch->end(*frame.cmd);   // same target and format: no second pass needed
        for (const auto& fn : s.rawRenderFns) fn(*this, *frame.cmd);
        frame.cmd->endRenderPass();
    }

    s.device->endFrame();
    s.lastDrawCalls = slot.batch->drawCallCount();

    if (!s.screenshotPath.empty()) {
        captureBackbuffer(*s.device, frame.backbuffer, frame.width, frame.height,
                          s.swapchain->format(), s.screenshotPath);
        s.screenshotPath.clear();
    }

    s.renderEnd = std::chrono::steady_clock::now();
    const std::chrono::duration<f32, std::milli> dt = s.renderEnd - t0;
    s.renderMs = dt.count();
}

// Both diagnostics are recorded HERE, on the main thread, and never from the game thread:
// the diagnostics registry is a plain vector with no lock, and two threads creating a
// series at once is exactly the kind of race that shows up once a month in the wild.
void App::endFrame() {
    Impl& s = *m_impl;

    ++s.frames;
    s.fpsAccum += s.clock->deltaTime();
    ++s.fpsFrames;
    if (s.fpsAccum >= 0.5) {
        s.fpsValue  = static_cast<f32>(static_cast<f64>(s.fpsFrames) / s.fpsAccum);
        s.fpsAccum  = 0.0;
        s.fpsFrames = 0;
    }

    diag::frame(s.delta);
    diag::add("sim.ms", s.simMs);
    diag::add("render.ms", s.renderMs);
    diag::add("present.wait.ms", s.acquireMs);

    // How much of this frame's simulation and rendering actually ran at the same time: the
    // length of the intersection of the two intervals. Zero in the single-threaded loop by
    // construction (they cannot overlap), and it is the honest measure of what the pipeline
    // bought — a frame-time comparison would also be measuring the display.
    if (s.config.threadedSimulation) {
        const auto begin = s.simBegin > s.renderBegin ? s.simBegin : s.renderBegin;
        const auto end   = s.simEnd   < s.renderEnd   ? s.simEnd   : s.renderEnd;
        const std::chrono::duration<f32, std::milli> d = end - begin;
        s.overlapMs = end > begin ? d.count() : 0.0f;
    }
    diag::add("overlap.ms", s.overlapMs);
}

int App::run() {
    Impl& s = *m_impl;

    for (const auto& fn : s.startFns) fn(*this);

    const auto stopWanted = [&] {
        return s.window->shouldClose() || s.quitRequested ||
               (s.config.maxFrames != 0 && s.frames >= s.config.maxFrames);
    };

    if (!s.config.threadedSimulation) {
        while (!stopWanted()) {
            pollPlatform();
            if (s.minimised) continue;
            simulate(0);
            render(0);
            endFrame();
        }
    } else {
        VORTEX_INFO("App", "pipelined: simulation on its own thread, one frame ahead");

        s.simThread = std::thread([this, &s] {
            for (;;) {
                s.simStart.acquire();
                if (s.simStop) return;
                simulate(s.simSlot);
                s.simDone.release();
            }
        });
        s.simThreadId = s.simThread.get_id();

        // Prime the pipeline: there must be a frame in flight to render before the first
        // overlap can begin. Frame 0 is simulated inline, so the game thread's first real
        // job is frame 1 — running concurrently with frame 0's recording.
        pollPlatform();
        simulate(0);

        u32 current = 0;
        while (!stopWanted()) {
            const u32 next = current ^ 1u;

            pollPlatform();          // input and window size for the frame the game thread
            if (s.minimised) continue;   // is about to simulate

            s.simSlot = next;
            s.simStart.release();    // >>> game thread simulates frame N+1 ...

            render(current);         // ... while this thread records and submits frame N

            s.simDone.acquire();     // <<< rejoin
            endFrame();
            current = next;
        }

        s.simStop = true;
        s.simStart.release();
        s.simThread.join();
    }

    // Reverse order: a plugin that set something up during start should tear it down after the
    // things registered after it, which may depend on it.
    for (auto it = s.shutdownFns.rbegin(); it != s.shutdownFns.rend(); ++it) (*it)(*this);

    // Settings are written AFTER shutdown hooks, so a hook that records "the player quit on
    // level 4" is in the file that gets saved rather than in the one that was already written.
    if (!s.settingsPath.empty() && !s.settings.save(s.settingsPath.c_str()))
        VORTEX_WARN("Settings", "could not write %s", s.settingsPath.c_str());
    s.device->waitIdle();
    return 0;
}

}
