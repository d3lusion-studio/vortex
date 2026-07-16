// Breakout, the classic — and a worked example of the whole 2D stack pulling
// together: the App loop, Box2D physics, sprite rendering, and the ECS facilities
// added alongside it. Two of those are worth pointing at:
//
//   * A CommandBuffer defers brick destruction. A contact fires from inside the
//     physics step, where destroying an entity (and its body) mid-solve is unsafe,
//     so the hit is *recorded* and applied in the fixed update once the step is
//     done. This is exactly the ordering problem command buffers exist for.
//
//   * A BrickDestroyed event drives scoring. The contact handler only emits; an
//     observer owns the score, the brick count and the win condition. Gameplay
//     that reacts to "a brick died" subscribes without the physics code knowing.
//
// Controls: A/D or Left/Right move the paddle, Space (re)launches, Esc quits.
//
// Headless self-check: set VORTEX_BREAKOUT_CHECK=1 and the ball auto-launches, the
// game quits after a fixed span of simulated time, and the process exits non-zero
// unless bricks were actually cleared — so it doubles as a CI regression test the
// way the other examples do (it does need a GPU, like any App example).

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/commands.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/physics/components.hpp"
#include "vortex/physics/physics_world.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_batch.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace vortex;

namespace {

constexpr f32 kFieldHalfW  = 560.0f;
constexpr f32 kFieldTop    = 360.0f;
constexpr f32 kPaddleY     = -320.0f;
constexpr f32 kDrainY      = -430.0f;
constexpr f32 kPaddleHalfW = 80.0f;
constexpr f32 kPaddleSpeed = 900.0f;
constexpr f32 kBallRadius  = 11.0f;
constexpr f32 kBallSpeed   = 520.0f;

struct Paddle {};
struct Wall {};
struct Ball {};
struct Brick {
    i32  points = 10;
    bool doomed = false;
};

struct BrickDestroyed {
    i32 points = 0;
};

struct GameState {
    ecs::Entity        ball;
    ecs::Entity        paddle;
    i32                score           = 0;
    i32                lives           = 3;
    i32                bricksRemaining = 0;
    i32                bricksDestroyed = 0;
    f32                simTime         = 0.0f;
    bool               won             = false;
    bool               checkMode       = false;
    ecs::CommandBuffer deferred;   // brick destroys recorded during the physics step
};

ecs::Entity spawnWall(app::App& a, Vec2 at, Vec2 size) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = at;
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = Color::fromRgb(0x3A4256), .size = size, .layer = -1});
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
        .type = physics::BodyType::Static, .friction = 0.0f, .restitution = 1.0f});
    a.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{.halfExtents = size * 0.5f});
    a.registry().emplace<Wall>(e, Wall{});
    return e;
}

ecs::Entity spawnBrick(app::App& a, Vec2 at, Vec2 size, Color color, i32 points) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = at;
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = color, .size = size, .layer = 0});
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
        .type = physics::BodyType::Static, .friction = 0.0f, .restitution = 1.0f});
    a.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{.halfExtents = size * 0.5f});
    a.registry().emplace<Brick>(e, Brick{.points = points});
    return e;
}

ecs::Entity spawnPaddle(app::App& a) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = {0.0f, kPaddleY};
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = Color::fromRgb(0xE8E8F0),
        .size = {kPaddleHalfW * 2.0f, 24.0f}, .layer = 1});
    // Kinematic: driven by velocity, pushes the ball, and is never pushed itself.
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
        .type = physics::BodyType::Kinematic, .friction = 0.0f, .restitution = 1.0f});
    a.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{.halfExtents = {kPaddleHalfW, 12.0f}});
    a.registry().emplace<Paddle>(e, Paddle{});
    return e;
}

ecs::Entity spawnBall(app::App& a) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = {0.0f, kPaddleY + 40.0f};
    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = Color::fromRgb(0xFFD166),
        .size = Vec2::one() * kBallRadius * 2.0f, .layer = 2});
    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
        .type = physics::BodyType::Dynamic, .friction = 0.0f, .restitution = 1.0f,
        .gravityScale = 0.0f,   // Breakout has no gravity; the ball keeps its speed
        .fixedRotation = true,
        .bullet = true});       // continuous collision so a fast ball never tunnels a brick
    a.registry().emplace<physics::CircleCollider2D>(e, physics::CircleCollider2D{.radius = kBallRadius});
    a.registry().emplace<Ball>(e, Ball{});
    return e;
}

// Put the ball back above the paddle and fire it up-field at a fixed angle.
void launchBall(app::App& a, GameState& state) {
    const auto& paddlePos = a.registry().get<ecs::Transform2D>(state.paddle).position;
    a.physics().setTransform(state.ball, {paddlePos.x, kPaddleY + 40.0f}, 0.0f);
    a.physics().setLinearVelocity(state.ball, normalize(Vec2{0.35f, 1.0f}) * kBallSpeed);
}

