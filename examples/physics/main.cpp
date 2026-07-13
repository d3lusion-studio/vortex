#include "vortex/audio/audio.hpp"
#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/physics/components.hpp"
#include "vortex/physics/physics_world.hpp"
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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

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
    };
    for (const char* c : candidates)
        if (fs.exists(c)) return c;
    return {};
}

template <class T>
void appendLE(std::vector<u8>& out, T value) {
    for (usize i = 0; i < sizeof(T); ++i) out.push_back(static_cast<u8>(value >> (8 * i)));
}


bool writeBeepWav(pf::IFileSystem& fs, const std::string& path) {
    constexpr u32 rate = 44100;
    constexpr f32 dur  = 0.09f;
    const u32 frames   = static_cast<u32>(rate * dur);

    std::vector<u8> pcm;
    for (u32 i = 0; i < frames; ++i) {
        const f32 t   = static_cast<f32>(i) / rate;
        const f32 env = 1.0f - static_cast<f32>(i) / frames;
        const f32 s   = std::sin(2.0f * 3.14159265f * 600.0f * t) * env * 0.4f;
        appendLE<i16>(pcm, static_cast<i16>(s * 32767.0f));
    }

    const u32 dataBytes = static_cast<u32>(pcm.size());
    std::vector<u8> wav;
    const auto tag = [&](const char* s) { for (int i = 0; i < 4; ++i) wav.push_back(static_cast<u8>(s[i])); };
    tag("RIFF"); appendLE<u32>(wav, 36 + dataBytes); tag("WAVE");
    tag("fmt "); appendLE<u32>(wav, 16); appendLE<u16>(wav, 1); appendLE<u16>(wav, 1);
    appendLE<u32>(wav, rate); appendLE<u32>(wav, rate * 2); appendLE<u16>(wav, 2); appendLE<u16>(wav, 16);
    tag("data"); appendLE<u32>(wav, dataBytes);
    wav.insert(wav.end(), pcm.begin(), pcm.end());
    return fs.writeFile(path.c_str(), wav.data(), wav.size());
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Physics"});
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
    if (fontPath.empty()) { VORTEX_ERROR("Demo", "No system font found."); return 1; }
    auto font = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 24.0f);
    if (!font) return 1;

    ui::UI gui(*device);

    const u8 whitePx[4] = {255, 255, 255, 255};
    const rhi::TextureHandle white =
        device->createTexture({.width = 1, .height = 1, .debugName = "white"}, whitePx);

    // Optional audio: a click on every collision.
    auto audio = audio::createAudioEngine();
    audio::SoundHandle clickSound;
    if (audio) {
        const std::string wav = "/tmp/vortex_click.wav";
        if (writeBeepWav(*fs, wav)) clickSound = audio->load(wav.c_str());
    }

    physics::PhysicsWorld physics({.gravity = {0.0f, -9.81f}, .pixelsPerMeter = 50.0f});

    ecs::Scene scene;
    scene.camera.viewportWidth  = static_cast<f32>(fbw);
    scene.camera.viewportHeight = static_cast<f32>(fbh);
    scene.addSystem([&](ecs::Registry& reg, f32 dt) { physics.step(reg, dt); });

    int collisions = 0;
    physics.setContactBegin([&](ecs::Entity, ecs::Entity) {
        ++collisions;
        if (audio && clickSound.valid()) audio->play(clickSound);
    });

    // Static arena: floor + two side walls.
    auto addStatic = [&](Vec2 pos, Vec2 half, Vec4 color) {
        const ecs::Entity e = scene.spawn();
        scene.registry().get<ecs::Transform2D>(e).position = pos;
        scene.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
            .texture = white, .color = color, .size = {half.x * 2.0f, half.y * 2.0f}});
        scene.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
            .type = physics::BodyType::Static});
        scene.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{.halfExtents = half});
    };
    addStatic({0.0f, -300.0f},  {600.0f, 20.0f}, {0.30f, 0.32f, 0.40f, 1.0f});
    addStatic({-600.0f, 0.0f},  {20.0f, 300.0f}, {0.30f, 0.32f, 0.40f, 1.0f});
    addStatic({ 600.0f, 0.0f},  {20.0f, 300.0f}, {0.30f, 0.32f, 0.40f, 1.0f});

    std::mt19937 rng(1234);
    std::uniform_real_distribution<f32> xDist(-450.0f, 450.0f);
    std::uniform_real_distribution<f32> hue(0.2f, 1.0f);

    std::vector<ecs::Entity> boxes;
    auto spawnBox = [&]() {
        if (boxes.size() >= 80) return;
        const ecs::Entity e = scene.spawn();
        scene.registry().get<ecs::Transform2D>(e).position = {xDist(rng), 320.0f};
        const Vec4 color{hue(rng), hue(rng), hue(rng), 1.0f};
        scene.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
            .texture = white, .color = color, .size = {44.0f, 44.0f}});
        scene.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
            .type = physics::BodyType::Dynamic, .restitution = 0.25f});
        scene.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{
            .halfExtents = {22.0f, 22.0f}});
        boxes.push_back(e);
    };
    auto reset = [&]() {
        for (ecs::Entity e : boxes) scene.destroy(e);
        boxes.clear();
        collisions = 0;
    };

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f64 spawnAccum = 0.0;
    std::vector<renderer::RenderItem> items;

    VORTEX_INFO("App", "Boxes fall and collide. Click 'Spawn' or wait. ESC to quit.");

    while (!window->shouldClose()) {
        clock->tick();
        const f32 dt = static_cast<f32>(clock->deltaTime());
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
        scene.camera.viewportWidth  = static_cast<f32>(w);
        scene.camera.viewportHeight = static_cast<f32>(h);

        // Auto-spawn a steady trickle so the scene is lively on its own.
        spawnAccum += dt;
        if (spawnAccum >= 0.6) { spawnAccum = 0.0; spawnBox(); }

        scene.update(dt);
        scene.extract(items);

        float mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);
        ui::InputState in;
        in.mouse    = scene.camera.screenToWorld(mx, my);
        in.down     = input->isMouseDown(pf::MouseButton::Left);
        in.pressed  = input->isMousePressed(pf::MouseButton::Left);
        in.released = input->isMouseReleased(pf::MouseButton::Left);

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

        batch.begin(scene.camera.viewProjection());
        batch.submit(items.data(), items.size());

        char hud[128];
        std::snprintf(hud, sizeof(hud), "Boxes: %zu / 80\nCollisions: %d\nAudio: %s",
                      boxes.size(), collisions, audio ? "on" : "off");
        text::drawText(batch, *font, hud, {-620.0f, 330.0f}, {0.9f, 0.95f, 1.0f, 1.0f}, 1.0f, 10);

        gui.begin(batch, *font, in);
        gui.beginColumn({480.0f, 300.0f}, {220.0f, 40.0f}, 14.0f);
        if (gui.button("Spawn box")) spawnBox();
        if (gui.button("Reset"))     reset();
        gui.end();

        batch.end(*frame.cmd);
        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
    }

    device->waitIdle();
    if (audio && clickSound.valid()) audio->unload(clickSound);
    device->destroyTexture(white);
    swapchain.reset();
    VORTEX_INFO("App", "Spawned %zu boxes, %d collisions. Goodbye.", boxes.size(), collisions);
    return 0;
}
