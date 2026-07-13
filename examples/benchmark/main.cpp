#include "vortex/core/log.hpp"
#include "vortex/core/memory/frame_allocator.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/ecs/systems.hpp"
#include "vortex/jobs/job_system.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"
#include "vortex/text/font.hpp"
#include "vortex/text/text_renderer.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace vortex;

#ifndef VORTEX_FONT_PATH
#define VORTEX_FONT_PATH "/usr/share/fonts/TTF/DejaVuSans.ttf"
#endif

namespace {

std::string findFont(pf::IFileSystem& fs) {
    const char* candidates[] = {
        VORTEX_FONT_PATH, "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    };
    for (const char* c : candidates)
        if (fs.exists(c)) return c;
    return {};
}

constexpr f32 kBound = 2000.0f;

}

int main() {
    const char* spritesEnv = std::getenv("VORTEX_SPRITES");
    const u32 spriteCount = spritesEnv ? static_cast<u32>(std::strtoul(spritesEnv, nullptr, 10))
                                       : 100000u;

    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Benchmark"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), spriteCount + 1024);
    renderer::SpriteBatch hudBatch(*device, swapchain->format(), 4096);

    const std::string fontPath = findFont(*fs);
    if (fontPath.empty()) { VORTEX_ERROR("Demo", "No system font found."); return 1; }
    auto font = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 20.0f);
    if (!font) return 1;

    const u8 whitePx[4] = {255, 255, 255, 255};
    const rhi::TextureHandle white =
        device->createTexture({.width = 1, .height = 1, .debugName = "white"}, whitePx);

    jobs::JobSystem jobs;
    FrameAllocator  frameAlloc(static_cast<usize>(spriteCount) * sizeof(renderer::RenderItem) + 4096);

    ecs::Scene scene;
    scene.camera.zoom           = 0.15f;
    scene.camera.viewportWidth  = static_cast<f32>(fbw);
    scene.camera.viewportHeight = static_cast<f32>(fbh);

    std::mt19937 rng(7);
    std::uniform_real_distribution<f32> posD(-kBound, kBound);
    std::uniform_real_distribution<f32> velD(-220.0f, 220.0f);
    std::uniform_real_distribution<f32> colD(0.25f, 1.0f);

