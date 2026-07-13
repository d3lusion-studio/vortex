#include "vortex/core/log.hpp"
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
#include "vortex/text/text_renderer.hpp"
#include "vortex/ui/ui.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace vortex;

#ifndef VORTEX_FONT_PATH
#define VORTEX_FONT_PATH "/usr/share/fonts/TTF/DejaVuSans.ttf"
#endif

namespace {

// Pick the first font that exists, so the demo runs across distros.
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
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Text + UI"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 8192);

    const std::string fontPath = findFont(*fs);
    if (fontPath.empty()) {
        VORTEX_ERROR("Demo", "No usable system font found; set VORTEX_FONT_PATH.");
        return 1;
    }
    auto title = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 48.0f);
    auto body  = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 22.0f);
    if (!title || !body) return 1;

    ui::UI gui(*device);

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    int  counter = 0;
    bool dark    = true;
    bool quit    = false;

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;

    VORTEX_INFO("App", "Click the buttons. ESC to quit.");

    while (!window->shouldClose() && !quit) {
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
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        float mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);
        ui::InputState in;
        in.mouse    = camera.screenToWorld(mx, my);
        in.down     = input->isMouseDown(pf::MouseButton::Left);
        in.pressed  = input->isMousePressed(pf::MouseButton::Left);
        in.released = input->isMouseReleased(pf::MouseButton::Left);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        const f32 bg = dark ? 0.06f : 0.85f;
        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        // `bg` is authored the way it looks on screen, so it decodes like any other
        // sRGB value before it reaches a target the hardware will re-encode.
        pass.color.setClear({srgbToLinear(bg), srgbToLinear(bg), srgbToLinear(bg * 1.1f), 1.0f});
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        batch.begin(camera.viewProjection());

        // Plain world-space text (no UI), to show the text path on its own.
        const Vec4 ink = dark ? Vec4{0.9f, 0.95f, 1.0f, 1.0f} : Vec4{0.08f, 0.09f, 0.12f, 1.0f};
        text::drawText(batch, *title, "Vortex Engine", {-560.0f, 300.0f}, ink, 1.0f, 10);
        text::drawText(batch, *body,
                       "Phase 6: text rendering + immediate-mode UI.\n"
                       "stb_truetype atlas, tinted through the sprite batch.",
                       {-560.0f, 230.0f}, ink, 1.0f, 10);

        // UI panel with a vertical stack of widgets.
        gui.begin(batch, *body, in);
        gui.panel({0.0f, -40.0f}, {360.0f, 320.0f});

        gui.beginColumn({0.0f, 80.0f}, {300.0f, 44.0f}, 16.0f);

        char buf[64];
        std::snprintf(buf, sizeof(buf), "Counter: %d", counter);
        gui.label(buf);

        if (gui.button("Increment")) ++counter;
        if (gui.button("Decrement")) --counter;
        if (gui.button(dark ? "Theme: Dark" : "Theme: Light")) dark = !dark;
        if (gui.button("Quit")) quit = true;

        gui.end();

        batch.end(*frame.cmd);
        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    device->waitIdle();
    swapchain.reset();
    VORTEX_INFO("App", "Final counter = %d. Goodbye.", counter);
    return 0;
}
