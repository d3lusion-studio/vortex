// What an atlas is actually for: collapsing draw calls.
//
// The same 64 sprites are drawn twice a second, alternating between two setups that
// look identical on screen:
//
//   LOOSE   64 separate device textures  -> 64 draw calls
//   ATLAS   the same 64 images, packed   ->  1 draw call
//
// Press SPACE to hold one mode. The draw-call count is logged every second.

#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/atlas.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace vortex;

namespace {

constexpr u32 kSpriteCount = 64;
constexpr u32 kImageSize   = 32;

// A distinct little image per sprite, so a packing bug shows up as the wrong picture
// rather than as a picture that happens to still look plausible.
std::vector<u8> makeImage(u32 index) {
    std::vector<u8> px(static_cast<usize>(kImageSize) * kImageSize * 4, 0);
    const f32 hue = static_cast<f32>(index) / static_cast<f32>(kSpriteCount);
    const u8  r   = static_cast<u8>(120 + 130 * std::sin(hue * 6.28f));
    const u8  g   = static_cast<u8>(120 + 130 * std::sin(hue * 6.28f + 2.09f));
    const u8  b   = static_cast<u8>(120 + 130 * std::sin(hue * 6.28f + 4.19f));

    const u32 bars = 1 + (index % 5);   // 1..5 stripes: a per-image fingerprint
    for (u32 y = 0; y < kImageSize; ++y)
        for (u32 x = 0; x < kImageSize; ++x) {
            const bool on = (x * bars / kImageSize) % 2 == 0;
            u8* p = &px[(static_cast<usize>(y) * kImageSize + x) * 4];
            p[0] = on ? r : static_cast<u8>(r / 3);
            p[1] = on ? g : static_cast<u8>(g / 3);
            p[2] = on ? b : static_cast<u8>(b / 3);
            p[3] = 255;
        }
    return px;
}

} // namespace

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Atlas"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 1024);

    // Setup 1: every image its own texture. This is what loading a folder of PNGs
    // through a plain texture loader gives you.
    std::vector<rhi::TextureHandle> loose;
    loose.reserve(kSpriteCount);

    // Setup 2: the same images, packed onto as few pages as they fit on.
    renderer::AtlasBuilder builder(/*pageSize=*/512, /*padding=*/1);
    std::vector<u32> ids;
    ids.reserve(kSpriteCount);

    for (u32 i = 0; i < kSpriteCount; ++i) {
        const std::vector<u8> px = makeImage(i);
        loose.push_back(device->createTexture({.width = kImageSize, .height = kImageSize}, px.data()));
        ids.push_back(builder.add("img" + std::to_string(i), px.data(), kImageSize, kImageSize));
    }

    renderer::TextureAtlas atlas = builder.build(*device);

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    VORTEX_INFO("App", "SPACE holds a mode; otherwise it alternates every second. ESC quits.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64  frameCount = 0;
    f32  elapsed    = 0.0f;
    bool useAtlas   = false;
    bool hold       = false;
    int  lastW = fbw, lastH = fbh;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;
        if (input->isKeyPressed(pf::Key::Space)) hold = !hold;

        const f32 dt = static_cast<f32>(clock->deltaTime());
        elapsed += dt;
        if (!hold) useAtlas = static_cast<int>(elapsed) % 2 == 1;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x0A0D17));
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width  = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        batch.begin(camera.viewProjection());
        for (u32 i = 0; i < kSpriteCount; ++i) {
            const f32 col = static_cast<f32>(i % 8);
            const f32 row = static_cast<f32>(i / 8);
            const Vec2 pos{-420.0f + col * 120.0f, 260.0f - row * 120.0f};

            renderer::Sprite s;
            s.position = pos;
            s.size     = {96.0f, 96.0f};
            s.sampler  = renderer::SpriteSampler::NearestClamp;

            if (useAtlas) {
                // Same picture, but every sprite now names the same page texture — which
                // is the entire reason the batch can fold them into one draw call.
                const renderer::TextureRegion r = atlas.region(ids[i]);
                s.texture = r.texture;
                s.uv      = r.uv;
            } else {
                s.texture = loose[i];
            }
            batch.draw(s);
        }
        batch.end(*frame.cmd);

        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
        if (frameCount % 60 == 0) {
            VORTEX_INFO("App", "%s | %u sprites | %u draw calls",
                        useAtlas ? "ATLAS" : "LOOSE", batch.spriteCount(), batch.drawCallCount());
        }
    }

    device->waitIdle();
    for (rhi::TextureHandle t : loose) {
        batch.releaseTexture(t);
        device->destroyTexture(t);
    }
    for (rhi::TextureHandle p : atlas.pages()) batch.releaseTexture(p);
    atlas.destroy(*device);
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
