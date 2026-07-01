#include "game_api.h"

#include "vortex/core/console.hpp"
#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/dynamic_library.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
using namespace vortex;

#ifndef VORTEX_GAME_LIB
#define VORTEX_GAME_LIB "libgame.so"
#endif

namespace {
struct LoadedGame {
    std::unique_ptr<pf::DynamicLibrary> lib;
    const GameApi*                      api = nullptr;
    fs::path                            tempPath;
    i64                                 sourceMTime = 0;
};

bool loadGame(LoadedGame& out, u32 reloadCounter) {
    const fs::path source = VORTEX_GAME_LIB;
    std::error_code ec;

    fs::path temp = source;
    temp += ".hot" + std::to_string(reloadCounter);
    fs::copy_file(source, temp, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        VORTEX_ERROR("HotReload", "copy '%s' failed: %s", source.string().c_str(),
                     ec.message().c_str());
        return false;
    }

    auto lib = pf::DynamicLibrary::load(temp.string().c_str());
    if (!lib) { fs::remove(temp, ec); return false; }

    auto entry = lib->symbolAs<const GameApi* (*)()>("vortex_game_api");
    if (!entry) {
        VORTEX_ERROR("HotReload", "missing vortex_game_api in '%s'", source.string().c_str());
        fs::remove(temp, ec);
        return false;
    }
    const GameApi* api = entry();
    if (!api || api->version != VORTEX_GAME_API_VERSION) {
        VORTEX_ERROR("HotReload", "game ABI version mismatch (got %d, want %d)",
                     api ? api->version : -1, VORTEX_GAME_API_VERSION);
        fs::remove(temp, ec);
        return false;
    }

    out.lib         = std::move(lib);
    out.api         = api;
    out.tempPath    = temp;
    out.sourceMTime = pf::fileModifiedTime(source.string().c_str());
    return true;
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Hot Reload"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 4096);
    const u8 whitePx[4] = {255, 255, 255, 255};
    rhi::TextureHandle white =
        device->createTexture({.width = 1, .height = 1, .debugName = "hot_white"}, whitePx);

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    // Persistent game memory: survives reloads, the game lays its state over it.
    std::vector<unsigned char> gameMemory(64 * 1024, 0);
    std::vector<GameBox>        boxes(256);

    u32        reloadCounter = 0;
    LoadedGame game;
    if (!loadGame(game, reloadCounter++)) {
        VORTEX_ERROR("HotReload", "could not load initial game module");
        return 1;
    }

    GameContext ctx{};
    ctx.memory     = gameMemory.data();
    ctx.memorySize = gameMemory.size();
    ctx.boxes      = boxes.data();
    ctx.boxCap     = static_cast<int>(boxes.size());

    game.api->on_load(&ctx);
    VORTEX_INFO("HotReload", "running. Rebuild the 'game' target to hot-reload. ESC to quit.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f64 reloadCheck = 0.0;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        const f32 dt = static_cast<f32>(clock->deltaTime());

        // Poll the .so for changes a few times a second.
        reloadCheck += dt;
        if (reloadCheck > 0.25) {
            reloadCheck = 0.0;
            const i64 mtime = pf::fileModifiedTime(fs::path(VORTEX_GAME_LIB).string().c_str());
            if (mtime != 0 && mtime != game.sourceMTime) {
                VORTEX_INFO("HotReload", "change detected, reloading game module...");
                game.api->on_unload(&ctx);
                LoadedGame next;
                if (loadGame(next, reloadCounter++)) {
                    std::error_code ec;
                    fs::remove(game.tempPath, ec);   // drop the previous temp copy
                    game = std::move(next);
                    game.api->on_load(&ctx);
                    VORTEX_INFO("HotReload", "reloaded.");
                } else {
                    game.api->on_load(&ctx);         // keep running the old module
                }
            }
        }

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        ctx.dt      = dt;
        ctx.time   += dt;
        ctx.width   = w;
        ctx.height  = h;
        ctx.inLeft  = input->isKeyDown(pf::Key::Left)  || input->isKeyDown(pf::Key::A);
        ctx.inRight = input->isKeyDown(pf::Key::Right) || input->isKeyDown(pf::Key::D);
        ctx.inUp    = input->isKeyDown(pf::Key::Up)    || input->isKeyDown(pf::Key::W);
        ctx.inDown  = input->isKeyDown(pf::Key::Down)  || input->isKeyDown(pf::Key::S);
        ctx.boxCount = 0;

        if (!game.api->update(&ctx)) break;

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
        for (int i = 0; i < ctx.boxCount; ++i) {
            const GameBox& b = ctx.boxes[i];
            batch.drawSprite(white, {b.x, b.y}, {b.w, b.h}, {b.r, b.g, b.b, b.a});
        }
        batch.end(*frame.cmd);
        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    game.api->on_unload(&ctx);
    device->waitIdle();
    if (white.valid()) device->destroyTexture(white);
    std::error_code ec;
    fs::remove(game.tempPath, ec);
    swapchain.reset();
    VORTEX_INFO("HotReload", "goodbye.");
    return 0;
}
