// 3D gizmos: world-space debug lines — a grid, axes, a wire box and a wire sphere —
// drawn through renderer::Gizmos3D. This is what you reach for to see an entity's
// transform, a bounding volume, or a ray, none of which have geometry of their own.
//
// Headless self-check: VORTEX_GIZMOS3D_CHECK=1 renders the axes gizmo into an
// offscreen target and reads it back, asserting that distinctly red, green and blue
// pixels are present — i.e. the X/Y/Z lines actually rasterised. Exits non-zero
// otherwise (needs a GPU).

#include "vortex/core/log.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/gizmos3d.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

// Fill the gizmo buffer with a small demo scene.
void buildScene(renderer::Gizmos3D& giz) {
    giz.begin();
    giz.grid({0.0f, 0.0f, 0.0f}, 8.0f, 8, {0.35f, 0.38f, 0.45f, 1.0f});
    giz.axes(Mat4::identity(), 2.0f);
    giz.box({2.0f, 1.0f, 0.0f}, {0.6f, 0.6f, 0.6f}, {1.0f, 0.85f, 0.2f, 1.0f});
    giz.wireSphere({-2.0f, 1.0f, 0.0f}, 0.9f, {0.2f, 0.9f, 0.95f, 1.0f});
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex 3D Gizmos"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::Gizmos3D gizmos(*device, swapchain->format());

    renderer::Camera cam;
    cam.mode        = renderer::Camera::Mode::Perspective;
    cam.fovYRadians = 1.0471975512f;
    cam.nearZ = 0.1f;
    cam.farZ  = 100.0f;
    cam.up     = {0.0f, 1.0f, 0.0f};
    cam.target = {0.0f, 0.5f, 0.0f};

    // -------------------------------------------------------------- headless check
    if (std::getenv("VORTEX_GIZMOS3D_CHECK")) {
        constexpr u32 kSize = 256;
        rhi::TextureHandle offscreen = device->createTexture(
            {.width = kSize, .height = kSize, .format = swapchain->format(),
             .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
             .debugName = "gizmos_offscreen"});

        cam.aspect = 1.0f;
        cam.position = {4.0f, 4.0f, 4.0f};
        buildScene(gizmos);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        bool pass = false;
        if (frame.valid) {
            rhi::RenderPassDesc pass0;
            pass0.color.target = offscreen;
            pass0.color.loadOp = rhi::LoadOp::Clear;
            pass0.color.setClear(Color::fromRgb(0x05060A));
            pass0.width = kSize; pass0.height = kSize;
            frame.cmd->beginRenderPass(pass0);
            frame.cmd->setViewport({.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(kSize), .height = static_cast<f32>(kSize)});
            frame.cmd->setScissor(0, 0, kSize, kSize);
            gizmos.flush(*frame.cmd, cam.viewProjection());
            frame.cmd->endRenderPass();

            rhi::RenderPassDesc bb;   // keep the backbuffer defined for present
            bb.color.target = frame.backbuffer;
            bb.color.loadOp = rhi::LoadOp::Clear;
            bb.color.setClear(Color::fromRgb(0x000000));
            bb.width = frame.width; bb.height = frame.height;
            frame.cmd->beginRenderPass(bb);
            frame.cmd->endRenderPass();
            device->endFrame();

            std::vector<u8> px(static_cast<usize>(kSize) * kSize * 4);
            device->readTexture(offscreen, px.data());   // BGRA8

            int red = 0, green = 0, blue = 0, lit = 0;
            const u32 clearKey = *reinterpret_cast<const u32*>(&px[0]);
            for (usize i = 0; i < px.size(); i += 4) {
                if (*reinterpret_cast<const u32*>(&px[i]) != clearKey) ++lit;
                // Detect axis colours by which channel dominates — robust to the
                // sRGB encoding the swapchain target applies.
                const int b = px[i + 0], g = px[i + 1], r = px[i + 2];
                if (r > 150 && r > g + 40 && r > b + 40) ++red;    // X axis
                if (g > 150 && g > r + 40 && g > b + 40) ++green;  // Y axis
                if (b > 150 && b > r + 40 && b > g + 40) ++blue;   // Z axis
            }
            pass = lit > 0 && red > 0 && green > 0 && blue > 0;
            std::printf("\n[%s] Gizmos3D self-check: %d lit px, axes red=%d green=%d blue=%d\n",
                        pass ? "PASS" : "FAIL", lit, red, green, blue);
        }

        device->waitIdle();
        device->destroyTexture(offscreen);
        swapchain.reset();
        return pass ? 0 : 1;
    }

    // -------------------------------------------------------------- interactive
    VORTEX_INFO("Gizmos3D", "Orbiting 3D debug gizmos. ESC to quit.");
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f32 t = 0.0f;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;
        t += static_cast<f32>(clock->deltaTime());

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) { swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h)); lastW = w; lastH = h; }
        if (w == 0 || h == 0) continue;
        cam.aspect   = static_cast<f32>(w) / static_cast<f32>(h);
        cam.position = {std::sin(t * 0.4f) * 6.0f, 4.0f, std::cos(t * 0.4f) * 6.0f};

        buildScene(gizmos);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x05060A));
        pass.width = frame.width; pass.height = frame.height;
        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(frame.width), .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);
        gizmos.flush(*frame.cmd, cam.viewProjection());
        frame.cmd->endRenderPass();
        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    swapchain.reset();
    VORTEX_INFO("Gizmos3D", "Goodbye.");
    return 0;
}
