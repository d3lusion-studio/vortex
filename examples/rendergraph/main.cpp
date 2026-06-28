#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/render_graph.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

std::vector<std::byte> loadSpirv(pf::IFileSystem& fs, const char* path) {
    auto bytes = fs.readFile(path);
    if (bytes.empty()) VORTEX_ERROR("App", "Failed to load shader: %s", path);
    return bytes;
}

std::vector<u8> solid(u8 r, u8 g, u8 b) { return {r, g, b, 255}; }

struct Mover {
    Vec2 pos, vel, size;
    Vec4 color;
};

constexpr f32 kWorldW = 1200.0f;
constexpr f32 kWorldH = 680.0f;

struct PostPush { f32 tint[4]; };

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Render Graph"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(rhi::GraphicsAPI::Vulkan, *window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    const rhi::Format colorFormat = swapchain->format();
    const rhi::Format depthFormat = rhi::Format::D32_SFLOAT;

    // Sprites are depth-tested into the scene pass's depth target.
    renderer::SpriteBatch batch(*device, colorFormat, 4096, depthFormat);

    // A couple of solid 1x1 textures to colour the sprites.
    rhi::TextureHandle texA = device->createTexture({.width = 1, .height = 1}, solid(255, 255, 255).data());

    // Fullscreen post-process pipeline (no vertex buffer, samples set 0).
    rhi::GraphicsPipelineDesc post;
    post.vertexSpirv       = loadSpirv(*fs, VORTEX_RG_SHADER_DIR "/post.vert.spv");
    post.fragmentSpirv     = loadSpirv(*fs, VORTEX_RG_SHADER_DIR "/post.frag.spv");
    post.colorFormat       = colorFormat;
    post.hasMaterialTexture = true;
    post.pushConstantSize  = sizeof(PostPush);
    post.debugName         = "post_pipeline";
    rhi::PipelineHandle postPipeline = device->createGraphicsPipeline(post);

    renderer::RenderGraph graph(*device);

    renderer::Camera camera;
    camera.mode = renderer::Camera::Mode::Orthographic;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    std::vector<Mover> movers;
    for (int i = 0; i < 60; ++i) {
        const f32 t = static_cast<f32>(i) / 60.0f;
        movers.push_back({
            .pos  = {(t - 0.5f) * kWorldW, std::sin(t * 12.0f) * 250.0f},
            .vel  = {std::cos(t * 7.0f) * 140.0f, std::sin(t * 5.0f) * 140.0f},
            .size = {44.0f, 44.0f},
            .color = {0.4f + 0.6f * t, 0.7f, 1.0f - 0.5f * t, 1.0f},
        });
    }

    bool postEnabled = std::getenv("VORTEX_RG_NO_POST") == nullptr;
    VORTEX_INFO("App", "Multi-pass render graph. P toggles the post pass. ESC to quit.");

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
        if (input->isKeyPressed(pf::Key::P)) {
            postEnabled = !postEnabled;
            VORTEX_INFO("App", "post pass %s", postEnabled ? "ON" : "OFF");
        }

        const f32 dt = static_cast<f32>(clock->deltaTime());
        t += dt;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        for (Mover& m : movers) {
            m.pos = m.pos + m.vel * dt;
            if (m.pos.x < -kWorldW * 0.5f || m.pos.x > kWorldW * 0.5f) m.vel.x = -m.vel.x;
            if (m.pos.y < -kWorldH * 0.5f || m.pos.y > kWorldH * 0.5f) m.vel.y = -m.vel.y;
        }

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        // Build this frame's sprite batch (CPU side; recorded inside the pass).
        batch.begin(camera.viewProjection());
        for (const Mover& m : movers)
            batch.draw({.position = m.pos, .size = m.size, .color = m.color, .texture = texA});

        graph.beginFrame();
        const auto backbuffer = graph.importBackbuffer(frame.backbuffer, frame.width, frame.height);
        const auto sceneDepth = graph.depthTarget("scene_depth", frame.width, frame.height);

        const f32 sceneClear[4] = {0.05f, 0.06f, 0.09f, 1.0f};
        const f32 vp[4]         = {0.0f, 0.0f, static_cast<f32>(frame.width),
                                   static_cast<f32>(frame.height)};

        auto recordScene = [&](rhi::ICommandList& cmd) {
            cmd.setViewport({.x = vp[0], .y = vp[1], .width = vp[2], .height = vp[3]});
            cmd.setScissor(0, 0, frame.width, frame.height);
            batch.end(cmd);
        };

        if (postEnabled) {
            const auto sceneColor =
                graph.colorTarget("scene_color", frame.width, frame.height, colorFormat);

            graph.addPass("scene",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.writeColor(sceneColor, sceneClear);
                    b.writeDepth(sceneDepth);
                },
                recordScene);

            graph.addPass("post",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.sample(sceneColor);
                    const f32 black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    b.writeColor(backbuffer, black);
                },
                [&](rhi::ICommandList& cmd) {
                    cmd.setViewport({.x = vp[0], .y = vp[1], .width = vp[2], .height = vp[3]});
                    cmd.setScissor(0, 0, frame.width, frame.height);
                    cmd.setPipeline(postPipeline);
                    // Animated warm tint, so the post effect is obvious.
                    PostPush pc{{1.0f, 0.85f + 0.15f * std::sin(t), 0.7f, 1.0f}};
                    cmd.pushConstants(&pc, sizeof(pc));
                    cmd.setBindGroup(0, graph.sampledBindGroup(sceneColor));
                    cmd.draw(3);
                });
        } else {
            graph.addPass("scene",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.writeColor(backbuffer, sceneClear);
                    b.writeDepth(sceneDepth);
                },
                recordScene);
        }

        graph.execute(*frame.cmd);
        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    device->destroyPipeline(postPipeline);
    device->destroyTexture(texA);
    swapchain.reset();
    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
