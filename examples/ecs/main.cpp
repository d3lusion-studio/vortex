#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <algorithm>
#include <cstdlib>
#include <random>
#include <vector>

using namespace vortex;

namespace {

struct Spin {
    f32 speed = 0.0f;
};

std::vector<u8> makeSolid(u32 size, u8 r, u8 g, u8 b) {
    std::vector<u8> px(static_cast<usize>(size) * size * 4);
    for (usize i = 0; i < px.size(); i += 4) { px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = 255; }
    return px;
}

std::vector<u8> makeCircle(u32 size) {
    std::vector<u8> px(static_cast<usize>(size) * size * 4, 0);
    const f32 c = size * 0.5f, rad = size * 0.5f;
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x) {
            const f32 dx = x + 0.5f - c, dy = y + 0.5f - c;
            const bool inside = (dx * dx + dy * dy) <= rad * rad;
            u8* p = &px[(static_cast<usize>(y) * size + x) * 4];
            p[0] = p[1] = p[2] = 255;
            p[3] = inside ? 255 : 0;
        }
    return px;
}

constexpr f32 kWorldW = 2400.0f;
constexpr f32 kWorldH = 1600.0f;

struct Demo {
    ecs::Scene&        scene;
    rhi::TextureHandle hub;
    rhi::TextureHandle dot;
    std::mt19937&      rng;

    ecs::Entity spawnSatellite() {
        std::uniform_real_distribution<f32> distX(-kWorldW * 0.5f, kWorldW * 0.5f);
        std::uniform_real_distribution<f32> distY(-kWorldH * 0.5f, kWorldH * 0.5f);
        std::uniform_real_distribution<f32> distV(-90.0f, 90.0f);
        std::uniform_real_distribution<f32> distSpin(-2.5f, 2.5f);
        std::uniform_real_distribution<f32> distC(0.4f, 1.0f);

        ecs::Entity parent = scene.spawn();
        auto& pt = scene.registry().get<ecs::Transform2D>(parent);
        pt.position = {distX(rng), distY(rng)};
        scene.registry().emplace<ecs::Velocity>(parent, ecs::Velocity{{distV(rng), distV(rng)}});
        scene.registry().emplace<Spin>(parent, Spin{distSpin(rng)});
        scene.registry().emplace<ecs::SpriteComp>(parent, ecs::SpriteComp{
            .texture = hub, .color = {0.9f, 0.9f, 0.95f, 1.0f}, .size = {22.0f, 22.0f}, .layer = 1});

        constexpr f32 radius = 70.0f;
        const Vec2 offsets[4] = {{radius, 0}, {-radius, 0}, {0, radius}, {0, -radius}};
        for (const Vec2 off : offsets) {
            ecs::Entity child = scene.spawn();
            scene.registry().get<ecs::Transform2D>(child).position = off;
            scene.registry().emplace<ecs::Parent>(child, ecs::Parent{parent});
            scene.registry().emplace<ecs::SpriteComp>(child, ecs::SpriteComp{
                .texture = dot,
                .color   = {distC(rng), distC(rng), distC(rng), 1.0f},
                .size    = {26.0f, 26.0f},
                .layer   = 0});
        }
        return parent;
    }
};

}