    for (u32 i = 0; i < spriteCount; ++i) {
        const ecs::Entity e = scene.spawn();
        scene.registry().get<ecs::Transform2D>(e).position = {posD(rng), posD(rng)};
        scene.registry().emplace<ecs::Velocity>(e, ecs::Velocity{{velD(rng), velD(rng)}});
        scene.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
            .texture = white, .color = {colD(rng), colD(rng), colD(rng), 1.0f}, .size = {7.0f, 7.0f}});
    }
    VORTEX_INFO("Bench", "Spawned %u sprites on %u workers", spriteCount, jobs.workerCount());

    std::vector<std::pair<ecs::Transform2D*, ecs::Velocity*>> movers;
    std::vector<std::pair<const ecs::WorldTransform2D*, const ecs::SpriteComp*>> pairs;
    std::vector<renderer::RenderItem> serialItems;
    movers.reserve(spriteCount);
    pairs.reserve(spriteCount);

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f64 fps = 0.0;

    while (!window->shouldClose()) {
        clock->tick();
        const f32 dt = static_cast<f32>(clock->deltaTime());
        fps = dt > 0.0 ? 0.9 * fps + 0.1 * (1.0 / dt) : fps;
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        scene.camera.viewportWidth  = static_cast<f32>(w);
        scene.camera.viewportHeight = static_cast<f32>(h);

        profiler::beginFrame();

        {
            VORTEX_PROFILE_ZONE("movement");
            movers.clear();
            scene.registry().view<ecs::Transform2D, ecs::Velocity>(
                [&](ecs::Entity, ecs::Transform2D& t, ecs::Velocity& v) { movers.push_back({&t, &v}); });
            jobs.parallelFor(movers.size(), [&](usize i) {
                ecs::Transform2D& t = *movers[i].first;
                ecs::Velocity&    v = *movers[i].second;
                t.position = t.position + v.value * dt;
                if (t.position.x < -kBound || t.position.x > kBound) v.value.x = -v.value.x;
                if (t.position.y < -kBound || t.position.y > kBound) v.value.y = -v.value.y;
            });
        }

        { VORTEX_PROFILE_ZONE("transform"); ecs::updateTransforms(scene.registry()); }

        // Serial extraction — timed for comparison only.
        { VORTEX_PROFILE_ZONE("extract.serial"); serialItems.clear(); ecs::extractSprites(scene.registry(), serialItems); }

        // Parallel extraction into the frame allocator — this is what we render.
        renderer::RenderItem* items = nullptr;
        usize itemCount = 0;
        {
            VORTEX_PROFILE_ZONE("extract.parallel");
            pairs.clear();
            scene.registry().view<ecs::WorldTransform2D, ecs::SpriteComp>(
                [&](ecs::Entity, ecs::WorldTransform2D& wt, ecs::SpriteComp& s) {
                    pairs.push_back({&wt, &s});
                });
            frameAlloc.reset();
            items     = frameAlloc.allocArray<renderer::RenderItem>(pairs.size());
            itemCount = pairs.size();
            jobs.parallelFor(itemCount, [&](usize i) {
                const ecs::WorldTransform2D& wt = *pairs[i].first;
                const ecs::SpriteComp&       s  = *pairs[i].second;
                items[i] = {.transform = wt.matrix * Mat4::scaling(s.size.x, s.size.y, 1.0f),
                            .color = s.color, .uv = s.uv, .texture = s.texture, .layer = s.layer};
            });
        }

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) { profiler::endFrame(); continue; }

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x0A0A12));
        pass.width = frame.width; pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0, .y = 0, .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        {
            VORTEX_PROFILE_ZONE("render");
            // World pass: the sprite swarm under the zoomed-out camera.
            batch.begin(scene.camera.viewProjection());
            batch.submit(items, itemCount);
            batch.end(*frame.cmd);

            // HUD from the previous completed frame's profiler snapshot: header
            // plus every zone sorted by time, so the hot path is obvious.
            f64 serialMs = 0.0, parallelMs = 0.0;
            for (const profiler::Entry& e : profiler::lastFrame()) {
                if (std::string_view(e.name) == "extract.serial")   serialMs   = e.ms;
                if (std::string_view(e.name) == "extract.parallel") parallelMs = e.ms;
            }
            const f64 speedup = parallelMs > 0.0 ? serialMs / parallelMs : 0.0;

            char hud[512];
            int n = std::snprintf(hud, sizeof(hud),
                                  "Sprites: %u   FPS: %.0f   Workers: %u\n"
                                  "extract speedup: %.1fx   frame arena: %zu KB\n"
                                  "-- profiler (ms/frame) --\n",
                                  spriteCount, fps, jobs.workerCount(), speedup,
                                  frameAlloc.used() / 1024);
            for (const profiler::Entry& e : profiler::lastFrame()) {
                if (n >= static_cast<int>(sizeof(hud))) break;
                n += std::snprintf(hud + n, sizeof(hud) - n, "%-18s %6.2f\n", e.name, e.ms);
            }

            // HUD pass: screen-space camera (zoom 1, origin centred) so text is
            // crisp and anchored top-left regardless of the world camera.
            renderer::Camera2D screen;
            screen.viewportWidth  = static_cast<f32>(w);
            screen.viewportHeight = static_cast<f32>(h);
            hudBatch.begin(screen.viewProjection());
            text::drawText(hudBatch, *font, hud,
                           {static_cast<f32>(-w) * 0.5f + 16.0f, static_cast<f32>(h) * 0.5f - 16.0f},
                           {0.9f, 0.95f, 1.0f, 1.0f}, 1.0f, 100);
            hudBatch.end(*frame.cmd);
        }

        frame.cmd->endRenderPass();
        device->endFrame();

        profiler::endFrame();
        ++frameCount;
    }

    device->waitIdle();
    device->destroyTexture(white);
    swapchain.reset();
    VORTEX_INFO("Bench", "Done after %llu frames.", static_cast<unsigned long long>(frameCount));
    return 0;
}
