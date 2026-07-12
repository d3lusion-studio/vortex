#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/ecs/systems.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/post_process.hpp"
#include "vortex/renderer/render_graph.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace vortex;

int main() {
    auto window    = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Mesh 3D"});
    auto input     = pf::createInputProvider(*window);
    auto clock     = pf::createClock();
    auto device    = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    const rhi::Format swapFormat  = swapchain->format();
    const rhi::Format depthFormat = rhi::Format::D32_SFLOAT;
    // The scene is rendered into a float HDR target; post then tone-maps it to
    // the swapchain. Mesh and overlay pipelines therefore target the HDR format.
    const rhi::Format hdrFormat   = rhi::Format::R32G32B32A32_SFLOAT;
    constexpr u32     kShadowRes  = 2048;

    renderer::MeshRenderer mesh(*device, hdrFormat, depthFormat);
    // Overlay sprites: no depth, so they always sit on top of the 3D scene.
    renderer::SpriteBatch  batch(*device, hdrFormat, 256);
    renderer::PostProcess  post(*device, hdrFormat, swapFormat);
    renderer::RenderGraph  graph(*device);

    const renderer::MeshHandle cubeMesh  = mesh.createMesh(renderer::makeCube(1.0f));
    const renderer::MeshHandle planeMesh = mesh.createMesh(renderer::makePlane(24.0f));
    const renderer::MeshHandle ballMesh  = mesh.createMesh(renderer::makeSphere());

    rhi::TextureHandle white = device->createTexture(
        {.width = 1, .height = 1}, std::vector<u8>{255, 255, 255, 255}.data());

    // --- Scene: a ground plane + a ring of spinning cubes + a centre sphere. ---
    ecs::Registry reg;

    const ecs::Entity ground = reg.create();
    reg.emplace<ecs::Transform3D>(ground, ecs::Transform3D{.position = {0.0f, -1.0f, 0.0f}});
    reg.emplace<ecs::MeshComp>(ground, ecs::MeshComp{.mesh = planeMesh,
                                                     .color = {0.30f, 0.32f, 0.36f, 1.0f},
                                                     .metallic = 0.0f, .roughness = 0.9f});

    // A polished gold sphere: metallic with low roughness shows off PBR + bloom.
    const ecs::Entity centre = reg.create();
    reg.emplace<ecs::Transform3D>(centre, ecs::Transform3D{});
    reg.emplace<ecs::MeshComp>(centre, ecs::MeshComp{.mesh = ballMesh,
                                                     .color = {1.0f, 0.78f, 0.34f, 1.0f},
                                                     .metallic = 1.0f, .roughness = 0.18f});

    constexpr int kCubeCount = 8;
    std::vector<ecs::Entity> cubes;
    for (int i = 0; i < kCubeCount; ++i) {
        const f32 a = 6.2831853f * static_cast<f32>(i) / kCubeCount;
        const f32 t = static_cast<f32>(i) / kCubeCount;
        const ecs::Entity e = reg.create();
        reg.emplace<ecs::Transform3D>(e, ecs::Transform3D{
            .position = {std::cos(a) * 3.2f, 0.0f, std::sin(a) * 3.2f}});
        reg.emplace<ecs::MeshComp>(e, ecs::MeshComp{
            .mesh = cubeMesh, .color = {0.3f + 0.6f * t, 0.5f, 1.0f - 0.5f * t, 1.0f},
            .metallic = t, .roughness = 0.2f + 0.7f * (1.0f - t)});
        cubes.push_back(e);
    }

    renderer::Camera cam;
    cam.mode = renderer::Camera::Mode::Perspective;
    cam.fovYRadians = 1.0471975512f;   // 60 degrees
    cam.nearZ = 0.1f;
    cam.farZ  = 100.0f;
    cam.up    = {0.0f, 1.0f, 0.0f};
    cam.target = {0.0f, 0.0f, 0.0f};

    renderer::Camera uiCam;             // orthographic, for the 2D overlay
    uiCam.mode = renderer::Camera::Mode::Orthographic;

    VORTEX_INFO("App", "Lit 3D mesh + 2D overlay. The light orbits the scene. ESC to quit.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f32 t = 0.0f;

    std::vector<renderer::MeshInstance> instances;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        const f32 dt = static_cast<f32>(clock->deltaTime());
        t += dt;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        if (w == 0 || h == 0) continue;

        // Orbiting camera + animated directional light (so shading visibly moves).
        cam.aspect   = static_cast<f32>(w) / static_cast<f32>(h);
        cam.position = {std::sin(t * 0.3f) * 7.0f, 3.0f, std::cos(t * 0.3f) * 7.0f};

        renderer::DirectionalLight light;
        light.direction = {std::cos(t * 0.7f), -0.8f, std::sin(t * 0.7f)};
        light.intensity = 3.0f;                 // HDR: bright enough for specular to bloom
        light.shadowTarget = {0.0f, 0.0f, 0.0f};
        light.shadowExtent = 8.0f;              // covers the sphere + cube ring
        light.shadowDistance = 30.0f;

        // Spin the ring cubes and the centre sphere via their ECS transforms.
        for (int i = 0; i < kCubeCount; ++i)
            reg.get<ecs::Transform3D>(cubes[i]).rotation =
                Quat::fromAxisAngle({0.3f, 1.0f, 0.2f}, t * 1.5f + static_cast<f32>(i));
        reg.get<ecs::Transform3D>(centre).rotation = Quat::fromAxisAngle({0, 1, 0}, t * 0.6f);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        instances.clear();
        ecs::extractMeshes(reg, instances);
        mesh.begin(cam, light);
        mesh.submit(instances.data(), instances.size());

        uiCam.viewportWidth  = static_cast<f32>(w);
        uiCam.viewportHeight = static_cast<f32>(h);
        batch.begin(uiCam.viewProjection());
        // A small HUD bar pinned near the top — pure 2D, drawn over the 3D scene.
        batch.drawSprite(white, {0.0f, static_cast<f32>(h) * 0.5f - 24.0f},
                         {static_cast<f32>(w), 36.0f}, {0.06f, 0.07f, 0.10f, 0.85f});
        batch.drawSprite(white, {-static_cast<f32>(w) * 0.5f + 60.0f,
                                 static_cast<f32>(h) * 0.5f - 24.0f},
                         {72.0f, 20.0f}, {0.9f, 0.5f, 0.2f, 1.0f});

        graph.beginFrame();
        const auto backbuffer = graph.importBackbuffer(frame.backbuffer, frame.width, frame.height);
        const auto sceneHdr   = graph.colorTarget("scene_hdr", frame.width, frame.height, hdrFormat);
        const auto sceneDepth = graph.depthTarget("scene_depth", frame.width, frame.height);
        const auto shadowMap  = graph.depthTarget("shadow_map", kShadowRes, kShadowRes,
                                                  /*sampled=*/true);

        const f32 clear[4] = {0.02f, 0.03f, 0.05f, 1.0f};
        const rhi::Viewport vp{.x = 0.0f, .y = 0.0f,
                               .width = static_cast<f32>(frame.width),
                               .height = static_cast<f32>(frame.height)};

        // 1) Shadow pass: depth-only, from the light's point of view.
        graph.addPass("shadow",
            [&](renderer::RenderGraph::PassBuilder& b) { b.writeDepth(shadowMap); },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport({.x = 0.0f, .y = 0.0f,
                                 .width = static_cast<f32>(kShadowRes),
                                 .height = static_cast<f32>(kShadowRes)});
                cmd.setScissor(0, 0, kShadowRes, kShadowRes);
                mesh.renderShadow(cmd);
            });

        // 2) Lit scene into the HDR target, sampling the shadow map.
        graph.addPass("mesh",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.sample(shadowMap);
                b.writeColor(sceneHdr, clear);
                b.writeDepth(sceneDepth);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport(vp);
                cmd.setScissor(0, 0, frame.width, frame.height);
                mesh.end(cmd, graph.sampledBindGroup(shadowMap));
            });

        // 3) 2D overlay on top of the scene (still HDR).
        graph.addPass("overlay",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.writeColor(sceneHdr, clear, rhi::LoadOp::Load);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport(vp);
                cmd.setScissor(0, 0, frame.width, frame.height);
                batch.end(cmd);
            });

        // 4) Bloom + ACES tone map, resolving HDR into the backbuffer.
        post.addPasses(graph, sceneHdr, backbuffer, frame.width, frame.height);

        graph.execute(*frame.cmd);
        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    device->destroyTexture(white);
    swapchain.reset();
    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
