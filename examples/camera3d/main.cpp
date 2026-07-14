// Cameras: the three things a 3D camera has to do that the mesh3d demo never shows.
//
//   Split screen   — the same world, twice, side by side, from two cameras.
//   Orthographic3D — the right-hand view. Parallel lines stay parallel and distance does not
//                    shrink anything: the CAD / isometric-strategy-game look. It is NOT the 2D
//                    camera with a 3D scene in it; see Camera::Mode.
//   Ray casting    — Camera::viewportToWorld turns the mouse into a world-space ray, and
//                    MeshRenderer::rayCast intersects it with the actual triangles. The shape
//                    under the cursor lights up, and it is exact — not a bounding-box guess.
//
// A note on why there are TWO MeshRenderers rather than one used twice:
//
// MeshRenderer::begin() rotates a frame-in-flight slot on every call, so calling it twice in one
// device frame would write both slots in that frame — and the GPU may still be reading them from
// the previous one. One renderer per view sidesteps that. It is not free (each carries its own
// pipelines and IBL cubemaps), and a renderer that could serve several views from one frame's
// resources would be the better fix.

#include "vortex/core/log.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/post_process.hpp"
#include "vortex/renderer/render_graph.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace vortex;

int main() {
    auto window    = pf::createWindow({.width = 1400, .height = 720, .title = "Vortex Cameras"});
    auto input     = pf::createInputProvider(*window);
    auto clock     = pf::createClock();
    auto device    = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    const rhi::Format swapFormat  = swapchain->format();
    const rhi::Format depthFormat = rhi::Format::D32_SFLOAT;
    const rhi::Format hdrFormat   = rhi::Format::R16G16B16A16_SFLOAT;

    renderer::MeshRenderer left(*device, hdrFormat, depthFormat);
    renderer::MeshRenderer right(*device, hdrFormat, depthFormat);
    renderer::PostProcess  post(*device, hdrFormat, swapFormat);
    renderer::RenderGraph  graph(*device);

    // The same meshes in both renderers: a handle belongs to the renderer that made it.
    struct Shapes { renderer::MeshHandle floor, cube, sphere, torus, cone; };
    auto build = [](renderer::MeshRenderer& r) {
        return Shapes{r.createMesh(renderer::makePlane(14.0f)),
                      r.createMesh(renderer::makeCube(1.0f)),
                      r.createMesh(renderer::makeSphere(20, 28, 0.55f)),
                      r.createMesh(renderer::makeTorus(0.55f, 0.2f)),
                      r.createMesh(renderer::makeCone(0.55f, 1.1f))};
    };
    const Shapes ls = build(left);
    const Shapes rs = build(right);

    // The scene, described once. Both views draw it; picking runs against the left one.
    struct Prop { int shape; Vec3 position; Vec4 color; };
    const Prop props[] = {
        {1, {-2.2f, 0.0f,  0.6f}, {0.85f, 0.35f, 0.30f, 1.0f}},
        {2, {-0.6f, 0.05f, -1.4f}, {0.35f, 0.65f, 0.90f, 1.0f}},
        {3, { 1.1f, 0.25f,  0.9f}, {0.95f, 0.75f, 0.30f, 1.0f}},
        {4, { 2.6f, 0.05f, -0.8f}, {0.45f, 0.80f, 0.50f, 1.0f}},
        {1, { 0.4f, 0.0f,   2.2f}, {0.70f, 0.45f, 0.85f, 1.0f}},
    };
    constexpr usize kPropCount = sizeof(props) / sizeof(props[0]);

    auto meshOf = [](const Shapes& s, int i) {
        switch (i) {
            case 1:  return s.cube;
            case 2:  return s.sphere;
            case 3:  return s.torus;
            default: return s.cone;
        }
    };

    renderer::Camera persp;
    persp.mode        = renderer::Camera::Mode::Perspective;
    persp.fovYRadians = 1.0f;
    persp.nearZ = 0.1f;
    persp.farZ  = 100.0f;
    persp.target = {0.0f, 0.4f, 0.0f};

    // Orthographic3D: a lookAt camera with a BOX frustum. `orthoHeight` is the half-height of
    // that box in world units — the zoom. There is no perspective divide, so the far cube is
    // exactly as big as the near one, which is the entire point of the mode.
    renderer::Camera ortho;
    ortho.mode        = renderer::Camera::Mode::Orthographic3D;
    ortho.orthoHeight = 3.4f;
    ortho.nearZ = 0.1f;
    ortho.farZ  = 100.0f;
    ortho.target = {0.0f, 0.4f, 0.0f};
    // The classic isometric station: equal on all three axes.
    ortho.position = {6.0f, 6.0f, 6.0f};

    const char* shotPath     = std::getenv("VORTEX_SCREENSHOT");
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64   maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;

    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f32 t = 0.0f;
    i32 hovered = -1;

    std::vector<renderer::MeshInstance> scene;

    VORTEX_INFO("App", "Left: perspective. Right: Orthographic3D (isometric).");
    VORTEX_INFO("App", "Move the mouse over the LEFT half — the shape under the cursor lights up.");

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        const bool deterministic = shotPath != nullptr || maxFrames != 0;
        const f32  dt = deterministic ? 1.0f / 60.0f
                                      : static_cast<f32>(clock->deltaTime());
        t += dt;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        if (w == 0 || h == 0) continue;

        const u32 halfW = static_cast<u32>(w) / 2u;

        persp.position = {std::sin(t * 0.25f) * 6.5f, 3.0f, std::cos(t * 0.25f) * 6.5f};
        persp.aspect   = static_cast<f32>(halfW) / static_cast<f32>(h);
        ortho.aspect   = static_cast<f32>(halfW) / static_cast<f32>(h);

        renderer::SceneLighting sun;
        sun.sun.direction = {-0.45f, -1.0f, -0.35f};
        sun.sun.intensity = 2.6f;
        sun.shadow.cascadeCount = 2;
        sun.shadow.maxDistance  = 25.0f;
        sun.shadow.resolution   = 2048;

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        // --- Picking, against the LEFT viewport ---------------------------
        //
        // The camera's viewport is the LEFT HALF of the window, so the ray must be built from a
        // camera whose viewport says so. Hand it the full window size and every pick is wrong by
        // a factor of two in x — silently, because the ray still hits *something*.
        persp.viewportWidth  = static_cast<f32>(halfW);
        persp.viewportHeight = static_cast<f32>(h);

        f32 mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);

        // Build the scene once; both views and the ray cast all read the same list.
        auto fill = [&](const Shapes& s) {
            scene.clear();
            scene.push_back({.mesh = s.floor, .model = Mat4::translation(0.0f, -0.5f, 0.0f),
                             .color = {0.42f, 0.44f, 0.48f, 1.0f},
                             .metallic = 0.0f, .roughness = 0.9f});
            for (usize i = 0; i < kPropCount; ++i) {
                const Prop& p = props[i];
                Vec4 c = p.color;
                if (static_cast<i32>(i) == hovered)
                    c = {1.0f, 0.85f, 0.35f, 1.0f};   // the pick, made obvious
                scene.push_back({.mesh  = meshOf(s, p.shape),
                                 .model = Mat4::translation(p.position.x, p.position.y,
                                                            p.position.z) *
                                          Mat4::rotationY(t * 0.6f + static_cast<f32>(i)),
                                 .color = c,
                                 .metallic = 0.1f, .roughness = 0.45f});
            }
        };

        // The pick has to run against the instances the renderer holds, so submit first, cast,
        // then re-submit with the highlight. Cheap: it is the same vector either way.
        left.begin(persp, sun);
        fill(ls);
        left.submit(scene.data(), scene.size());

        // A deterministic self-check: with no mouse to move (a headless capture), fire a ray at
        // the centre of the left viewport and say what it hit. A pick that reports nothing, or
        // reports the floor when a shape is plainly in the way, is a broken pick.
        if (deterministic && frameCount == 25) {
            const renderer::Ray probe = persp.viewportToWorld(static_cast<f32>(halfW) * 0.5f,
                                                              static_cast<f32>(h) * 0.5f);
            const renderer::RayHit hit = left.rayCast(probe);
            if (hit)
                VORTEX_INFO("Pick", "centre ray -> instance %zu at (%.2f, %.2f, %.2f), %.2fm away",
                            hit.instance, hit.point.x, hit.point.y, hit.point.z, hit.distance);
            else
                VORTEX_INFO("Pick", "centre ray -> nothing");
        }

        hovered = -1;
        if (mx >= 0.0f && mx < static_cast<f32>(halfW)) {
            const renderer::Ray ray = persp.viewportToWorld(mx, my);
            if (const renderer::RayHit hit = left.rayCast(ray)) {
                // instance 0 is the floor; the props follow it.
                if (hit.instance > 0) hovered = static_cast<i32>(hit.instance) - 1;
            }
        }

        // Re-submit with the highlight applied. begin() cleared the queue, so refill it.
        left.begin(persp, sun);
        fill(ls);
        left.submit(scene.data(), scene.size());

        right.begin(ortho, sun);
        fill(rs);
        right.submit(scene.data(), scene.size());

        graph.beginFrame();
        const auto backbuffer = graph.importBackbuffer(frame.backbuffer, frame.width, frame.height);
        const auto sceneHdr   = graph.colorTarget("scene_hdr", frame.width, frame.height, hdrFormat);
        const auto sceneDepth = graph.depthTarget("scene_depth", frame.width, frame.height);
        const auto shadowL    = graph.depthTarget("shadow_l", 2048, 2048, true);
        const auto shadowR    = graph.depthTarget("shadow_r", 2048, 2048, true);

        const f32 clear[4] = {0.03f, 0.04f, 0.06f, 1.0f};

        graph.addPass("shadow_l",
            [&](renderer::RenderGraph::PassBuilder& b) { b.writeDepth(shadowL); },
            [&](rhi::ICommandList& cmd) { left.renderShadow(cmd); });
        graph.addPass("shadow_r",
            [&](renderer::RenderGraph::PassBuilder& b) { b.writeDepth(shadowR); },
            [&](rhi::ICommandList& cmd) { right.renderShadow(cmd); });

        left.setShadowMap(graph.texture(shadowL));
        right.setShadowMap(graph.texture(shadowR));

        // Two views into ONE target: the split is a viewport, not a second window. Each pass
        // scissors itself to its half, so neither can paint over the other.
        graph.addPass("view_left",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.sample(shadowL);
                b.writeColor(sceneHdr, clear);
                b.writeDepth(sceneDepth);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport({.x = 0.0f, .y = 0.0f,
                                 .width = static_cast<f32>(halfW),
                                 .height = static_cast<f32>(frame.height)});
                cmd.setScissor(0, 0, halfW, frame.height);
                left.renderSkybox(cmd);
                left.end(cmd);
            });

        graph.addPass("view_right",
            [&](renderer::RenderGraph::PassBuilder& b) {
                b.sample(shadowR);
                b.writeColor(sceneHdr, clear, rhi::LoadOp::Load);
                b.writeDepth(sceneDepth, 1.0f, rhi::LoadOp::Clear);
            },
            [&](rhi::ICommandList& cmd) {
                cmd.setViewport({.x = static_cast<f32>(halfW), .y = 0.0f,
                                 .width = static_cast<f32>(frame.width - halfW),
                                 .height = static_cast<f32>(frame.height)});
                cmd.setScissor(halfW, 0, frame.width - halfW, frame.height);
                right.renderSkybox(cmd);
                right.end(cmd);
            });

        post.addPasses(graph, sceneHdr, backbuffer, frame.width, frame.height);

        graph.execute(*frame.cmd);
        device->endFrame();
        ++frameCount;

        if (shotPath != nullptr && frameCount == 30) {
            std::vector<u8> px(static_cast<usize>(frame.width) * frame.height * 4);
            device->readTexture(frame.backbuffer, px.data());
            if (std::FILE* f = std::fopen(shotPath, "wb")) {
                std::fprintf(f, "P6\n%u %u\n255\n", frame.width, frame.height);
                for (usize i = 0; i < px.size(); i += 4) {
                    const u8 rgb[3] = {px[i + 2], px[i + 1], px[i]};   // backbuffer is BGRA
                    std::fwrite(rgb, 1, 3, f);
                }
                std::fclose(f);
                VORTEX_INFO("App", "Wrote screenshot: %s", shotPath);
            }
            break;
        }
    }

    device->waitIdle();
    swapchain.reset();
    return 0;
}
