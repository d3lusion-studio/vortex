// Everything the 2D sprite path grew in the P0 slice, on one screen:
//
//   row 1  filtering — the same 16x16 sheet drawn big, linear vs nearest
//   row 2  tiling    — one quad, uv taken past 1.0, with a Repeat sampler
//   row 3  flipping  — an asymmetric glyph mirrored on each axis
//   row 4  anchors   — the same sprite spun about three different anchor points
//   row 5  CPU drawing — a texture whose pixels are written every frame
//
// Left-click and drag over the bottom panel to paint into it.

#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstdlib>
#include <vector>

using namespace vortex;
using renderer::Sprite;
using renderer::SpriteSampler;

namespace {

constexpr u32 kPixelArtSize = 16;
constexpr u32 kCanvasSize   = 96;

// A tiny, deliberately chunky sprite. Scaled up 12x it is the clearest possible
// demonstration of what the sampler choice does.
std::vector<u8> makePixelArt() {
    std::vector<u8> px(static_cast<usize>(kPixelArtSize) * kPixelArtSize * 4, 0);
    for (u32 y = 0; y < kPixelArtSize; ++y)
        for (u32 x = 0; x < kPixelArtSize; ++x) {
            const bool on = ((x / 2) + (y / 2)) % 2 == 0;
            u8* p = &px[(static_cast<usize>(y) * kPixelArtSize + x) * 4];
            p[0] = on ? 250 : 40;
            p[1] = on ? 190 : 60;
            p[2] = on ? 80  : 120;
            p[3] = 255;
        }
    return px;
}

// An "F": no symmetry on either axis, so a flip is unmistakable.
std::vector<u8> makeGlyph() {
    constexpr u32 n = 16;
    std::vector<u8> px(static_cast<usize>(n) * n * 4, 0);
    const auto set = [&](u32 x, u32 y) {
        u8* p = &px[(static_cast<usize>(y) * n + x) * 4];
        p[0] = 120; p[1] = 220; p[2] = 255; p[3] = 255;
    };
    for (u32 y = 2; y < 14; ++y) set(4, y);          // spine
    for (u32 x = 4; x < 13; ++x) set(x, 2);          // top bar
    for (u32 x = 4; x < 11; ++x) set(x, 7);          // middle bar
    return px;
}

} // namespace

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Sprite 2D"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 1024);

    const std::vector<u8> art   = makePixelArt();
    const std::vector<u8> glyph = makeGlyph();

    rhi::TextureHandle texArt = device->createTexture(
        {.width = kPixelArtSize, .height = kPixelArtSize, .debugName = "pixel_art"}, art.data());
    rhi::TextureHandle texGlyph = device->createTexture(
        {.width = 16, .height = 16, .debugName = "glyph"}, glyph.data());

    // CopyDst is what makes this one writable after creation. Without it the texture
    // is immutable and updateTexture() has nothing to copy into.
    rhi::TextureHandle texCanvas = device->createTexture(
        {.width = kCanvasSize, .height = kCanvasSize,
         .usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
         .debugName = "canvas"});

    std::vector<u8> canvas(static_cast<usize>(kCanvasSize) * kCanvasSize * 4, 0);
    for (usize i = 0; i < canvas.size(); i += 4) {
        canvas[i] = 25; canvas[i + 1] = 28; canvas[i + 2] = 42; canvas[i + 3] = 255;
    }
    device->updateTexture(texCanvas, canvas.data(), 0, 0, kCanvasSize, kCanvasSize);
    bool canvasDirty = false;

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    VORTEX_INFO("App", "Left-drag over the bottom panel to paint. ESC quits.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    f32 spin = 0.0f;
    int lastW = fbw, lastH = fbh;

    // The painting panel, in world space. Kept here because both the draw call and
    // the hit test need to agree on exactly where it is.
    const Vec2 canvasCenter{0.0f, -250.0f};
    const Vec2 canvasSize{192.0f, 192.0f};

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        const f32 dt = static_cast<f32>(clock->deltaTime());
        spin += dt;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        // --- CPU drawing: mouse -> world -> canvas texel, written straight to the GPU.
        if (input->isMouseDown(pf::MouseButton::Left)) {
            float mx = 0.0f, my = 0.0f;
            input->mousePosition(mx, my);
            const Vec2 world = camera.screenToWorld(mx, my);
            const Vec2 local{(world.x - canvasCenter.x) / canvasSize.x + 0.5f,
                             0.5f - (world.y - canvasCenter.y) / canvasSize.y};   // v grows downwards
            if (local.x >= 0.0f && local.x < 1.0f && local.y >= 0.0f && local.y < 1.0f) {
                const auto cx = static_cast<i32>(local.x * kCanvasSize);
                const auto cy = static_cast<i32>(local.y * kCanvasSize);
                for (i32 dy = -2; dy <= 2; ++dy)
                    for (i32 dx = -2; dx <= 2; ++dx) {
                        const i32 px = cx + dx, py = cy + dy;
                        if (px < 0 || py < 0 || px >= static_cast<i32>(kCanvasSize) ||
                            py >= static_cast<i32>(kCanvasSize)) continue;
                        u8* p = &canvas[(static_cast<usize>(py) * kCanvasSize + px) * 4];
                        p[0] = 255; p[1] = 210; p[2] = 90; p[3] = 255;
                    }
                canvasDirty = true;
            }
        }
        if (canvasDirty) {
            device->updateTexture(texCanvas, canvas.data(), 0, 0, kCanvasSize, kCanvasSize);
            canvasDirty = false;
        }

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

        // Row 1 — filtering. Same texture, same size, one sampler apart.
        batch.draw({.position = {-420.0f, 240.0f}, .size = {160.0f, 160.0f},
                    .texture = texArt, .sampler = SpriteSampler::LinearClamp});
        batch.draw({.position = {-230.0f, 240.0f}, .size = {160.0f, 160.0f},
                    .texture = texArt, .sampler = SpriteSampler::NearestClamp});

        // Row 2 — tiling. The quad is one sprite; the uv rect runs 0..4 and 0..3, and
        // a Repeat sampler wraps that into a 4x3 grid of the texture.
        batch.draw({.position = {80.0f, 240.0f}, .size = {320.0f, 240.0f},
                    .uv = {0.0f, 0.0f, 4.0f, 3.0f},
                    .texture = texArt, .sampler = SpriteSampler::NearestRepeat});

        // Row 3 — flipping, all four combinations.
        for (int i = 0; i < 4; ++i) {
            batch.draw({.position = {-420.0f + static_cast<f32>(i) * 110.0f, 40.0f},
                        .size = {96.0f, 96.0f},
                        .texture = texGlyph,
                        .sampler = SpriteSampler::NearestClamp,
                        .flipX = (i & 1) != 0,
                        .flipY = (i & 2) != 0});
        }

        // Row 4 — anchors. Three sprites at the SAME position, spinning; each turns
        // about a different point of its own quad, so they sweep different arcs.
        const Vec2 anchors[3] = {{0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.5f}};
        for (int i = 0; i < 3; ++i) {
            batch.draw({.position = {80.0f + static_cast<f32>(i) * 150.0f, 40.0f},
                        .size = {80.0f, 80.0f},
                        .rotation = spin,
                        .color = {1.0f, 1.0f, 1.0f, 0.85f},
                        .texture = texGlyph,
                        .sampler = SpriteSampler::NearestClamp,
                        .anchor = anchors[i]});
            // A dot marking the position the sprite is anchored to.
            batch.draw({.position = {80.0f + static_cast<f32>(i) * 150.0f, 40.0f},
                        .size = {8.0f, 8.0f},
                        .color = {1.0f, 0.3f, 0.3f, 1.0f},
                        .texture = texArt,
                        .layer = 1,
                        .sampler = SpriteSampler::NearestClamp});
        }

        // Row 5 — the painted canvas.
        batch.draw({.position = canvasCenter, .size = canvasSize,
                    .texture = texCanvas, .sampler = SpriteSampler::NearestClamp});

        batch.end(*frame.cmd);
        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    device->waitIdle();
    batch.releaseTexture(texCanvas);
    batch.releaseTexture(texGlyph);
    batch.releaseTexture(texArt);
    device->destroyTexture(texCanvas);
    device->destroyTexture(texGlyph);
    device->destroyTexture(texArt);
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
