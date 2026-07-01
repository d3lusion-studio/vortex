#include "vortex/asset/asset_manager.hpp"
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

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace vortex;

#ifndef VORTEX_ASSET_DIR
#define VORTEX_ASSET_DIR "assets"
#endif

namespace {

// Scale a texture's native size to fit inside a square box while keeping aspect.
Vec2 fitInto(u32 texW, u32 texH, f32 box) {
    if (texW == 0 || texH == 0) return {box, box};
    const f32 aspect = static_cast<f32>(texW) / static_cast<f32>(texH);
    return aspect >= 1.0f ? Vec2{box, box / aspect}
                          : Vec2{box * aspect, box};
}

}

int main() {
    const std::filesystem::path assetDir = VORTEX_ASSET_DIR;
    const std::array<std::string, 4> names = {"meme1.png", "meme2.png",
                                              "meme3.png", "meme4.png"};

    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Assets"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 1024);
    assets::AssetManager assetMgr(*device, *fs);

    std::array<assets::TextureHandle, 4> textures{};
    std::array<std::string, 4>           paths{};
    for (usize i = 0; i < names.size(); ++i) {
        paths[i]    = (assetDir / names[i]).string();
        textures[i] = assetMgr.loadTexture(paths[i].c_str());
        if (!assetMgr.get(textures[i]))
            VORTEX_ERROR("Demo", "Could not load %s", paths[i].c_str());
    }

    // Cache demo: loading the same path again returns the same handle.
    const assets::TextureHandle dup = assetMgr.loadTexture(paths[0].c_str());
    VORTEX_INFO("Demo", "Cache hit: same path -> same handle = %s",
                (dup == textures[0]) ? "PASS" : "FAIL");

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    VORTEX_INFO("App", "Rendering %zu textures from %s. Edit a file to hot-reload. ESC quit.",
                names.size(), assetDir.string().c_str());

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
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        assetMgr.beginFrame();
        assetMgr.pollHotReload();   // picks up external edits to the meme files

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

        // Lay the four textures out in a centered row.
        batch.begin(camera.viewProjection());
        constexpr f32 box     = 280.0f;
        constexpr f32 spacing = 300.0f;
        const f32 startX = -spacing * (textures.size() - 1) * 0.5f;
        for (usize i = 0; i < textures.size(); ++i) {
            const assets::TextureAsset* a = assetMgr.get(textures[i]);
            if (!a) continue;
            const Vec2 size = fitInto(a->width, a->height, box);
            batch.drawSprite(a->gpu, {startX + spacing * static_cast<f32>(i), 0.0f}, size);
        }
        batch.end(*frame.cmd);

        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    device->waitIdle();
    VORTEX_INFO("App", "Live textures still resident: %zu", assetMgr.liveTextureCount());
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
