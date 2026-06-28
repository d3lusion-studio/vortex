#include "vortex/core/console.hpp"
#include "vortex/core/log.hpp"
#include "vortex/debug/debug_draw.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"
#include "vortex/text/font.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

using namespace vortex;

#ifndef VORTEX_FONT_PATH
#define VORTEX_FONT_PATH "/usr/share/fonts/TTF/DejaVuSans.ttf"
#endif

namespace {
std::string findFont(pf::IFileSystem& fs) {
    const char* candidates[] = {
        VORTEX_FONT_PATH,
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    };
    for (const char* c : candidates)
        if (fs.exists(c)) return c;
    return {};
}
}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Debug Draw"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(rhi::GraphicsAPI::Vulkan, *window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 16384);

    const std::string fontPath = findFont(*fs);
    std::unique_ptr<text::Font> font;
    if (!fontPath.empty())
        font = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 22.0f);

    debug::DebugDraw dbg(*device);
    const auto catGrid   = dbg.category("grid");
    const auto catShapes = dbg.category("shapes");
    const auto catLabels = dbg.category("labels");

    // A console command that prints all registered cvars/commands.
    Console::global().registerCommand("list", [](const std::vector<std::string>&) {
        for (const auto& e : Console::global().list())
            VORTEX_INFO("Console", "  %-20s %s  // %s", e.name.c_str(),
                        e.isCommand ? "(cmd)" : e.value.c_str(), e.help.c_str());
    }, "list cvars and commands");
    Console::global().execute("list");

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    VORTEX_INFO("App", "1/2/3 toggle grid/shapes/labels. ESC to quit.");

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

        if (input->isKeyPressed(pf::Key::Num1)) dbg.setEnabled(catGrid,   !dbg.enabled(catGrid));
        if (input->isKeyPressed(pf::Key::Num2)) dbg.setEnabled(catShapes, !dbg.enabled(catShapes));
        if (input->isKeyPressed(pf::Key::Num3)) dbg.setEnabled(catLabels, !dbg.enabled(catLabels));

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);
        t += static_cast<f32>(clock->deltaTime());

        dbg.begin();

        // Grid lines.
        const f32 halfW = w * 0.5f, halfH = h * 0.5f;
        const Vec4 gridColor{0.22f, 0.24f, 0.30f, 1.0f};
        for (f32 x = -halfW; x <= halfW; x += 80.0f)
            dbg.line({x, -halfH}, {x, halfH}, gridColor, 1.0f, catGrid);
        for (f32 y = -halfH; y <= halfH; y += 80.0f)
            dbg.line({-halfW, y}, {halfW, y}, gridColor, 1.0f, catGrid);

        // Animated shapes.
        const f32 r = 140.0f + std::sin(t * 1.5f) * 40.0f;
        dbg.circle({0.0f, 0.0f}, r, {0.4f, 0.8f, 1.0f, 1.0f}, 48, 2.0f, catShapes);
        dbg.box({0.0f, 0.0f}, {r * 1.4f, r * 1.4f}, {1.0f, 0.6f, 0.2f, 1.0f}, 2.0f, catShapes);
        dbg.line({-300.0f, -200.0f}, {300.0f * std::cos(t), 200.0f * std::sin(t)},
                 {0.9f, 0.3f, 0.5f, 1.0f}, 3.0f, catShapes);
        dbg.filledBox({0.0f, -260.0f}, {120.0f, 30.0f}, {0.2f, 0.9f, 0.5f, 0.6f}, catShapes);

        // Labels.
        dbg.text({-halfW + 16.0f, halfH - 16.0f}, "Vortex debug draw", {0.9f, 0.95f, 1.0f, 1.0f},
                 catLabels);
        dbg.text({0.0f, 30.0f}, "world origin", {1.0f, 1.0f, 0.6f, 1.0f}, catLabels);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.clearColor[0] = 0.05f;
        pass.color.clearColor[1] = 0.06f;
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
        dbg.flush(batch, font.get());
        batch.end(*frame.cmd);
        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    device->waitIdle();
    font.reset();
    swapchain.reset();
    return 0;
}
