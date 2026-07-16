// Animated UI, easing curves, and colour spaces — the same anim::Curve that poses a skeleton,
// pointed at a rectangle instead.
//
// That is the claim worth checking: nothing here is a "UI animation system". A Curve<f32> does
// not know or care whether the float it produces is a joint's rotation, a panel's opacity or a
// camera's field of view, and needing a second system for each of those is how an engine ends up
// with four of them that disagree about what "ease-in" means.
//
// Three rows:
//   1. Every easing curve in the engine, each dropping a marker down its own track. The shape of
//      the curve is drawn behind it, so the marker and its graph can be compared directly.
//   2. Panels whose position, size and opacity are keyframed — including a Catmull-Rom path,
//      which is smooth through its waypoints where a linear one has a corner at each.
//   3. The same two colours interpolated in four colour spaces. They disagree, and that
//      disagreement is the entire point of choosing one.

#include "vortex/anim/curve.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/color.hpp"
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
#include <vector>

using namespace vortex;

namespace {

struct Named {
    easing::Ease curve;
    const char*  name;
};

const Named kCurves[] = {
    {easing::Ease::Linear,      "Linear"},
    {easing::Ease::InQuad,      "InQuad"},
    {easing::Ease::OutQuad,     "OutQuad"},
    {easing::Ease::InOutQuad,   "InOutQuad"},
    {easing::Ease::InCubic,     "InCubic"},
    {easing::Ease::OutCubic,    "OutCubic"},
    {easing::Ease::InOutCubic,  "InOutCubic"},
    {easing::Ease::InSine,      "InSine"},
    {easing::Ease::OutSine,     "OutSine"},
    {easing::Ease::InExpo,      "InExpo"},
    {easing::Ease::OutExpo,     "OutExpo"},
    {easing::Ease::InCirc,      "InCirc"},
    {easing::Ease::OutCirc,     "OutCirc"},
    {easing::Ease::InBack,      "InBack"},
    {easing::Ease::OutBack,     "OutBack"},
    {easing::Ease::OutElastic,  "OutElastic"},
    {easing::Ease::OutBounce,   "OutBounce"},
    {easing::Ease::InOutBounce, "InOutBounce"},
};
constexpr usize kCurveCount = sizeof(kCurves) / sizeof(kCurves[0]);

} // namespace

