// The 2D mesh path and the 9-patch, side by side.
//
//   row 1  shapes        circle, hexagon, triangle, convex polygon
//   row 2  arcs          sector, circular segment, stroked arc, spinning ring
//   row 3  vertex colour one mesh, a different colour per vertex — a quad cannot do this
//   row 4  blend modes   the same shape drawn Opaque, Blend and Additive over a backdrop
//   row 5  9-patch       one 32x32 rounded panel stretched to three sizes without
//                        smearing its corners, next to the same sprite scaled naively
//
// Press SPACE to toggle the naive comparison on the 9-patch row.

#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/renderer/mesh2d.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace vortex;
using namespace vortex::renderer;

namespace {

constexpr u32 kPanelSize  = 32;
constexpr f32 kPanelInset = 10.0f;   // the rounded corner is 10px across

// A rounded panel with a bright border: the classic 9-patch subject. Stretched naively
// the corners smear, which is exactly what the 9-patch row is there to show.
std::vector<u8> makePanel() {
    std::vector<u8> px(static_cast<usize>(kPanelSize) * kPanelSize * 4, 0);
    const f32 radius = kPanelInset;

    for (u32 y = 0; y < kPanelSize; ++y)
        for (u32 x = 0; x < kPanelSize; ++x) {
            const f32 fx = static_cast<f32>(x) + 0.5f;
            const f32 fy = static_cast<f32>(y) + 0.5f;

            // Distance to the rounded rectangle's outline: clamp into the inner box, then
            // measure. Negative is inside.
            const f32 cx = std::min(std::max(fx, radius), static_cast<f32>(kPanelSize) - radius);
            const f32 cy = std::min(std::max(fy, radius), static_cast<f32>(kPanelSize) - radius);
            const f32 d  = std::sqrt((fx - cx) * (fx - cx) + (fy - cy) * (fy - cy)) - radius;

            u8* p = &px[(static_cast<usize>(y) * kPanelSize + x) * 4];
            if (d > 0.0f) { p[3] = 0; continue; }             // outside
            const bool border = d > -3.0f;
            p[0] = border ? 120 : 30;
            p[1] = border ? 220 : 40;
            p[2] = border ? 255 : 70;
            p[3] = 255;
        }
    return px;
}

} // namespace

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Mesh2D"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    SpriteBatch    batch(*device, swapchain->format(), 256);
    Mesh2DRenderer meshes(*device, swapchain->format());

    const std::vector<u8> panelPixels = makePanel();
    rhi::TextureHandle texPanel = device->createTexture(
        {.width = kPanelSize, .height = kPanelSize, .debugName = "panel"}, panelPixels.data());

    // --- Shapes ---------------------------------------------------------------
    // Authored through Color, so the hex is the colour you would pick in a design tool and
    // fromRgb decodes it to the linear light the renderer works in. A raw Vec4 here would
    // be taken as linear and come out pastel.
    const Vec4 cyan = Color::fromRgb(0x59D9FF);
    const Vec4 gold = Color::fromRgb(0xFFC74D);
    const Vec4 rose = Color::fromRgb(0xFF598C);

    const Vec2 house[5] = {{-30.0f, -40.0f}, {30.0f, -40.0f}, {45.0f, 10.0f},
                           {0.0f, 45.0f}, {-45.0f, 10.0f}};

    const Mesh2DHandle circle  = meshes.createMesh(makeCircleMesh(45.0f, 48, cyan));
    const Mesh2DHandle hexagon = meshes.createMesh(makeRegularPolygonMesh(6, 45.0f, gold));
    const Mesh2DHandle tri     = meshes.createMesh(makeRegularPolygonMesh(3, 48.0f, rose));
    const Mesh2DHandle poly    = meshes.createMesh(makeConvexPolygonMesh(house, 5, cyan));

    const Mesh2DHandle sector  = meshes.createMesh(makeSectorMesh(45.0f, 0.3f, 2.6f, 32, gold));
    const Mesh2DHandle segment = meshes.createMesh(makeSegmentMesh(45.0f, 0.3f, 2.6f, 32, rose));
    const Mesh2DHandle arc     = meshes.createMesh(makeArcMesh(40.0f, 12.0f, 0.0f, 4.2f, 32, cyan));
    const Mesh2DHandle ring    = meshes.createMesh(makeArcMesh(40.0f, 8.0f, 0.0f, kTwoPi, 48, gold));

    // Vertex colours: one mesh, four corners, four colours. The interpolation across the
    // triangles is free — it is what the vertex format was always able to carry.
    Mesh2DData rainbow = makeRectMesh({100.0f, 100.0f});
    rainbow.vertices[0].color = Color::fromRgb(0xFF3333);
    rainbow.vertices[1].color = Color::fromRgb(0x33FF4D);
    rainbow.vertices[2].color = Color::fromRgb(0x4D66FF);
    rainbow.vertices[3].color = Color::fromRgb(0xFFE633);
    const Mesh2DHandle vcolors = meshes.createMesh(rainbow);

    const Mesh2DHandle blob = meshes.createMesh(
        makeCircleMesh(38.0f, 32, Color::fromRgb(0x80CCFF).withAlpha(0.7f)));
    const Mesh2DHandle backdrop = meshes.createMesh(
        makeRectMesh({420.0f, 110.0f}, Color::fromRgb(0x2E2947)));

    Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);

    const NineSlice slice{.left   = kPanelInset, .top    = kPanelInset,
                          .right  = kPanelInset, .bottom = kPanelInset,
                          .sourcePixels = {static_cast<f32>(kPanelSize),
                                           static_cast<f32>(kPanelSize)}};

    VORTEX_INFO("App", "SPACE toggles naive scaling on the 9-patch row. ESC quits.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64  frameCount = 0;
    f32  spin  = 0.0f;
    bool naive = false;
    int  lastW = fbw, lastH = fbh;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;
        if (input->isKeyPressed(pf::Key::Space)) naive = !naive;

        spin += static_cast<f32>(clock->deltaTime());

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x0A0D17));
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width  = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        const auto at = [](f32 x, f32 y) { return Mat4::translation(x, y, 0.0f); };

        meshes.begin(camera.viewProjection());

        // Row 1 — shapes.
        meshes.submit({.mesh = circle,  .transform = at(-500.0f, 260.0f)});
        meshes.submit({.mesh = hexagon, .transform = at(-360.0f, 260.0f)});
        meshes.submit({.mesh = tri,     .transform = at(-220.0f, 260.0f)});
        meshes.submit({.mesh = poly,    .transform = at(-80.0f,  260.0f)});

        // Row 2 — arcs. The ring spins, to show a mesh under a live transform.
        meshes.submit({.mesh = sector,  .transform = at(-500.0f, 120.0f)});
        meshes.submit({.mesh = segment, .transform = at(-360.0f, 120.0f)});
        meshes.submit({.mesh = arc,     .transform = at(-220.0f, 120.0f)});
        meshes.submit({.mesh = ring,
                       .transform = at(-80.0f, 120.0f) * Mat4::rotationZ(spin)});

        // Row 3 — per-vertex colour.
        meshes.submit({.mesh = vcolors, .transform = at(-500.0f, -40.0f)});

        // Row 4 — blend modes, over a backdrop so the difference is visible.
        meshes.submit({.mesh = backdrop, .transform = at(-160.0f, -40.0f),
                       .blend = BlendMode2D::Opaque, .layer = 0});
        meshes.submit({.mesh = blob, .transform = at(-280.0f, -40.0f),
                       .blend = BlendMode2D::Opaque,   .layer = 1});
        meshes.submit({.mesh = blob, .transform = at(-160.0f, -40.0f),
                       .blend = BlendMode2D::Blend,    .layer = 1});
        meshes.submit({.mesh = blob, .transform = at(-40.0f, -40.0f),
                       .blend = BlendMode2D::Additive, .layer = 1});

        meshes.end(*frame.cmd);

        // Row 5 — the 9-patch, at three sizes. Naive scaling for comparison on SPACE.
        batch.begin(camera.viewProjection());
        const Vec2 sizes[3] = {{120.0f, 90.0f}, {260.0f, 90.0f}, {380.0f, 140.0f}};
        f32 x = -520.0f;
        for (const Vec2 size : sizes) {
            Sprite s;
            s.position = {x + size.x * 0.5f, -230.0f};
            s.size     = size;
            s.texture  = texPanel;
            s.sampler  = SpriteSampler::LinearClamp;
            if (naive) batch.draw(s);                  // stretched: corners smear
            else       batch.drawNineSlice(s, slice);  // sliced: corners keep their size
            x += size.x + 30.0f;
        }
        batch.end(*frame.cmd);

        frame.cmd->endRenderPass();
        device->endFrame();

        ++frameCount;
        if (frameCount % 120 == 0) {
            VORTEX_INFO("App", "%s | mesh draws %u | sprite draws %u (%u quads)",
                        naive ? "NAIVE" : "9-PATCH",
                        meshes.drawCallCount(), batch.drawCallCount(), batch.spriteCount());
        }
    }

    device->waitIdle();
    for (Mesh2DHandle m : {circle, hexagon, tri, poly, sector, segment, arc, ring,
                           vcolors, blob, backdrop})
        meshes.destroyMesh(m);
    batch.releaseTexture(texPanel);
    device->destroyTexture(texPanel);
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
