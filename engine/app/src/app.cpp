#include "vortex/app/app.hpp"

#include "vortex/asset/asset_manager.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/jobs/job_system.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <vector>

namespace vortex::app {

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

    ecs::Scene scene;

    App::StartFn  startFn;
    App::UpdateFn updateFn;
    App::UpdateFn fixedUpdateFn;
    App::RenderFn renderFn;
    App::StartFn  shutdownFn;

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
    s.batch  = std::make_unique<renderer::SpriteBatch>(*s.device, s.swapchain->format(),
                                                       config.maxSprites);

    s.scene.camera.viewportWidth  = static_cast<f32>(s.lastWidth);
    s.scene.camera.viewportHeight = static_cast<f32>(s.lastHeight);
}

App::~App() {
    if (m_impl->white.valid()) {
        m_impl->device->waitIdle();
        m_impl->device->destroyTexture(m_impl->white);
    }
}

App& App::onStart(StartFn fn)        { m_impl->startFn       = std::move(fn); return *this; }
App& App::onUpdate(UpdateFn fn)      { m_impl->updateFn      = std::move(fn); return *this; }
App& App::onFixedUpdate(UpdateFn fn) { m_impl->fixedUpdateFn = std::move(fn); return *this; }
App& App::onRender(RenderFn fn)      { m_impl->renderFn      = std::move(fn); return *this; }
App& App::onShutdown(StartFn fn)     { m_impl->shutdownFn    = std::move(fn); return *this; }

void App::quit() { m_impl->quitRequested = true; }

ecs::Scene&              App::scene()     { return m_impl->scene; }
ecs::Registry&           App::registry()  { return m_impl->scene.registry(); }
renderer::Camera2D&      App::camera()    { return m_impl->scene.camera; }
renderer::ParticleWorld& App::particles() { return m_impl->scene.particles; }
pf::IWindow&             App::window()    { return *m_impl->window; }
pf::IInputProvider&      App::input()     { return *m_impl->input; }
rhi::IGraphicsDevice&    App::device()    { return *m_impl->device; }
assets::AssetManager&    App::assets()    { return *m_impl->assets; }
jobs::JobSystem&         App::jobs()      { return *m_impl->jobs; }

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

f32   App::time()           const { return m_impl->elapsed; }
f32   App::deltaTime()      const { return m_impl->delta; }
f32   App::fps()            const { return m_impl->fpsValue; }
u64   App::frameCount()     const { return m_impl->frames; }
u32   App::drawCalls()      const { return m_impl->batch->drawCallCount(); }
usize App::visibleSprites() const { return m_impl->scene.visibleSprites(); }

f32 App::fixedAlpha() const {
    const f32 step = m_impl->config.fixedTimeStep;
    return step > 0.0f ? m_impl->accumulator / step : 0.0f;
}

int App::run() {
    Impl& s = *m_impl;

    if (s.startFn) s.startFn(*this);

    while (!s.window->shouldClose() && !s.quitRequested) {
        if (s.config.maxFrames != 0 && s.frames >= s.config.maxFrames) break;

        s.clock->tick();
        s.input->newFrame();
        s.window->pollEvents();

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

        s.scene.camera.viewportWidth  = static_cast<f32>(width);
        s.scene.camera.viewportHeight = static_cast<f32>(height);

        if (s.fixedUpdateFn && s.config.fixedTimeStep > 0.0f) {
            VORTEX_PROFILE_ZONE("app.fixedUpdate");
            s.accumulator += s.delta;
            while (s.accumulator >= s.config.fixedTimeStep) {
                s.fixedUpdateFn(*this, s.config.fixedTimeStep);
                s.accumulator -= s.config.fixedTimeStep;
            }
        }

        {
            VORTEX_PROFILE_ZONE("app.update");
            if (s.updateFn) s.updateFn(*this, s.delta);
            if (s.config.parallelExtract) s.scene.update(s.delta, *s.jobs);
            else                          s.scene.update(s.delta);
        }

        s.assets->beginFrame();

        rhi::FrameContext frame = s.device->beginFrame(*s.swapchain);
        if (!frame.valid) continue;

        const Color& clear = s.config.clearColor;
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
        frame.cmd->setViewport({.x      = 0.0f,
                                .y      = 0.0f,
                                .width  = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        s.batch->begin(s.scene.camera.viewProjection());
        {
            VORTEX_PROFILE_ZONE("app.extract");
            if (s.config.parallelExtract) s.scene.extract(s.items, *s.jobs);
            else                          s.scene.extract(s.items);
        }
        if (!s.items.empty()) s.batch->submit(s.items.data(), s.items.size());
        if (s.renderFn) s.renderFn(*this, *s.batch);
        s.batch->end(*frame.cmd);

        frame.cmd->endRenderPass();
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

    if (s.shutdownFn) s.shutdownFn(*this);
    s.device->waitIdle();
    return 0;
}

}
