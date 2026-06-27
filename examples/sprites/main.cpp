#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
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

std::vector<u8> makeChecker(u32 size) {
    std::vector<u8> px(static_cast<usize>(size) * size * 4);
    for (u32 y = 0; y < size; ++y)
        for (u32 x = 0; x < size; ++x) {
            const bool on = ((x / 8) + (y / 8)) % 2 == 0;
            u8* p = &px[(static_cast<usize>(y) * size + x) * 4];
            p[0] = p[1] = p[2] = on ? 240 : 60;
            p[3] = 255;
        }
    return px;
}

struct Mover {
    Vec2 pos, vel, size;
    Vec4 color;
    rhi::TextureHandle tex;
};

} // namespace

int main() {
    const char* spritesEnv = std::getenv("VORTEX_SPRITES");
    const u32 spriteCount = spritesEnv ? static_cast<u32>(std::strtoul(spritesEnv, nullptr, 10)) : 20000;

    auto window = pf::createWindow({.width = 1280, .height = 720,
                                    .title = "Vortex Sprites"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(rhi::GraphicsAPI::Vulkan, *window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 200000);

    auto white  = makeSolid(4, 255, 255, 255);
    auto circle = makeCircle(64);
    auto check  = makeChecker(64);
    rhi::TextureHandle texWhite  = device->createTexture({.width = 4,  .height = 4},  white.data());
    rhi::TextureHandle texCircle = device->createTexture({.width = 64, .height = 64}, circle.data());
    rhi::TextureHandle texCheck  = device->createTexture({.width = 64, .height = 64}, check.data());
    rhi::TextureHandle textures[] = {texWhite, texCircle, texCheck};

    const f32 worldW = 2400.0f, worldH = 1600.0f;
    std::mt19937 rng(1234);
    std::uniform_real_distribution<f32> distX(-worldW * 0.5f, worldW * 0.5f);
    std::uniform_real_distribution<f32> distY(-worldH * 0.5f, worldH * 0.5f);
    std::uniform_real_distribution<f32> distV(-120.0f, 120.0f);
    std::uniform_real_distribution<f32> distS(12.0f, 36.0f);
    std::uniform_real_distribution<f32> distC(0.3f, 1.0f);
    std::uniform_int_distribution<int>  distT(0, 2);

    std::vector<Mover> movers(spriteCount);
    for (Mover& m : movers) {
        m.pos  = {distX(rng), distY(rng)};
        m.vel  = {distV(rng), distV(rng)};
        const f32 s = distS(rng);
        m.size = {s, s};
        m.color = {distC(rng), distC(rng), distC(rng), 1.0f};
        m.tex  = textures[distT(rng)];
    }

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    VORTEX_INFO("App", "%u sprites, 3 textures. WASD pan, scroll zoom, ESC quit.", spriteCount);

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    f64 fpsAccum = 0.0; u32 fpsFrames = 0;
    int lastW = fbw, lastH = fbh;

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
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        const f32 panSpeed = 400.0f / camera.zoom * dt;
        if (input->isKeyDown(pf::Key::A) || input->isKeyDown(pf::Key::Left))  camera.position.x -= panSpeed;
        if (input->isKeyDown(pf::Key::D) || input->isKeyDown(pf::Key::Right)) camera.position.x += panSpeed;
        if (input->isKeyDown(pf::Key::W) || input->isKeyDown(pf::Key::Up))    camera.position.y += panSpeed;
        if (input->isKeyDown(pf::Key::S) || input->isKeyDown(pf::Key::Down))  camera.position.y -= panSpeed;
        if (const f32 scroll = input->scrollDelta(); scroll != 0.0f)
            camera.zoom = std::max(0.1f, camera.zoom * (1.0f + scroll * 0.1f));

        for (Mover& m : movers) {
            m.pos = m.pos + m.vel * dt;
            if (m.pos.x < -worldW * 0.5f || m.pos.x > worldW * 0.5f) m.vel.x = -m.vel.x;
            if (m.pos.y < -worldH * 0.5f || m.pos.y > worldH * 0.5f) m.vel.y = -m.vel.y;
        }

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

        batch.begin(camera.viewProjection());
        for (const Mover& m : movers)
            batch.drawSprite(m.tex, m.pos, m.size, m.color);
        batch.end(*frame.cmd);

        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
        fpsAccum += clock->deltaTime();
        ++fpsFrames;
        if (fpsAccum >= 1.0) {
            VORTEX_INFO("App", "%.0f FPS | %u sprites | %u draw calls",
                        fpsFrames / fpsAccum, batch.spriteCount(), batch.drawCallCount());
            fpsAccum = 0.0; fpsFrames = 0;
        }
    }

    device->waitIdle();
    device->destroyTexture(texWhite);
    device->destroyTexture(texCircle);
    device->destroyTexture(texCheck);
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
