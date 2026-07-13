#include "vortex/core/log.hpp"
#include "vortex/debug/imgui_layer.hpp"
#include "vortex/debug/inspector.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstdlib>
#include <random>
#include <vector>

using namespace vortex;

namespace {
std::vector<u8> makeSolid(u32 size, u8 r, u8 g, u8 b) {
    std::vector<u8> px(static_cast<usize>(size) * size * 4);
    for (usize i = 0; i < px.size(); i += 4) { px[i] = r; px[i+1] = g; px[i+2] = b; px[i+3] = 255; }
    return px;
}
constexpr f32 kWorldW = 1100.0f;
constexpr f32 kWorldH = 620.0f;
}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex ImGui Inspector"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 4096);
    debug::ImGuiLayer    imgui(*device, swapchain->format());
    debug::EntityInspector inspector;

    auto whitePx = makeSolid(4, 255, 255, 255);
    rhi::TextureHandle white = device->createTexture({.width = 4, .height = 4}, whitePx.data());

    std::mt19937 rng(11);
    std::uniform_real_distribution<f32> distX(-kWorldW * 0.5f, kWorldW * 0.5f);
    std::uniform_real_distribution<f32> distY(-kWorldH * 0.5f, kWorldH * 0.5f);
    std::uniform_real_distribution<f32> distV(-160.0f, 160.0f);
    std::uniform_real_distribution<f32> distC(0.3f, 1.0f);

    ecs::Scene scene;
    for (int i = 0; i < 40; ++i) {
        ecs::Entity e = scene.spawn();
        scene.registry().get<ecs::Transform2D>(e).position = {distX(rng), distY(rng)};
        scene.registry().emplace<ecs::Velocity>(e, ecs::Velocity{{distV(rng), distV(rng)}});
        scene.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
            .texture = white, .color = {distC(rng), distC(rng), distC(rng), 1.0f},
            .size = {40.0f, 40.0f}, .layer = 0});
    }

    scene.addSystem([](ecs::Registry& reg, f32 dt) {
        reg.view<ecs::Transform2D, ecs::Velocity>(
            [dt](ecs::Entity, ecs::Transform2D& t, ecs::Velocity& v) {
                t.position = t.position + v.value * dt;
                if (t.position.x < -kWorldW * 0.5f || t.position.x > kWorldW * 0.5f) v.value.x = -v.value.x;
                if (t.position.y < -kWorldH * 0.5f || t.position.y > kWorldH * 0.5f) v.value.y = -v.value.y;
            });
    });

    scene.camera.viewportWidth  = static_cast<f32>(fbw);
    scene.camera.viewportHeight = static_cast<f32>(fbh);

    VORTEX_INFO("App", "Drag the Inspector windows; edit components live. ESC to quit.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    bool showDemo = false;
    std::vector<renderer::RenderItem> items;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        const f32 dt = static_cast<f32>(clock->deltaTime());

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        scene.camera.viewportWidth  = static_cast<f32>(w);
        scene.camera.viewportHeight = static_cast<f32>(h);

        // Feed ImGui input from the platform provider.
        float mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);
        debug::ImGuiInput in;
        in.displayWidth  = static_cast<f32>(w);
        in.displayHeight = static_cast<f32>(h);
        in.mouse = {mx, my};
        in.mouseDown[0] = input->isMouseDown(pf::MouseButton::Left);
        in.mouseDown[1] = input->isMouseDown(pf::MouseButton::Right);
        in.mouseDown[2] = input->isMouseDown(pf::MouseButton::Middle);
        in.scroll = input->scrollDelta();

        imgui.newFrame(in, dt);
        inspector.draw(scene);
        if (showDemo) imgui.showDemoWindow(&showDemo);

        scene.update(dt);
        scene.extract(items);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x0F121A));
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        batch.begin(scene.camera.viewProjection());
        batch.submit(items.data(), items.size());
        batch.end(*frame.cmd);

        imgui.render(*frame.cmd);   // ImGui on top of the game

        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    device->waitIdle();
    if (white.valid()) device->destroyTexture(white);
    swapchain.reset();
    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
