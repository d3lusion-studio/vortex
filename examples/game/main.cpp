// A complete 2D game loop: animated sprites, a culled camera, and a follow
// camera — with no window/device/swapchain boilerplate.

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/rhi/device.hpp"

#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

constexpr u32 kFrameSize  = 32;
constexpr u32 kFrameCount = 4;

// A 4-frame flipbook of a pulsing dot, painted straight into pixels so the
// example needs no asset files on disk.
rhi::TextureHandle makeSheet(rhi::IGraphicsDevice& device) {
    const u32 width = kFrameSize * kFrameCount;
    std::vector<u8> pixels(static_cast<usize>(width) * kFrameSize * 4, 0);

    for (u32 frame = 0; frame < kFrameCount; ++frame) {
        const f32 radius = remap(static_cast<f32>(frame), 0.0f, kFrameCount - 1.0f, 6.0f, 15.0f);
        for (u32 y = 0; y < kFrameSize; ++y) {
            for (u32 x = 0; x < kFrameSize; ++x) {
                const Vec2 d{static_cast<f32>(x) + 0.5f - kFrameSize * 0.5f,
                             static_cast<f32>(y) + 0.5f - kFrameSize * 0.5f};
                const bool inside = length(d) <= radius;
                u8* p = &pixels[(static_cast<usize>(y) * width + frame * kFrameSize + x) * 4];
                p[0] = p[1] = p[2] = 255;
                p[3] = inside ? 255 : 0;
            }
        }
    }
    return device.createTexture({.width = width, .height = kFrameSize}, pixels.data());
}

} // namespace

int main() {
    app::AppConfig config;
    config.title           = "Vortex Game";
    config.parallelExtract = true;
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);

    app.onStart([](app::App& a) {
        const rhi::TextureHandle texture = makeSheet(a.device());
        const renderer::SpriteSheet sheet{.texture       = texture,
                                          .textureWidth  = kFrameSize * kFrameCount,
                                          .textureHeight = kFrameSize,
                                          .columns       = kFrameCount,
                                          .rows          = 1};
        const renderer::AnimationHandle clip = a.scene().animations.addFromSheet(sheet, 8.0f, true);

        Random rng{1234u};
        for (int i = 0; i < 20000; ++i) {
            const ecs::Entity e = a.scene().spawn();
            a.registry().get<ecs::Transform2D>(e).position =
                rng.insideRect({-2000.0f, -1500.0f}, {2000.0f, 1500.0f});
            a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
                .texture = texture,
                .color   = Color::fromHsv(rng.range(0.0f, 360.0f), 0.55f, 1.0f),
                .size    = Vec2::one() * rng.range(18.0f, 40.0f)});
            a.registry().emplace<ecs::SpriteAnimator>(e, ecs::SpriteAnimator{
                .clip = clip, .time = rng.range(0.0f, 0.5f)});
            a.registry().emplace<ecs::Velocity>(e, ecs::Velocity{rng.insideUnitCircle() * 90.0f});
        }

        a.scene().addSystem([](ecs::Registry& r, f32 dt) {
            r.view<ecs::Transform2D, ecs::Velocity>(
                [dt](ecs::Entity, ecs::Transform2D& t, ecs::Velocity& v) {
                    t.position += v.value * dt;
                    if (t.position.x < -2000.0f || t.position.x > 2000.0f) v.value.x = -v.value.x;
                    if (t.position.y < -1500.0f || t.position.y > 1500.0f) v.value.y = -v.value.y;
                    t.rotation = wrap(t.rotation + dt, 0.0f, kTwoPi);
                });
        });

        VORTEX_INFO("Game", "20000 animated sprites. WASD pan, scroll zoom, ESC quit.");
    });

    // The camera chases a target rather than snapping to it: damp() converges the
    // same way at any frame rate, unlike a raw lerp against dt.
    app.onUpdate([target = Vec2::zero()](app::App& a, f32 dt) mutable {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();

        Vec2 move{};
        if (in.isKeyDown(pf::Key::A)) move.x -= 1.0f;
        if (in.isKeyDown(pf::Key::D)) move.x += 1.0f;
        if (in.isKeyDown(pf::Key::W)) move.y += 1.0f;
        if (in.isKeyDown(pf::Key::S)) move.y -= 1.0f;
        target += normalize(move) * (600.0f / a.camera().zoom) * dt;

        if (const f32 scroll = in.scrollDelta(); scroll != 0.0f)
            a.camera().zoom = clamp(a.camera().zoom * (1.0f + scroll * 0.1f), 0.05f, 10.0f);

        a.camera().position = damp(a.camera().position, target, 10.0f, dt);

        if (a.frameCount() % 120 == 0 && a.frameCount() > 0)
            VORTEX_INFO("Game", "%.0f FPS | %zu/%zu sprites visible | %u draw calls",
                        static_cast<f64>(a.fps()), a.visibleSprites(),
                        a.registry().aliveCount(), a.drawCalls());
    });

    return app.run();
}
