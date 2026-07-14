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

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    const renderer::MeshHandle paneMesh  = mesh.createMesh(renderer::makeQuad());
    const renderer::MeshHandle cylMesh   = mesh.createMesh(renderer::makeCylinder());
    const renderer::MeshHandle coneMesh  = mesh.createMesh(renderer::makeCone());
    const renderer::MeshHandle torusMesh = mesh.createMesh(renderer::makeTorus());
    const renderer::MeshHandle capMesh   = mesh.createMesh(renderer::makeCapsule());

    rhi::TextureHandle white = device->createTexture(
        {.width = 1, .height = 1}, std::vector<u8>{255, 255, 255, 255}.data());

    // A brick normal map whose ALPHA is the height field — that alpha is what parallax
    // occlusion mapping marches through. UNORM, not SRGB: these are directions and
    // depths, not colour, and an sRGB decode would bend them.
    constexpr u32 kTex = 256;
    std::vector<u8> brick(static_cast<usize>(kTex) * kTex * 4);
    std::vector<u8> brickAlbedo(static_cast<usize>(kTex) * kTex * 4);
    for (u32 y = 0; y < kTex; ++y) {
        for (u32 x = 0; x < kTex; ++x) {
            // Running bond: every other course is offset by half a brick.
            const u32 row    = y / 64;
            const u32 shift  = (row % 2) ? 64 : 0;
            const u32 bx     = (x + shift) % 128;
            const u32 by     = y % 64;
            // Distance into the mortar groove, 0 at the groove, 1 on the brick face.
            const f32 mx = std::min<f32>(std::min<f32>(bx, 128 - 1 - bx) / 8.0f, 1.0f);
            const f32 my = std::min<f32>(std::min<f32>(by, 64 - 1 - by) / 8.0f, 1.0f);
            const f32 h  = std::min(mx, my);            // 1 = brick face, 0 = mortar

            // A normal derived from the height's slope, in tangent space.
            const f32 nx = (bx < 8) ? -0.6f : (bx > 128 - 9 ? 0.6f : 0.0f);
            const f32 ny = (by < 8) ? -0.6f : (by > 64 - 9 ? 0.6f : 0.0f);
            const f32 nz = 1.0f;
            const f32 len = std::sqrt(nx * nx + ny * ny + nz * nz);

            const usize i = (static_cast<usize>(y) * kTex + x) * 4;
            brick[i + 0] = static_cast<u8>((nx / len * 0.5f + 0.5f) * 255.0f);
            brick[i + 1] = static_cast<u8>((ny / len * 0.5f + 0.5f) * 255.0f);
            brick[i + 2] = static_cast<u8>((nz / len * 0.5f + 0.5f) * 255.0f);
            brick[i + 3] = static_cast<u8>(h * 255.0f);   // the height field

            const f32 tint = 0.55f + 0.45f * h;
            brickAlbedo[i + 0] = static_cast<u8>(200.0f * tint);
            brickAlbedo[i + 1] = static_cast<u8>(110.0f * tint);
            brickAlbedo[i + 2] = static_cast<u8>(90.0f * tint);
            brickAlbedo[i + 3] = 255;
        }
    }
    rhi::TextureHandle brickNormal = device->createTexture(
        {.width = kTex, .height = kTex, .format = rhi::Format::R8G8B8A8_UNORM,
         .debugName = "brick_normal_height"}, brick.data());
    rhi::TextureHandle brickAlb = device->createTexture(
        {.width = kTex, .height = kTex, .debugName = "brick_albedo"}, brickAlbedo.data());

    // A scorch mark: dark in the middle, fading to fully transparent at the rim. The alpha
    // is what makes it a decal rather than a sticker — the edges have to vanish.
    constexpr u32 kDecalTex = 128;
    std::vector<u8> scorch(static_cast<usize>(kDecalTex) * kDecalTex * 4);
    for (u32 y = 0; y < kDecalTex; ++y) {
        for (u32 x = 0; x < kDecalTex; ++x) {
            const f32 dx = (static_cast<f32>(x) / kDecalTex - 0.5f) * 2.0f;
            const f32 dy = (static_cast<f32>(y) / kDecalTex - 0.5f) * 2.0f;
            const f32 r  = std::sqrt(dx * dx + dy * dy);
            // A ragged edge, so it does not read as a perfect circle.
            const f32 wobble = 0.82f + 0.12f * std::sin(std::atan2(dy, dx) * 7.0f);
            const f32 a = std::clamp((wobble - r) / 0.35f, 0.0f, 1.0f);

            const usize i = (static_cast<usize>(y) * kDecalTex + x) * 4;
            scorch[i + 0] = 30;
            scorch[i + 1] = 22;
            scorch[i + 2] = 18;
            scorch[i + 3] = static_cast<u8>(a * 235.0f);
        }
    }
    rhi::TextureHandle scorchTex = device->createTexture(
        {.width = kDecalTex, .height = kDecalTex, .debugName = "decal_scorch"}, scorch.data());

    // --- Scene: a ground plane + a ring of spinning cubes + a centre sphere. ---
    ecs::Registry reg;

    // The floor: a brick material whose relief is real enough to occlude itself.
    const renderer::MaterialHandle brickMat = mesh.createMaterial({
        .albedo = brickAlb, .normalMap = brickNormal,
        .metallic = 0.0f, .roughness = 0.85f,
        .uvScale = 6.0f,
        .parallaxScale = 0.05f, .parallaxLayers = 32});

    const ecs::Entity ground = reg.create();
    reg.emplace<ecs::Transform3D>(ground, ecs::Transform3D{.position = {0.0f, -1.0f, 0.0f}});
    reg.emplace<ecs::MeshComp>(ground, ecs::MeshComp{.mesh = planeMesh, .material = brickMat});

    // A polished gold sphere: metallic with low roughness shows off PBR + bloom.
    const ecs::Entity centre = reg.create();
    reg.emplace<ecs::Transform3D>(centre, ecs::Transform3D{});
    reg.emplace<ecs::MeshComp>(centre, ecs::MeshComp{.mesh = ballMesh,
                                                     .color = {1.0f, 0.78f, 0.34f, 1.0f},
                                                     .metallic = 1.0f, .roughness = 0.18f});

    // A pane of glass in front of the sphere: alpha-blended and double-sided, so it
    // takes the forward path even when the rest of the scene is deferred.
    const renderer::MaterialHandle glass = mesh.createMaterial({
        .baseColor = {0.75f, 0.92f, 0.98f, 0.5f},
        .metallic = 0.0f, .roughness = 0.05f,
        .transmission = 0.9f, .ior = 1.5f,
        .blend = rhi::BlendMode::Alpha, .doubleSided = true});

    const ecs::Entity pane = reg.create();
    reg.emplace<ecs::Transform3D>(pane, ecs::Transform3D{.position = {-3.6f, 0.6f, 1.6f},
                                                         .scale = {2.4f, 2.4f, 1.0f}});
    reg.emplace<ecs::MeshComp>(pane, ecs::MeshComp{.mesh = paneMesh, .material = glass,
                                                   .castsShadow = false});

    constexpr int kCubeCount = 8;
    std::vector<ecs::Entity> cubes;
    for (int i = 0; i < kCubeCount; ++i) {
        const f32 a = 6.2831853f * static_cast<f32>(i) / kCubeCount;
        const f32 t = static_cast<f32>(i) / kCubeCount;
        const ecs::Entity e = reg.create();
        reg.emplace<ecs::Transform3D>(e, ecs::Transform3D{
            .position = {std::cos(a) * 3.2f, 0.0f, std::sin(a) * 3.2f}});
        // One of each built-in shape around the ring — if a primitive's triangles were
        // wound inside-out, it would light up wrong here and nowhere else.
        const renderer::MeshHandle shapes[kCubeCount] = {
            cubeMesh, cylMesh, coneMesh, torusMesh, capMesh, cubeMesh, cylMesh, torusMesh};
        reg.emplace<ecs::MeshComp>(e, ecs::MeshComp{
            .mesh = shapes[i], .color = {0.3f + 0.6f * t, 0.5f, 1.0f - 0.5f * t, 1.0f},
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

    // Same scene, two lighting architectures. VORTEX_DEFERRED=1 fills a G-buffer and
    // lights each screen pixel once; the default forward path lights each fragment
    // that gets drawn. The image should be indistinguishable.
    const char* deferredEnv = std::getenv("VORTEX_DEFERRED");
    const bool  deferred = deferredEnv != nullptr && deferredEnv[0] == '1';
    VORTEX_INFO("App", "Lighting path: %s", deferred ? "deferred" : "forward");

    // SSAO needs a G-buffer, so it only exists on the deferred path.
    const bool ssao       = deferred && std::getenv("VORTEX_SSAO") != nullptr;
    // Motion blur reads the G-buffer's velocity target, so it is deferred-only.
    const bool motionBlur = deferred && std::getenv("VORTEX_MOTION_BLUR") != nullptr;
    const bool contact    = deferred && std::getenv("VORTEX_CONTACT") != nullptr;
    const bool decals     = deferred && std::getenv("VORTEX_DECALS") != nullptr;
    const bool volumetric = deferred && std::getenv("VORTEX_VOLUMETRIC") != nullptr;
    VORTEX_INFO("App", "SSAO: %s, motion blur: %s, contact shadows: %s, volumetric: %s",
                ssao ? "on" : "off", motionBlur ? "on" : "off",
                contact ? "on" : "off", volumetric ? "on" : "off");

    const char* shotPath     = std::getenv("VORTEX_SCREENSHOT");
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

        // When capturing, step time in fixed increments instead of by the wall clock.
        // Otherwise two runs reach frame 30 at slightly different `t`, the sun and camera
        // land in different places, and an A/B image diff measures that instead of the
        // feature under test.
        const f32 dt = shotPath != nullptr ? 1.0f / 60.0f
                                           : static_cast<f32>(clock->deltaTime());
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
        // VORTEX_STATIC_CAM pins the camera. With it, anything that still blurs can only
        // be blurring because it moved on its own — which is the only way to tell object
        // motion blur apart from the camera's.
        cam.position = std::getenv("VORTEX_STATIC_CAM")
                     ? Vec3{0.0f, 3.0f, 7.0f}
                     : Vec3{std::sin(t * 0.3f) * 7.0f, 3.0f, std::cos(t * 0.3f) * 7.0f};

        renderer::SceneLighting scene;
        scene.sun.direction = {std::cos(t * 0.7f), -0.8f, std::sin(t * 0.7f)};
        scene.sun.intensity = 3.0f;             // HDR: bright enough for specular to bloom
        // Four cascades fitted to the camera's own frustum every frame — no region of
        // the world has to be nominated in advance.
        scene.shadow.cascadeCount = 4;
        scene.shadow.maxDistance  = 40.0f;
        scene.shadow.resolution   = kShadowRes;

        // Two coloured point lights orbiting the other way, and a spot from above:
        // the sun alone cannot show off multi-light shading or cone falloff.
        scene.lights.push_back({.type = renderer::LightType::Point,
                                .position = {std::cos(-t) * 4.0f, 1.2f, std::sin(-t) * 4.0f},
                                .color = {1.0f, 0.35f, 0.15f},
                                .intensity = 12.0f, .range = 9.0f, .radius = 0.3f});
        scene.lights.push_back({.type = renderer::LightType::Point,
                                .position = {std::cos(-t + 3.14f) * 4.0f, 1.2f,
                                             std::sin(-t + 3.14f) * 4.0f},
                                .color = {0.2f, 0.5f, 1.0f},
                                .intensity = 12.0f, .range = 9.0f, .radius = 0.3f});
        scene.lights.push_back({.type = renderer::LightType::Spot,
                                .position = {0.0f, 6.0f, 0.0f},
                                .direction = {0.0f, -1.0f, 0.0f},
                                .color = {1.0f, 0.95f, 0.8f},
                                .intensity = 40.0f, .range = 14.0f,
                                .innerAngle = 0.18f, .outerAngle = 0.34f});

        // Exponential fog tinted like the sky, thinning out with height.
        scene.fog.mode          = renderer::Fog::Mode::Exp;
        scene.fog.color         = {0.42f, 0.50f, 0.62f};
        scene.fog.density       = 0.012f;
        scene.fog.heightFalloff = 0.10f;

        scene.contactShadows.enabled = contact;

        scene.volumetric.enabled  = volumetric;
        scene.volumetric.density  = 0.05f;
        scene.volumetric.maxDistance = 30.0f;

        scene.ssao.enabled   = ssao;
        scene.ssao.radius    = 0.6f;
        scene.ssao.intensity = 1.0f;
        scene.ssao.power     = 1.6f;

        // Spin the ring cubes and the centre sphere via their ECS transforms.
        for (int i = 0; i < kCubeCount; ++i)
            reg.get<ecs::Transform3D>(cubes[i]).rotation =
                Quat::fromAxisAngle({0.3f, 1.0f, 0.2f}, t * 1.5f + static_cast<f32>(i));
        reg.get<ecs::Transform3D>(centre).rotation = Quat::fromAxisAngle({0, 1, 0}, t * 0.6f);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        instances.clear();
        ecs::extractMeshes(reg, instances);
        mesh.begin(cam, scene);
        mesh.submit(instances.data(), instances.size());

        // Three scorch marks stamped onto the floor. Nothing is added to the scene: each
        // is a box, and whatever surface the G-buffer holds inside it takes the texture.
        if (decals) {
            const Vec3 spots[3] = {{-2.0f, -1.0f, 1.5f}, {1.8f, -1.0f, -0.5f},
                                   {0.2f, -1.0f, 3.0f}};
            const f32  sizes[3] = {3.0f, 2.2f, 2.6f};
            for (int i = 0; i < 3; ++i)
                mesh.submitDecal({.model = Mat4::translation(spots[i].x, spots[i].y, spots[i].z) *
                                           Mat4::rotationY(0.7f * static_cast<f32>(i)) *
                                           // A shallow box: it should catch the floor and
                                           // nothing else. Make it tall and it engulfs the
                                           // objects standing on the floor and projects onto
                                           // them too, which is correct but rarely wanted.
                                           Mat4::scaling(sizes[i], 0.6f, sizes[i]),
                                  .texture = scorchTex,
                                  .opacity = 0.9f});
        }

        // Pick whatever is under the cursor and report it: this is Camera::viewportToWorld
        // feeding MeshRenderer::rayCast, the same pair a click-to-select tool needs.
        f32 mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);
        cam.viewportWidth  = static_cast<f32>(w);
        cam.viewportHeight = static_cast<f32>(h);
        if (const renderer::RayHit hit = mesh.rayCast(cam.viewportToWorld(mx, my))) {
            if (frameCount % 60 == 0)
                VORTEX_INFO("Pick", "instance %zu at %.2f, %.2f, %.2f (%.2fm away)",
                            hit.instance, hit.point.x, hit.point.y, hit.point.z, hit.distance);
        }

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
            [&](rhi::ICommandList& cmd) { mesh.renderShadow(cmd); });

        // The lit pass reads shadows from this depth target; the skybox shares the set.
        mesh.setShadowMap(graph.texture(shadowMap));

        // Motion blur needs the scene's depth, and SSAO needs the G-buffer's — which
        // target that is depends on the path taken below.
        auto velocityTarget = renderer::RenderGraph::kInvalid;
        auto aoTarget     = renderer::RenderGraph::kInvalid;

        if (!deferred) {
            // 2) Forward: skybox, then the lit scene into the HDR target. Lighting is
            // paid per fragment drawn.
            graph.addPass("mesh",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.sample(shadowMap);
                    b.writeColor(sceneHdr, clear);
                    b.writeDepth(sceneDepth);
                },
                [&](rhi::ICommandList& cmd) {
                    cmd.setViewport(vp);
                    cmd.setScissor(0, 0, frame.width, frame.height);
                    mesh.renderSkybox(cmd);
                    mesh.end(cmd);
                });
        } else {
            // 2a) Deferred: write the surfaces into a G-buffer (three targets in one
            // pass), with no lighting at all.
            const auto gAlbedo = graph.colorTarget("g_albedo", frame.width, frame.height,
                                                   renderer::MeshRenderer::kGBufferAlbedoFormat);
            const auto gNormal = graph.colorTarget("g_normal", frame.width, frame.height,
                                                   renderer::MeshRenderer::kGBufferNormalFormat);
            const auto gEmissive = graph.colorTarget("g_emissive", frame.width, frame.height,
                                                     renderer::MeshRenderer::kGBufferEmissiveFormat);
            const auto gVelocity = graph.colorTarget("g_velocity", frame.width, frame.height,
                                                     renderer::MeshRenderer::kGBufferVelocityFormat);
            const auto gDepth = graph.depthTarget("g_depth", frame.width, frame.height,
                                                  /*sampled=*/true);

            const f32 zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            graph.addPass("gbuffer",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.writeColor(gAlbedo, zero);      // attachment 0
                    b.writeColor(gNormal, zero);      // attachment 1
                    b.writeColor(gEmissive, zero);    // attachment 2
                    b.writeColor(gVelocity, zero);    // attachment 3
                    b.writeDepth(gDepth);
                },
                [&](rhi::ICommandList& cmd) {
                    cmd.setViewport(vp);
                    cmd.setScissor(0, 0, frame.width, frame.height);
                    mesh.renderGBuffer(cmd);
                });

            mesh.setGBuffer(graph.texture(gAlbedo), graph.texture(gNormal),
                            graph.texture(gEmissive), graph.texture(gDepth));

            // Decals are stamped into the albedo before any lighting runs, so they are lit
            // exactly like the surface they landed on.
            if (decals)
                graph.addPass("decals",
                    [&](renderer::RenderGraph::PassBuilder& b) {
                        b.sample(gDepth);
                        b.writeColor(gAlbedo, zero, rhi::LoadOp::Load);
                    },
                    [&](rhi::ICommandList& cmd) {
                        cmd.setViewport(vp);
                        cmd.setScissor(0, 0, frame.width, frame.height);
                        mesh.renderDecals(cmd);
                    });

            // 2b) Ambient occlusion from the depth and normals we just wrote, then a
            // blur to turn the sampling noise into shading.
            if (ssao) {
                const auto aoRaw = graph.colorTarget("ssao_raw", frame.width, frame.height,
                                                     renderer::MeshRenderer::kSsaoFormat);
                const auto aoBlur = graph.colorTarget("ssao_blur", frame.width, frame.height,
                                                      renderer::MeshRenderer::kSsaoFormat);
                const f32 one[4] = {1.0f, 1.0f, 1.0f, 1.0f};

                graph.addPass("ssao",
                    [&](renderer::RenderGraph::PassBuilder& b) {
                        b.sample(gNormal); b.sample(gDepth);
                        b.writeColor(aoRaw, one);
                    },
                    [&](rhi::ICommandList& cmd) {
                        cmd.setViewport(vp);
                        cmd.setScissor(0, 0, frame.width, frame.height);
                        mesh.renderSSAO(cmd);
                    });

                graph.addPass("ssao_blur",
                    [&](renderer::RenderGraph::PassBuilder& b) {
                        b.sample(aoRaw);
                        b.writeColor(aoBlur, one);
                    },
                    [&, aoRaw](rhi::ICommandList& cmd) {
                        cmd.setViewport(vp);
                        cmd.setScissor(0, 0, frame.width, frame.height);
                        mesh.renderSSAOBlur(cmd, graph.sampledBindGroup(aoRaw),
                                            frame.width, frame.height);
                    });

                mesh.setAmbientOcclusion(graph.texture(aoBlur));
                aoTarget = aoBlur;
            }

            velocityTarget = gVelocity;

            // 2c) Light every pixel once, and paint the sky where nothing was drawn.
            graph.addPass("deferred_lighting",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.sample(gAlbedo); b.sample(gNormal); b.sample(gEmissive);
                    b.sample(gDepth);  b.sample(shadowMap);
                    if (aoTarget != renderer::RenderGraph::kInvalid) b.sample(aoTarget);
                    b.writeColor(sceneHdr, clear);
                },
                [&](rhi::ICommandList& cmd) {
                    cmd.setViewport(vp);
                    cmd.setScissor(0, 0, frame.width, frame.height);
                    mesh.renderDeferredLighting(cmd);
                });

            // Light scattering in the air, marched against the shadow map. Additive.
            if (volumetric)
                graph.addPass("volumetric",
                    [&](renderer::RenderGraph::PassBuilder& b) {
                        b.sample(gDepth); b.sample(shadowMap);
                        b.writeColor(sceneHdr, clear, rhi::LoadOp::Load);
                    },
                    [&](rhi::ICommandList& cmd) {
                        cmd.setViewport(vp);
                        cmd.setScissor(0, 0, frame.width, frame.height);
                        mesh.renderVolumetric(cmd);
                    });

            // The glass refracts what is behind it, and it cannot read the target it is
            // about to draw into — so the lit scene is copied out first.
            const auto sceneCopy = graph.colorTarget("scene_copy", frame.width, frame.height,
                                                     hdrFormat);
            graph.addPass("scene_copy",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.sample(sceneHdr);
                    b.writeColor(sceneCopy, clear);
                },
                [&, sceneHdr](rhi::ICommandList& cmd) {
                    cmd.setViewport(vp);
                    cmd.setScissor(0, 0, frame.width, frame.height);
                    mesh.renderSceneCopy(cmd, graph.sampledBindGroup(sceneHdr));
                });
            mesh.setSceneColor(graph.texture(sceneCopy));

            // 2d) Blended surfaces cannot live in a G-buffer, so they take the forward
            // path — over the lit image, against the depth the G-buffer left behind.
            graph.addPass("transparent",
                [&](renderer::RenderGraph::PassBuilder& b) {
                    b.sample(shadowMap);
                    b.sample(sceneCopy);
                    b.writeColor(sceneHdr, clear, rhi::LoadOp::Load);
                    b.writeDepth(gDepth, 1.0f, rhi::LoadOp::Load);
                },
                [&](rhi::ICommandList& cmd) {
                    cmd.setViewport(vp);
                    cmd.setScissor(0, 0, frame.width, frame.height);
                    mesh.endTransparent(cmd);
                });
        }

        // 3) Motion blur, before the overlay: the HUD is screen-space and must not smear
        // with the camera. It returns a new target, since a pass cannot read and write
        // the same texture.
        auto lit = sceneHdr;
        if (motionBlur && velocityTarget != renderer::RenderGraph::kInvalid)
            lit = post.addMotionBlur(graph, sceneHdr, velocityTarget,
                                     frame.width, frame.height, 1.0f, 12);

        // 4) 2D overlay on top of the scene (still HDR).
        graph.addPass("overlay",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.writeColor(lit, clear, rhi::LoadOp::Load);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport(vp);
                cmd.setScissor(0, 0, frame.width, frame.height);
                batch.end(cmd);
            });

        // 5) Bloom + tone map + grading, resolving HDR into the backbuffer.
        post.addPasses(graph, lit, backbuffer, frame.width, frame.height);

        graph.execute(*frame.cmd);
        device->endFrame();
        ++frameCount;

        // VORTEX_SCREENSHOT=<path> dumps the presented frame to a binary PPM once the
        // scene has settled. This is how a change to the lighting path gets checked
        // against pixels rather than against the absence of a crash.
        if (shotPath != nullptr && frameCount == 30) {
            std::vector<u8> px(static_cast<usize>(frame.width) * frame.height * 4);
            device->readTexture(frame.backbuffer, px.data());

            std::FILE* f = std::fopen(shotPath, "wb");
            if (f != nullptr) {
                std::fprintf(f, "P6\n%u %u\n255\n", frame.width, frame.height);
                for (usize i = 0; i < px.size(); i += 4) {
                    // The backbuffer is BGRA; PPM wants RGB.
                    const u8 rgb[3] = {px[i + 2], px[i + 1], px[i]};
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
                VORTEX_INFO("App", "Wrote screenshot: %s", shotPath);
            }
            break;
        }
    }

    device->waitIdle();
    device->destroyTexture(white);
    swapchain.reset();
    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