void buildBricks(app::App& a, GameState& state) {
    constexpr int   cols    = 11;
    constexpr int   rows    = 5;
    constexpr f32   gap     = 8.0f;
    const f32       brickW  = (kFieldHalfW * 2.0f - 80.0f) / cols - gap;
    constexpr f32   brickH  = 28.0f;
    const f32       originX = -((cols - 1) * (brickW + gap)) * 0.5f;
    const f32       originY = kFieldTop - 80.0f;

    for (int r = 0; r < rows; ++r) {
        const Color color  = Color::fromHsv(200.0f + static_cast<f32>(r) * 22.0f, 0.6f, 0.95f);
        const i32   points = (rows - r) * 10;   // higher rows are worth more
        for (int c = 0; c < cols; ++c) {
            const Vec2 at{originX + static_cast<f32>(c) * (brickW + gap),
                          originY - static_cast<f32>(r) * (brickH + gap)};
            spawnBrick(a, at, {brickW, brickH}, color, points);
            ++state.bricksRemaining;
        }
    }
}

}

int main() {
    app::AppConfig config;
    config.title          = "Vortex Breakout";
    config.clearColor     = Color::fromRgb(0x0A0D17);
    config.pixelsPerMeter = 100.0f;
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App  app(config);
    GameState state;
    state.checkMode = std::getenv("VORTEX_BREAKOUT_CHECK") != nullptr;

    app.onStart([&state](app::App& a) {
        spawnWall(a, {-kFieldHalfW - 20.0f, 0.0f}, {40.0f, kFieldTop * 2.0f + 40.0f});
        spawnWall(a, { kFieldHalfW + 20.0f, 0.0f}, {40.0f, kFieldTop * 2.0f + 40.0f});
        spawnWall(a, {0.0f, kFieldTop + 20.0f},    {kFieldHalfW * 2.0f + 80.0f, 40.0f});

        state.paddle = spawnPaddle(a);
        state.ball   = spawnBall(a);
        buildBricks(a, state);

        a.physics().step(a.registry(), 0.0f);

        a.registry().observe<BrickDestroyed>([&state](ecs::Trigger<BrickDestroyed>& t) {
            state.score += t.event.points;
            ++state.bricksDestroyed;
            --state.bricksRemaining;
        });

        a.physics().setContactBegin([&a, &state](ecs::Entity x, ecs::Entity y) {
            const ecs::Entity other = (x == state.ball) ? y : (y == state.ball ? x : ecs::Entity{});
            if (!other.valid()) return;
            Brick* brick = a.registry().tryGet<Brick>(other);
            if (!brick || brick->doomed) return;
            brick->doomed = true;
            state.deferred.destroy(other);
            a.registry().trigger(BrickDestroyed{brick->points});
        });

        launchBall(a, state);
        VORTEX_INFO("Breakout", "%d bricks. A/D to move, Space to relaunch, Esc to quit.",
                    state.bricksRemaining);
    });

    app.onFixedUpdate([&state](app::App& a, f32 dt) {
        state.deferred.flush(a.registry());

        pf::IInputProvider& in = a.input();
        f32 move = 0.0f;
        if (state.checkMode) {
            const f32 ballX = a.registry().get<ecs::Transform2D>(state.ball).position.x;
            const f32 padX  = a.registry().get<ecs::Transform2D>(state.paddle).position.x;
            if (ballX < padX - 6.0f) move -= 1.0f;
            else if (ballX > padX + 6.0f) move += 1.0f;
        } else {
            if (in.isKeyDown(pf::Key::A) || in.isKeyDown(pf::Key::Left))  move -= 1.0f;
            if (in.isKeyDown(pf::Key::D) || in.isKeyDown(pf::Key::Right)) move += 1.0f;
        }
        const f32 px = a.registry().get<ecs::Transform2D>(state.paddle).position.x;
        const f32 limit = kFieldHalfW - kPaddleHalfW - 20.0f;
        if ((px <= -limit && move < 0.0f) || (px >= limit && move > 0.0f)) move = 0.0f;
        a.physics().setLinearVelocity(state.paddle, {move * kPaddleSpeed, 0.0f});

        const Vec2 v = a.physics().linearVelocity(state.ball);
        if (lengthSquared(v) > 1.0f)
            a.physics().setLinearVelocity(state.ball, normalize(v) * kBallSpeed);

        // Drain: ball fell past the paddle.
        const f32 ballY = a.registry().get<ecs::Transform2D>(state.ball).position.y;
        if (ballY < kDrainY) {
            if (--state.lives <= 0) {
                VORTEX_INFO("Breakout", "Game over. Final score %d.", state.score);
                if (state.checkMode) a.quit();
            } else {
                VORTEX_INFO("Breakout", "Ball lost. %d lives left.", state.lives);
                launchBall(a, state);
            }
        }

        if (state.bricksRemaining <= 0 && !state.won) {
            state.won = true;
            VORTEX_INFO("Breakout", "You cleared every brick! Score %d.", state.score);
            if (state.checkMode) a.quit();
        }

        state.simTime += dt;
        if (state.checkMode && state.simTime > 12.0f) a.quit();   // bound the CI run
    });

    app.onUpdate([&state](app::App& a, f32) {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();
        if (in.isKeyPressed(pf::Key::Space))  launchBall(a, state);
    });

    const int rc = app.run();

    if (state.checkMode) {
        const bool ok = state.bricksDestroyed > 0 && state.score > 0;
        std::printf("\n[%s] Breakout self-check: destroyed %d brick(s), score %d\n",
                    ok ? "PASS" : "FAIL", state.bricksDestroyed, state.score);
        return ok ? 0 : 1;
    }
    return rc;
}
