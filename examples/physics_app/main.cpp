// Physics through App: no PhysicsWorld to build, no step() to call. Asking for
// app.physics() creates the world for the active scene, and the loop steps it inside
// the same fixed update as gameplay.
//
// Shows: box and circle colliders, a revolute-jointed pendulum, a raycast from the
// mouse, contact begin/end callbacks, and a sensor that counts what falls into it.

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/physics/components.hpp"
#include "vortex/physics/physics_world.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_batch.hpp"

#include <cstdlib>

using namespace vortex;

namespace {

// Spawn a dynamic body. The collider decides the shape; the sprite just draws it.
ecs::Entity spawnBox(app::App& a, Vec2 at, Vec2 size, Color color) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = at;
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = color, .size = size});
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{.restitution = 0.25f});
    a.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{
        .halfExtents = size * 0.5f});
    return e;
}

ecs::Entity spawnBall(app::App& a, Vec2 at, f32 radius, Color color) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = at;
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = color, .size = Vec2::one() * radius * 2.0f});
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{.restitution = 0.6f});
    a.registry().emplace<physics::CircleCollider2D>(e, physics::CircleCollider2D{
        .radius = radius});
    return e;
}

ecs::Entity spawnStatic(app::App& a, Vec2 at, Vec2 size, Color color, bool sensor = false) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = at;
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = color, .size = size, .layer = -1});
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
        .type = physics::BodyType::Static});
    a.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{
        .halfExtents = size * 0.5f, .isSensor = sensor});
    return e;
}

struct State {
    int         contacts   = 0;
    int         inSensor   = 0;   // currently overlapping the drain
    ecs::Entity sensor;
    Vec2        rayHit{};
    bool        rayHitValid = false;
};

}

int main() {
    app::AppConfig config;
    config.title          = "Vortex Physics (App)";
    config.pixelsPerMeter = 100.0f;
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);
    State    state;

    app.onStart([&state](app::App& a) {
        // Ground and walls.
        spawnStatic(a, {0.0f, -320.0f}, {1100.0f, 40.0f}, Color::fromRgb(0x3A4256));
        spawnStatic(a, {-540.0f, 0.0f}, {40.0f, 620.0f}, Color::fromRgb(0x3A4256));
        spawnStatic(a, {540.0f, 0.0f},  {40.0f, 620.0f}, Color::fromRgb(0x3A4256));

        // A sensor sitting on the floor: things pass through it, but it reports them.
        state.sensor = spawnStatic(a, {0.0f, -280.0f}, {160.0f, 40.0f},
                                   Color{0.2f, 0.8f, 0.5f, 0.35f}, /*sensor=*/true);

        // A stack of boxes and a few balls, so both collider kinds are in play.
        Random rng{3u};
        for (int i = 0; i < 14; ++i)
            spawnBox(a, {rng.range(-300.0f, 300.0f), 100.0f + 60.0f * static_cast<f32>(i)},
                     Vec2::one() * rng.range(28.0f, 46.0f),
                     Color::fromHsv(rng.range(190.0f, 260.0f), 0.55f, 0.95f));
        for (int i = 0; i < 6; ++i)
            spawnBall(a, {rng.range(-300.0f, 300.0f), 420.0f + 50.0f * static_cast<f32>(i)},
                      rng.range(14.0f, 24.0f), Color::fromRgb(0xFFD166));

        // A pendulum: an anchor and a bob, pinned by a revolute joint. Both bodies
        // must exist first, so step the world once with dt = 0 to build them.
        const ecs::Entity anchor = spawnStatic(a, {0.0f, 300.0f}, {24.0f, 24.0f},
                                               Color::fromRgb(0x8899AA));
        const ecs::Entity bob    = spawnBall(a, {220.0f, 300.0f}, 30.0f,
                                             Color::fromRgb(0xE8734A));

        a.physics().step(a.registry(), 0.0f);   // creates the bodies, simulates nothing
        a.physics().createRevoluteJoint(anchor, bob, {0.0f, 300.0f});

        a.physics().setContactBegin([&state](ecs::Entity x, ecs::Entity y) {
            ++state.contacts;
            if (x == state.sensor || y == state.sensor) ++state.inSensor;
        });
        a.physics().setContactEnd([&state](ecs::Entity x, ecs::Entity y) {
            if (x == state.sensor || y == state.sensor) --state.inSensor;
        });

        VORTEX_INFO("Physics", "Click to drop a ball. Mouse casts a ray. ESC quits.");
    });

    app.onUpdate([&state](app::App& a, f32) {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();

        f32 mx = 0.0f, my = 0.0f;
        in.mousePosition(mx, my);
        const Vec2 mouse = a.camera().screenToWorld(mx, my);

        if (in.isMousePressed(pf::MouseButton::Left))
            spawnBall(a, mouse, 18.0f, Color::fromRgb(0x5AC8FA));

        // Right-click picks: the point query answers "what is under the cursor".
        if (in.isMousePressed(pf::MouseButton::Right)) {
            if (const ecs::Entity picked = a.physics().pointQuery(mouse); picked.valid())
                a.physics().applyLinearImpulse(picked, {0.0f, 12.0f});
        }

        // A ray straight down from the cursor, drawn in onRender.
        const physics::RaycastHit hit = a.physics().raycast(mouse, {mouse.x, -400.0f});
        state.rayHitValid = hit.hit();
        state.rayHit      = hit.point;

        if (a.frameCount() % 120 == 0 && a.frameCount() > 0)
            VORTEX_INFO("Physics", "%.0f FPS | %zu bodies | %d contacts | %d in drain",
                        static_cast<f64>(a.fps()), a.physics().bodyCount(),
                        state.contacts, state.inSensor);
    });

    app.onRender([&state](app::App& a, renderer::SpriteBatch& batch) {
        if (!state.rayHitValid) return;
        // A dot where the ray landed.
        batch.drawSprite(a.whiteTexture(), state.rayHit, {12.0f, 12.0f},
                         Color::fromRgb(0xFF4D4D), kFullUV, /*layer=*/50);
    });

    return app.run();
}
