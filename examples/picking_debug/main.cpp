// Picking debug tools: the overlay that draws the pick ray, the hit point, and the
// outline of the entity under the pointer. It reuses the real pick path, so it is
// also a check that picking and its visualisation agree.
//
// Headless self-check: VORTEX_PICKINGDEBUG_CHECK=1 aims the pointer at a box and off
// it, asserting the hit result is right and that the overlay actually emitted
// geometry (and rasterised, via an offscreen read-back). Exits non-zero otherwise.

#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/picking_debug.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/gizmos3d.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Picking Debug"});
    auto input  = pf::createInputProvider(*window);
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::Gizmos3D gizmos(*device, swapchain->format());

    // A single pickable box at the origin.
    ecs::Registry reg;
    const ecs::Entity box = reg.create();
    reg.emplace<ecs::Transform3D>(box, ecs::Transform3D{});
    reg.emplace<ecs::MeshPickable>(box, ecs::MeshPickable{
        .bounds = Aabb3D::fromCenterHalf({0.0f, 0.0f, 0.0f}, {0.6f, 0.6f, 0.6f})});

    constexpr u32 kSize = 256;
    renderer::Camera cam;
    cam.mode           = renderer::Camera::Mode::Perspective;
    cam.position       = {0.0f, 0.0f, 5.0f};
    cam.target         = {0.0f, 0.0f, 0.0f};
    cam.up             = {0.0f, 1.0f, 0.0f};
    cam.viewportWidth  = static_cast<f32>(kSize);
    cam.viewportHeight = static_cast<f32>(kSize);
    cam.aspect         = 1.0f;

    if (std::getenv("VORTEX_PICKINGDEBUG_CHECK")) {
        // Pointer on the box (centre) hits and outlines it.
        gizmos.begin();
        const ecs::MeshHit onBox = ecs::drawMeshPickDebug(gizmos, reg, cam, {128.0f, 128.0f});
        check(onBox.entity == box, "pointer at the centre hits the box");
        const usize hitVerts = gizmos.vertexCount();
        check(hitVerts > 0, "the overlay emitted geometry for the hit (ray + marker + bounds)");

        // Render the overlay into an offscreen target and confirm it rasterised.
        rhi::TextureHandle offscreen = device->createTexture(
            {.width = kSize, .height = kSize, .format = swapchain->format(),
             .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
             .debugName = "pickdbg_offscreen"});
        rhi::FrameContext frame = device->beginFrame(*swapchain);
        int lit = 0;
        if (frame.valid) {
            rhi::RenderPassDesc p0;
            p0.color.target = offscreen;
            p0.color.loadOp = rhi::LoadOp::Clear;
            p0.color.setClear(Color::fromRgb(0x05060A));
            p0.width = kSize; p0.height = kSize;
            frame.cmd->beginRenderPass(p0);
            frame.cmd->setViewport({.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(kSize), .height = static_cast<f32>(kSize)});
            frame.cmd->setScissor(0, 0, kSize, kSize);
            gizmos.flush(*frame.cmd, cam.viewProjection());
            frame.cmd->endRenderPass();

            rhi::RenderPassDesc bb;
            bb.color.target = frame.backbuffer;
            bb.color.loadOp = rhi::LoadOp::Clear;
            bb.color.setClear(Color::fromRgb(0x000000));
            bb.width = frame.width; bb.height = frame.height;
            frame.cmd->beginRenderPass(bb);
            frame.cmd->endRenderPass();
            device->endFrame();

            std::vector<u8> px(static_cast<usize>(kSize) * kSize * 4);
            device->readTexture(offscreen, px.data());
            const u32 clearKey = *reinterpret_cast<const u32*>(&px[0]);
            for (usize i = 0; i < px.size(); i += 4)
                if (*reinterpret_cast<const u32*>(&px[i]) != clearKey) ++lit;
        }
        check(lit > 0, "the overlay rasterised (offscreen read-back is non-empty)");

        // Pointer off the box misses: no hit, but the ray is still drawn.
        gizmos.begin();
        const ecs::MeshHit offBox = ecs::drawMeshPickDebug(gizmos, reg, cam, {10.0f, 10.0f});
        check(!offBox.valid(), "pointer in the corner misses the box");
        check(gizmos.vertexCount() > 0, "a miss still draws the ray");
        check(gizmos.vertexCount() < hitVerts, "a miss draws less than a hit (no marker or bounds)");

        device->waitIdle();
        device->destroyTexture(offscreen);
        swapchain.reset();
        std::printf("\n%s\n", g_failures == 0 ? "All picking-debug checks passed."
                                              : "picking-debug checks FAILED.");
        return g_failures == 0 ? 0 : 1;
    }

    // -------------------------------------------------------------- interactive
    VORTEX_INFO("PickingDebug", "Move the mouse over the box. ESC to quit.");
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;

    while (!window->shouldClose()) {
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w == 0 || h == 0) continue;
        cam.viewportWidth = static_cast<f32>(w);
        cam.viewportHeight = static_cast<f32>(h);
        cam.aspect = static_cast<f32>(w) / static_cast<f32>(h);

        f32 mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);

        gizmos.begin();
        ecs::drawMeshPickDebug(gizmos, reg, cam, {mx, my});

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
    VORTEX_INFO("PickingDebug", "Goodbye.");
    return 0;
}