int main() {
    auto window = pf::createWindow({.width = 1400, .height = 800, .title = "Vortex Animated UI"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    renderer::SpriteBatch batch(*device, swapchain->format(), 16384);

    const std::string fontPath = text::Font::defaultPath(*fs);
    if (fontPath.empty()) {
        VORTEX_ERROR("App", "No usable system font found; set VORTEX_FONT_PATH.");
        return 1;
    }
    auto title = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 34.0f);
    auto body  = text::Font::loadFromFile(*device, *fs, fontPath.c_str(), 15.0f);
    if (!title || !body) return 1;

    ui::UI gui(*device);
    renderer::Camera2D camera;

    // A 1x1 white texture is all a solid rectangle needs; the sprite batch tints it.
    const rhi::TextureHandle white = device->createTexture(
        {.width = 1, .height = 1}, std::vector<u8>{255, 255, 255, 255}.data());

    // --- Row 2: keyframed panels -------------------------------------------
    //
    // Position, size and opacity are three curves over three types. Nothing about them is
    // UI-specific; they are the same Curve<T> that drives a bone.
    anim::Curve<Vec2> slidePos;
    slidePos.loop   = true;
    slidePos.interp = anim::CurveInterp::CatmullRom;   // smooth THROUGH the waypoints
    slidePos.add(0.0f, {-450.0f, -105.0f})
            .add(1.2f, {-150.0f, -65.0f})
            .add(2.4f, { 150.0f, -145.0f})
            .add(3.6f, { 450.0f, -105.0f})
            .add(4.8f, {-450.0f, -105.0f});

    anim::Curve<Vec2> linearPos;   // the SAME waypoints, straight lines: corners at every key
    linearPos.loop   = true;
    linearPos.interp = anim::CurveInterp::Linear;
    linearPos.add(0.0f, {-450.0f, -190.0f})
             .add(1.2f, {-150.0f, -150.0f})
             .add(2.4f, { 150.0f, -230.0f})
             .add(3.6f, { 450.0f, -190.0f})
             .add(4.8f, {-450.0f, -190.0f});

    anim::Curve<Vec2> pulseSize;
    pulseSize.loop = true;
    pulseSize.add(0.0f, {70.0f, 46.0f})
             .add(1.2f, {120.0f, 70.0f}, easing::Ease::OutBack)
             .add(2.4f, {70.0f, 46.0f}, easing::Ease::InOutCubic)
             .add(4.8f, {70.0f, 46.0f});

    anim::Curve<f32> fade;
    fade.loop = true;
    fade.add(0.0f, 0.15f).add(1.2f, 1.0f, easing::Ease::OutCubic)
        .add(3.6f, 1.0f).add(4.8f, 0.15f, easing::Ease::InCubic);

    // --- Row 3: the same two colours, four spaces ---------------------------
    const Color colorA = Color::fromRgb(0xE02828);   // red
    const Color colorB = Color::fromRgb(0x28C048);   // green
    struct Space { anim::ColorSpace space; const char* name; };
    const Space kSpaces[] = {
        {anim::ColorSpace::LinearRgb, "LinearRgb"},
        {anim::ColorSpace::Srgb,      "sRGB"},
        {anim::ColorSpace::Oklab,     "Oklab"},
        {anim::ColorSpace::Hsv,       "HSV"},
    };

    const char* shotPath     = std::getenv("VORTEX_SCREENSHOT");
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64   maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;

    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f32 t = 0.0f;

    VORTEX_INFO("App", "Animated UI: easing curves, keyframed panels, colour spaces. ESC quits.");

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
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);

        f32 mx = 0.0f, my = 0.0f;
        input->mousePosition(mx, my);
        ui::InputState in;
        in.mouse    = camera.screenToWorld(mx, my);
        in.down     = input->isMouseDown(pf::MouseButton::Left);
        in.pressed  = input->isMousePressed(pf::MouseButton::Left);
        in.released = input->isMouseReleased(pf::MouseButton::Left);

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear({srgbToLinear(0.07f), srgbToLinear(0.08f), srgbToLinear(0.11f), 1.0f});
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);

        batch.begin(camera.viewProjection());
        gui.begin(batch, *body, in);

        const Vec4 ink{0.90f, 0.94f, 1.0f, 1.0f};
        const Vec4 dim{0.55f, 0.60f, 0.70f, 1.0f};

        text::drawText(batch, *title, "anim::Curve, pointed at a UI", {-600.0f, 318.0f}, ink, 1.0f, 20);

        // === Row 1: every easing curve ======================================
        //
        // A 3-second cycle. Each track draws the curve's own shape as a strip of dots, and the
        // marker rides it — so the marker's motion and the graph behind it are the same number.
        const f32 cycle = std::fmod(t, 3.0f) / 3.0f;

        text::drawText(batch, *body, "Easing curves — marker rides its own graph",
                       {-600.0f, 282.0f}, dim, 1.0f, 20);

        constexpr f32 kTrackW = 118.0f, kTrackH = 52.0f;
        for (usize i = 0; i < kCurveCount; ++i) {
            const f32 col = static_cast<f32>(i % 6);
            const f32 row = static_cast<f32>(i / 6);
            const Vec2 origin{-585.0f + col * 205.0f, 205.0f - row * 92.0f};

            // The curve's shape, sampled.
            for (int s = 0; s <= 26; ++s) {
                const f32 u = static_cast<f32>(s) / 26.0f;
                const f32 v = easing::evaluate(kCurves[i].curve, u);
                batch.drawSprite(white, {origin.x + u * kTrackW, origin.y + v * kTrackH},
                                 {2.5f, 2.5f}, {0.30f, 0.36f, 0.46f, 1.0f}, {}, 5);
            }

            // The marker, at this instant.
            const f32 v = easing::evaluate(kCurves[i].curve, cycle);
            batch.drawSprite(white, {origin.x + cycle * kTrackW, origin.y + v * kTrackH},
                             {9.0f, 9.0f}, {1.0f, 0.62f, 0.25f, 1.0f}, {}, 6);

            text::drawText(batch, *body, kCurves[i].name,
                           {origin.x, origin.y - 20.0f}, dim, 1.0f, 20);
        }

        // === Row 2: keyframed panels ========================================
        text::drawText(batch, *body,
                       "Keyframed panels — Catmull-Rom (top) vs Linear (bottom): same waypoints",
                       {-600.0f, -30.0f}, dim, 1.0f, 20);

        const f32  loop     = std::fmod(t, 4.8f);
        const Vec2 sp       = slidePos.evaluate(loop);
        const Vec2 lp       = linearPos.evaluate(loop);
        const Vec2 size     = pulseSize.evaluate(loop);
        const f32  opacity  = fade.evaluate(loop);

        // The waypoints themselves, so the two paths can be read against them.
        for (const auto& k : slidePos.keys())
            batch.drawSprite(white, k.value, {5.0f, 5.0f}, {0.35f, 0.40f, 0.50f, 1.0f}, {}, 5);
        for (const auto& k : linearPos.keys())
            batch.drawSprite(white, k.value, {5.0f, 5.0f}, {0.35f, 0.40f, 0.50f, 1.0f}, {}, 5);

        gui.panel(sp, size, {0.30f, 0.70f, 1.00f, opacity});
        gui.panel(lp, {70.0f, 46.0f}, {1.00f, 0.45f, 0.35f, opacity});

        // === Row 3: colour spaces ===========================================
        text::drawText(batch, *body,
                       "The same red -> green, in four colour spaces. They disagree.",
                       {-600.0f, -145.0f}, dim, 1.0f, 20);

        const f32 ct = 0.5f - 0.5f * std::cos(t * 0.9f);   // sweep 0 -> 1 -> 0

        for (usize i = 0; i < 4; ++i) {
            const f32 x0 = -570.0f + static_cast<f32>(i) * 300.0f;

            // The whole ramp, so the dead spot in the middle of the RGB one is visible rather
            // than merely asserted.
            for (int s = 0; s < 40; ++s) {
                const f32 u = static_cast<f32>(s) / 39.0f;
                const Color c = anim::mixColor(colorA, colorB, u, kSpaces[i].space);
                batch.drawSprite(white, {x0 + u * 230.0f, -250.0f}, {7.0f, 30.0f},
                                 {c.r, c.g, c.b, 1.0f}, {}, 5);
            }

            // And the animated sample, riding the ramp.
            const Color c = anim::mixColor(colorA, colorB, ct, kSpaces[i].space);
            batch.drawSprite(white, {x0 + ct * 230.0f, -218.0f}, {13.0f, 13.0f},
                             {c.r, c.g, c.b, 1.0f}, {}, 6);

            text::drawText(batch, *body, kSpaces[i].name, {x0, -185.0f}, dim, 1.0f, 20);
        }

        gui.end();
        batch.end(*frame.cmd);
        frame.cmd->endRenderPass();

        device->endFrame();
        ++frameCount;

        if (shotPath != nullptr && frameCount == 40) {
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
    device->destroyTexture(white);
    swapchain.reset();
    return 0;
}
