#include "vortex/app/app.hpp"

#include "vortex/asset/asset_manager.hpp"
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
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace vortex::app {

// The scene target's format when post-processing is on. Float, so a colour can exceed 1
// and still be a colour rather than a clamp — which is the whole premise of bloom. Half
// rather than full floats: RGBA32F cannot be alpha-blended into under WebGPU, and blending
// is exactly what a sprite does.
inline constexpr rhi::Format kHdrFormat = rhi::Format::R16G16B16A16_SFLOAT;

// Declaration order is destruction order reversed: the batcher and the asset
// manager both free GPU objects in their destructors, so the device must outlive
// them, and the swapchain must go before the device it was created from.
struct App::Impl {
    AppConfig config;

    std::unique_ptr<pf::IWindow>        window;
    std::unique_ptr<pf::IInputProvider> input;
    std::unique_ptr<pf::IClock>         clock;
    std::unique_ptr<pf::IFileSystem>    fs;
    std::unique_ptr<jobs::JobSystem>    jobs;

    std::unique_ptr<rhi::IGraphicsDevice> device;
    std::unique_ptr<rhi::ISwapchain>      swapchain;

    std::unique_ptr<assets::AssetManager>  assets;
    std::unique_ptr<renderer::SpriteBatch> batch;

    // Only built when AppConfig::postProcess is on. Without them the loop draws straight
    // to the backbuffer and pays for none of this.
    std::unique_ptr<renderer::RenderGraph> graph;
    std::unique_ptr<renderer::PostProcess> post;

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
    std::vector<App::StartFn>  shutdownFns;

    std::vector<std::unique_ptr<IPlugin>> plugins;
    std::vector<std::string>              pluginNames;

    Settings    settings;
    std::string settingsPath;

    std::vector<renderer::RenderItem> items;

    bool quitRequested = false;
    f32  elapsed       = 0.0f;
    f32  delta         = 0.0f;
    f32  accumulator   = 0.0f;
    u64  frames        = 0;

    f64 fpsAccum  = 0.0;
    u32 fpsFrames = 0;
    f32 fpsValue  = 0.0f;

    rhi::TextureHandle white;   // lazily created 1x1 white pixel

    int lastWidth  = 0;
    int lastHeight = 0;
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
    s.swapchain = s.device->createSwapchain({.width  = static_cast<u32>(s.lastWidth),
                                             .height = static_cast<u32>(s.lastHeight)},
                                            *s.window);

    s.assets = std::make_unique<assets::AssetManager>(*s.device, *s.fs);

    // With post-processing on, sprites are drawn into a float target rather than the
    // backbuffer, so the batcher's pipeline must be built for THAT format — a pipeline
    // is bound to the format of the target it renders to.
    const rhi::Format sceneFormat = config.postProcess ? kHdrFormat : s.swapchain->format();
    s.batch = std::make_unique<renderer::SpriteBatch>(*s.device, sceneFormat, config.maxSprites);

    if (config.postProcess) {
        s.graph = std::make_unique<renderer::RenderGraph>(*s.device);
        s.post  = std::make_unique<renderer::PostProcess>(*s.device, kHdrFormat,
                                                          s.swapchain->format());
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
    return m_impl->assets->gpuTexture(m_impl->assets->loadTexture(path));
}

rhi::TextureHandle App::whiteTexture() {
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

u32   App::drawCalls()      const { return m_impl->batch->drawCallCount(); }
usize App::visibleSprites() const { return m_impl->scenes.active().visibleSprites(); }

f32 App::fixedAlpha() const {
    const f32 step = m_impl->config.fixedTimeStep;
    return step > 0.0f ? m_impl->accumulator / step : 0.0f;
}

int App::run() {
    Impl& s = *m_impl;

    for (const auto& fn : s.startFns) fn(*this);

    while (!s.window->shouldClose() && !s.quitRequested) {
        if (s.config.maxFrames != 0 && s.frames >= s.config.maxFrames) break;

        // Before anything reads the scene this frame. A switch queued mid-update
        // lands here, so no system ever sees the world change under it.
        s.scenes.applyPendingSwitch();

        s.clock->tick();
        s.input->newFrame();
        s.window->pollEvents();

        // After pollEvents, before anything reads input: every query this frame
        // then sees one consistent snapshot.
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
        if (width == 0 || height == 0) continue;   // minimised; nothing to draw into

        s.scenes.active().camera.viewportWidth  = static_cast<f32>(width);
        s.scenes.active().camera.viewportHeight = static_cast<f32>(height);

        // Physics steps inside the same fixed loop as gameplay, and BEFORE it, so
        // onFixedUpdate reads transforms the solver has already settled this step
        // rather than last step's.
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

        s.assets->beginFrame();

        rhi::FrameContext frame = s.device->beginFrame(*s.swapchain);
        if (!frame.valid) continue;

        const Color& clear = s.config.clearColor;
        const f32 clearColor[4] = {clear.r, clear.g, clear.b, clear.a};
        const rhi::Viewport viewport{.x      = 0.0f,
                                     .y      = 0.0f,
                                     .width  = static_cast<f32>(frame.width),
                                     .height = static_cast<f32>(frame.height)};

        // Fill the batcher first, so the two paths below differ only in where its output
        // lands. Extraction has no idea a post-processing chain exists.
        s.batch->begin(s.scenes.active().camera.viewProjection());
        {
            VORTEX_PROFILE_ZONE("app.extract");
            if (s.config.parallelExtract) s.scenes.active().extract(s.items, *s.jobs);
            else                          s.scenes.active().extract(s.items);
        }
        if (!s.items.empty()) s.batch->submit(s.items.data(), s.items.size());
        for (const auto& fn : s.renderFns) fn(*this, *s.batch);

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
                    s.batch->end(cmd);
                });

            s.post->addPasses(*s.graph, sceneHdr, backbuffer, frame.width, frame.height,
                              s.config.post);
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
            s.batch->end(*frame.cmd);
            frame.cmd->endRenderPass();
        }

        s.device->endFrame();

        ++s.frames;
        s.fpsAccum += s.clock->deltaTime();
        ++s.fpsFrames;
        if (s.fpsAccum >= 0.5) {
            s.fpsValue  = static_cast<f32>(static_cast<f64>(s.fpsFrames) / s.fpsAccum);
            s.fpsAccum  = 0.0;
            s.fpsFrames = 0;
        }
    }

    // Reverse order: a plugin that set something up during start should tear it down after the
    // things registered after it, which may depend on it.
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
