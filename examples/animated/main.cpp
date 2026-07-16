// An animated shader: a full-screen effect driven by the time since startup, pushed
// to the GPU as a push constant every frame. It is the simplest "dynamic data in a
// shader" example, and the one every other time-based effect is a variation on.
//
// Headless self-check: VORTEX_ANIMATED_CHECK=1 renders the effect into an offscreen
// texture at two different times, reads both back, and asserts they differ — proof
// the animation is actually a function of time and not a static image. Exits
// non-zero otherwise (needs a GPU, like any RHI example).

#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace vortex;

namespace {

std::vector<std::byte> loadSpirv(pf::IFileSystem& fs, const char* path) {
    auto bytes = fs.readFile(path);
    if (bytes.empty()) VORTEX_ERROR("Animated", "Failed to load shader: %s", path);
    return bytes;
}

// Record a full-screen pass that draws the effect at `time` into `target`.
void drawEffect(rhi::ICommandList& cmd, rhi::TextureHandle target, u32 w, u32 h,
                rhi::PipelineHandle pipeline, f32 time) {
    rhi::RenderPassDesc pass;
    pass.color.target = target;
    pass.color.loadOp = rhi::LoadOp::Clear;
    pass.color.setClear(Color::fromRgb(0x0D0D14));
    pass.width  = w;
    pass.height = h;

    cmd.beginRenderPass(pass);
    cmd.setPipeline(pipeline);
    cmd.setViewport({.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(w), .height = static_cast<f32>(h)});
    cmd.setScissor(0, 0, w, h);
    cmd.pushConstants(&time, sizeof(f32));   // vertex-stage: the shader forwards it to fragment
    cmd.draw(3);                             // full-screen triangle, no vertex buffer
    cmd.endRenderPass();
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Animated Shader"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv       = loadSpirv(*fs, VORTEX_ANIMATED_SHADER_DIR "/animated.vert.spv");
    pd.fragmentSpirv     = loadSpirv(*fs, VORTEX_ANIMATED_SHADER_DIR "/animated.frag.spv");
    pd.colorFormat       = swapchain->format();
    pd.pushConstantSize  = sizeof(f32);
    pd.debugName         = "animated_pipeline";
    rhi::PipelineHandle pipeline = device->createGraphicsPipeline(pd);

    // -------------------------------------------------------------- headless check
    if (std::getenv("VORTEX_ANIMATED_CHECK")) {
        constexpr u32 kSize = 64;
        rhi::TextureHandle offscreen = device->createTexture(
            {.width = kSize, .height = kSize, .format = swapchain->format(),
             .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
             .debugName = "animated_offscreen"});

        std::vector<u8> frameA(static_cast<usize>(kSize) * kSize * 4);
        std::vector<u8> frameB(frameA.size());

        auto renderAt = [&](f32 time, std::vector<u8>& out) {
            rhi::FrameContext frame = device->beginFrame(*swapchain);
            if (!frame.valid) return false;
            drawEffect(*frame.cmd, offscreen, kSize, kSize, pipeline, time);
            // Keep the presented backbuffer defined: a bare clear pass.
            rhi::RenderPassDesc bb;
            bb.color.target = frame.backbuffer;
            bb.color.loadOp = rhi::LoadOp::Clear;
            bb.color.setClear(Color::fromRgb(0x000000));
            bb.width = frame.width; bb.height = frame.height;
            frame.cmd->beginRenderPass(bb);
            frame.cmd->endRenderPass();
            device->endFrame();
            device->readTexture(offscreen, out.data());
            return true;
        };

        const bool okA = renderAt(0.0f, frameA);
        const bool okB = renderAt(1.5f, frameB);
        const bool differ = okA && okB && std::memcmp(frameA.data(), frameB.data(), frameA.size()) != 0;
        // The image must also not be blank (all one colour): a real gradient varies.
        bool varies = false;
        for (usize i = 4; i < frameA.size() && !varies; i += 4)
            if (std::memcmp(&frameA[i], &frameA[0], 4) != 0) varies = true;

        const bool pass = differ && varies;
        std::printf("\n[%s] Animated self-check: two times %s, image %s\n",
                    pass ? "PASS" : "FAIL",
                    differ ? "differ" : "MATCH", varies ? "has detail" : "is FLAT");
        device->waitIdle();
        device->destroyTexture(offscreen);
        device->destroyPipeline(pipeline);
        swapchain.reset();
        return pass ? 0 : 1;
    }

    // -------------------------------------------------------------- interactive
    VORTEX_INFO("Animated", "A time-driven full-screen shader. ESC to quit.");
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;

    while (!window->shouldClose()) {
        clock->tick();
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

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;
        drawEffect(*frame.cmd, frame.backbuffer, frame.width, frame.height,
                   pipeline, static_cast<f32>(clock->time()));
        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    device->destroyPipeline(pipeline);
    swapchain.reset();
    VORTEX_INFO("Animated", "Goodbye.");
    return 0;
}