int main() {
    const char* countEnv = std::getenv("VORTEX_SATELLITES");
    const u32 satelliteCount = countEnv ? static_cast<u32>(std::strtoul(countEnv, nullptr, 10)) : 600;

    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex ECS"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 200000);

    auto hubPx = makeSolid(8, 230, 230, 245);
    auto dotPx = makeCircle(48);
    rhi::TextureHandle texHub = device->createTexture({.width = 8,  .height = 8},  hubPx.data());
    rhi::TextureHandle texDot = device->createTexture({.width = 48, .height = 48}, dotPx.data());

    std::mt19937 rng(7);
    ecs::Scene scene;
    Demo demo{scene, texHub, texDot, rng};

    scene.addSystem([](ecs::Registry& reg, f32 dt) {
        reg.view<ecs::Transform2D, ecs::Velocity>(
            [dt](ecs::Entity, ecs::Transform2D& t, ecs::Velocity& v) {
                t.position = t.position + v.value * dt;
                if (t.position.x < -kWorldW * 0.5f || t.position.x > kWorldW * 0.5f) v.value.x = -v.value.x;
                if (t.position.y < -kWorldH * 0.5f || t.position.y > kWorldH * 0.5f) v.value.y = -v.value.y;
            });
    });

    // Spin system: advance rotation so parented children orbit.
    scene.addSystem([](ecs::Registry& reg, f32 dt) {
        reg.view<ecs::Transform2D, Spin>([dt](ecs::Entity, ecs::Transform2D& t, Spin& s) {
            t.rotation += s.speed * dt;
        });
    });

    std::vector<ecs::Entity> satellites;
    satellites.reserve(satelliteCount);
    for (u32 i = 0; i < satelliteCount; ++i) satellites.push_back(demo.spawnSatellite());

    scene.camera.viewportWidth  = static_cast<f32>(fbw);
    scene.camera.viewportHeight = static_cast<f32>(fbh);
    scene.camera.zoom = 0.5f;

    VORTEX_INFO("App", "%u satellites (x5 entities each). WASD pan, scroll zoom, ESC quit.",
                satelliteCount);

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    f64 fpsAccum = 0.0; u32 fpsFrames = 0;
    f64 churnAccum = 0.0;
    int lastW = fbw, lastH = fbh;
    std::vector<renderer::RenderItem> items;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        const f32 dt = static_cast<f32>(clock->deltaTime());

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        scene.camera.viewportWidth  = static_cast<f32>(w);
        scene.camera.viewportHeight = static_cast<f32>(h);

        const f32 panSpeed = 400.0f / scene.camera.zoom * dt;
        if (input->isKeyDown(pf::Key::A) || input->isKeyDown(pf::Key::Left))  scene.camera.position.x -= panSpeed;
        if (input->isKeyDown(pf::Key::D) || input->isKeyDown(pf::Key::Right)) scene.camera.position.x += panSpeed;
        if (input->isKeyDown(pf::Key::W) || input->isKeyDown(pf::Key::Up))    scene.camera.position.y += panSpeed;
        if (input->isKeyDown(pf::Key::S) || input->isKeyDown(pf::Key::Down))  scene.camera.position.y -= panSpeed;
        if (const f32 scroll = input->scrollDelta(); scroll != 0.0f)
            scene.camera.zoom = std::max(0.1f, scene.camera.zoom * (1.0f + scroll * 0.1f));

        // Recycle a few satellites each second to stress entity create/destroy.
        churnAccum += clock->deltaTime();
        if (churnAccum >= 0.5 && !satellites.empty()) {
            churnAccum = 0.0;
            for (int k = 0; k < 8 && !satellites.empty(); ++k) {
                std::uniform_int_distribution<usize> pick(0, satellites.size() - 1);
                const usize idx = pick(rng);
                // Destroy the parent; orphaned children fall back to their local
                // transform, so tear them down too for a clean recycle.
                ecs::Entity parent = satellites[idx];
                std::vector<ecs::Entity> kids;
                scene.registry().view<ecs::Parent>([&](ecs::Entity e, ecs::Parent& p) {
                    if (p.value == parent) kids.push_back(e);
                });
                for (ecs::Entity kid : kids) scene.destroy(kid);
                scene.destroy(parent);
                satellites[idx] = demo.spawnSatellite();
            }
        }

        scene.update(dt);
        scene.extract(items);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.clearColor[0] = 0.04f;
        pass.color.clearColor[1] = 0.05f;
        pass.color.clearColor[2] = 0.09f;
        pass.color.clearColor[3] = 1.0f;
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        batch.begin(scene.camera.viewProjection());
        batch.submit(items.data(), items.size());
        batch.end(*frame.cmd);

        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
        fpsAccum += clock->deltaTime();
        ++fpsFrames;
        if (fpsAccum >= 1.0) {
            VORTEX_INFO("App", "%.0f FPS | %zu entities | %u sprites | %u draw calls",
                        fpsFrames / fpsAccum, scene.registry().aliveCount(),
                        batch.spriteCount(), batch.drawCallCount());
            fpsAccum = 0.0; fpsFrames = 0;
        }
    }

    device->waitIdle();
    device->destroyTexture(texHub);
    device->destroyTexture(texDot);
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
