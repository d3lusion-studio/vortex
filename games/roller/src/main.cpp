// Roller — see game.hpp. The whole game on the App loop's 3D path:
//
//   * onRender3D submits the floor, walls, gems and ball to the MeshRenderer the loop owns.
//   * onFixedUpdate rolls the ball, resolves it against the walls, and collects gems.
//   * onUi draws the HUD on top of the tone-mapped 3D scene.
//
// Controls: WASD / arrows roll (camera-relative), R restarts, Esc quits.
//
// VORTEX_ROLLER_CHECK=1 plays it hands-free — the ball steers to the nearest gem until the
// board is clear — and exits non-zero unless the round was actually won. Needs a GPU, like
// every App example here.

#include "game.hpp"

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/core/math/quat.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/camera.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/text/font.hpp"
#include "vortex/text/text_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace vortex;
using namespace roller;

namespace {

// Meshes and their look, built once. Colours are sRGB hex decoded to linear light.
struct Art {
    renderer::MeshHandle floor;
    renderer::MeshHandle wall;
    renderer::MeshHandle ball;
    renderer::MeshHandle gem;
};

struct Game {
    Art                         art;
    GameState                   state;
    std::unique_ptr<text::Font> font;

    // The moving entities. Floor and walls are spawned once and never move, so they keep no
    // handle. Gems and the ball are updated every frame, and gems are disabled when picked
    // up — which is all it takes to drop them from the loop's auto-draw.
    ecs::Entity              ballEntity;
    std::vector<ecs::Entity> gemEntities;

    bool checkMode = false;
    f32  time      = 0.0f;

    std::string shotPath;   // VORTEX_SCREENSHOT: grab one frame once the scene has settled
    i32         shotFrame = 0;
    i32         shotAt     = 90;
};

// --- Simulation ---------------------------------------------------------------

// Steer the ball. In play mode this reads WASD in camera space; in check mode it aims at
// the nearest uncollected gem, so the whole game runs itself for CI.
Vec3 desiredPush(app::App& app, const Game& game) {
    if (game.state.phase != Phase::Playing) return {};

    if (game.checkMode) {
        const Gem* nearest = nullptr;
        f32        best    = 1e30f;
        for (const Gem& g : game.state.gems) {
            if (g.collected) continue;
            const Vec3 d{g.position.x - game.state.ballPos.x, 0.0f,
                         g.position.z - game.state.ballPos.z};
            const f32 dist = length(d);
            if (dist < best) { best = dist; nearest = &g; }
        }
        if (nearest == nullptr) return {};
        return normalize(Vec3{nearest->position.x - game.state.ballPos.x, 0.0f,
                              nearest->position.z - game.state.ballPos.z});
    }

    pf::IInputProvider& in = app.input();
    Vec2 move{0.0f, 0.0f};
    if (in.isKeyDown(pf::Key::A) || in.isKeyDown(pf::Key::Left))  move.x -= 1.0f;
    if (in.isKeyDown(pf::Key::D) || in.isKeyDown(pf::Key::Right)) move.x += 1.0f;
    if (in.isKeyDown(pf::Key::W) || in.isKeyDown(pf::Key::Up))    move.y -= 1.0f;
    if (in.isKeyDown(pf::Key::S) || in.isKeyDown(pf::Key::Down))  move.y += 1.0f;
    if (move.x == 0.0f && move.y == 0.0f) return {};

    // Camera-relative: "forward" is the way the camera looks, flattened onto the ground, so
    // pressing up rolls the ball away from the viewer whatever angle the camera sits at.
    const Vec3 toBall = game.state.ballPos - game.state.camPos;
    Vec3       fwd{toBall.x, 0.0f, toBall.z};
    if (length(fwd) < 1e-4f) fwd = {0.0f, 0.0f, -1.0f};
    fwd = normalize(fwd);
    const Vec3 right{-fwd.z, 0.0f, fwd.x};

    return normalize(fwd * -move.y + right * move.x);
}

void updateBall(app::App& app, Game& game, f32 dt) {
    GameState& st = game.state;

    const Vec3 push = desiredPush(app, game);
    st.ballVel = st.ballVel + push * (kAccel * dt);

    // Friction only when the player is not pushing, so a released stick coasts to a stop
    // rather than the ball feeling glued.
    if (length(push) < 0.01f) {
        const f32 speed = length(st.ballVel);
        if (speed > 0.0f) {
            const f32 drop = std::min(speed, kFriction * dt * std::max(speed, 3.0f));
            st.ballVel = st.ballVel * ((speed - drop) / speed);
        }
    }

    const f32 speed = length(st.ballVel);
    if (speed > kMaxSpeed) st.ballVel = st.ballVel * (kMaxSpeed / speed);

    st.ballPos = st.ballPos + st.ballVel * dt;

    // Walls: the play area the ball's CENTRE may occupy is the floor minus the ball radius
    // and the wall thickness. Hitting a wall reflects the velocity, damped — a bounce, not
    // a dead stop, which reads as a wall rather than glue. Only x and z; the ball never
    // leaves the ground.
    const f32 limit = kArenaHalf - kWallThick - kBallRadius;
    const auto bounce = [&](f32& pos, f32& vel) {
        if (pos < -limit) { pos = -limit; if (vel < 0.0f) vel = -vel * kRestitution; }
        if (pos >  limit) { pos =  limit; if (vel > 0.0f) vel = -vel * kRestitution; }
    };
    bounce(st.ballPos.x, st.ballVel.x);
    bounce(st.ballPos.z, st.ballVel.z);

    st.ballPos.y = kBallRadius;

    // Roll the ball to match how far it moved: the spin axis is horizontal and perpendicular
    // to travel, and the angle is distance / radius. Cosmetic, but its absence is glaring.
    const Vec3 flatVel{st.ballVel.x, 0.0f, st.ballVel.z};
    const f32  flatSpeed = length(flatVel);
    if (flatSpeed > 0.01f) {
        const Vec3 dir = flatVel * (1.0f / flatSpeed);
        st.rollAxis = {dir.z, 0.0f, -dir.x};   // perpendicular, in the ground plane
        st.rollAngle += flatSpeed * dt / kBallRadius;
    }
}

void collectGems(Game& game) {
    GameState& st = game.state;
    for (Gem& g : st.gems) {
        if (g.collected) continue;
        const Vec3 d{g.position.x - st.ballPos.x, 0.0f, g.position.z - st.ballPos.z};
        if (length(d) <= kPickupRange) {
            g.collected = true;
            ++st.collected;
        }
    }
    if (st.remaining() == 0 && st.phase == Phase::Playing) st.phase = Phase::Won;
}

void updateCamera(Game& game, f32 dt) {
    GameState& st = game.state;
    // Behind and above the ball. "Behind" is opposite the ball's motion so the camera
    // trails it; when nearly still it holds its last angle rather than snapping.
    const Vec3 flatVel{st.ballVel.x, 0.0f, st.ballVel.z};
    Vec3       back{0.0f, 0.0f, 1.0f};
    if (length(flatVel) > 0.5f) back = normalize(Vec3{-flatVel.x, 0.0f, -flatVel.z});

    const Vec3 want = st.ballPos + back * 11.0f + Vec3{0.0f, 8.0f, 0.0f};
    st.camPos = damp(st.camPos, want, 4.0f, dt);
}

// --- 3D entities --------------------------------------------------------------
//
// Everything visible is an entity with a Transform3D and a MeshComp. The App loop extracts
// and draws them itself, the same way it draws 2D SpriteComp entities — so this game submits
// no meshes of its own. Gems and the ball get their transforms rewritten each frame; the
// static floor and walls are spawned once and left alone.

ecs::Entity spawnMesh(ecs::Registry& reg, renderer::MeshHandle m, Vec3 pos, Vec3 scale,
                      Vec4 color, f32 metallic, f32 roughness) {
    const ecs::Entity e = reg.create();
    reg.emplace<ecs::Transform3D>(e, ecs::Transform3D{.position = pos, .scale = scale});
    reg.emplace<ecs::MeshComp>(e, ecs::MeshComp{.mesh      = m,
                                                .color     = color,
                                                .metallic  = metallic,
                                                .roughness = roughness});
    return e;
}

void spawnWorld(ecs::Registry& reg, Game& game) {
    // Floor.
    spawnMesh(reg, game.art.floor, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
              Color::fromRgb(0x3C4A5E), 0.0f, 0.9f);

    // Four walls, each a stretched cube along one edge.
    const f32  span    = kArenaHalf * 2.0f;
    const f32  e       = kArenaHalf - kWallThick * 0.5f;
    const Vec4 wallCol = Color::fromRgb(0x28323F);
    spawnMesh(reg, game.art.wall, {0.0f, kWallHeight * 0.5f,  e}, {span, kWallHeight, kWallThick}, wallCol, 0.0f, 0.8f);
    spawnMesh(reg, game.art.wall, {0.0f, kWallHeight * 0.5f, -e}, {span, kWallHeight, kWallThick}, wallCol, 0.0f, 0.8f);
    spawnMesh(reg, game.art.wall, { e, kWallHeight * 0.5f, 0.0f}, {kWallThick, kWallHeight, span}, wallCol, 0.0f, 0.8f);
    spawnMesh(reg, game.art.wall, {-e, kWallHeight * 0.5f, 0.0f}, {kWallThick, kWallHeight, span}, wallCol, 0.0f, 0.8f);

    // Gems and the ball.
    game.gemEntities.clear();
    for (const Gem& g : game.state.gems)
        game.gemEntities.push_back(spawnMesh(reg, game.art.gem, g.position,
                                             {kGemRadius, kGemRadius, kGemRadius},
                                             Color::fromRgb(0x36E0C0), 0.85f, 0.12f));

    game.ballEntity = spawnMesh(reg, game.art.ball, game.state.ballPos,
                                {kBallRadius, kBallRadius, kBallRadius},
                                Color::fromRgb(0xF2A03C), 0.1f, 0.35f);
}

// Push this frame's simulation into the entities the loop will draw.
void syncEntities(ecs::Registry& reg, Game& game) {
    const GameState& st = game.state;

    for (usize i = 0; i < game.gemEntities.size(); ++i) {
        const ecs::Entity e = game.gemEntities[i];
        if (!reg.alive(e)) continue;
        // A collected gem is disabled, which drops it from the loop's extract with no other
        // bookkeeping — the same trick alien_cake_addict uses.
        if (st.gems[i].collected) { reg.disable(e); continue; }

        if (auto* t = reg.tryGet<ecs::Transform3D>(e)) {
            const f32 bob = std::sin(game.time * 2.0f + st.gems[i].position.x) * 0.18f;
            t->position   = {st.gems[i].position.x, st.gems[i].position.y + bob,
                             st.gems[i].position.z};
            t->rotation   = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, game.time * 1.8f);
        }
    }

    if (auto* t = reg.tryGet<ecs::Transform3D>(game.ballEntity)) {
        t->position = st.ballPos;
        t->rotation = Quat::fromAxisAngle(st.rollAxis, st.rollAngle);
    }
}

// --- HUD ---------------------------------------------------------------------

void text(Game& game, renderer::SpriteBatch& batch, std::string_view s, Vec2 topLeft,
          Color color, f32 scale = 1.0f) {
    text::drawText(batch, *game.font, s, topLeft, color, scale, 1000);
}

void textCentered(Game& game, renderer::SpriteBatch& batch, std::string_view s, Vec2 center,
                  Color color, f32 scale = 1.0f) {
    const Vec2 m = game.font->measure(s);
    text(game, batch, s, {center.x - m.x * scale * 0.5f, center.y + m.y * scale * 0.5f}, color,
         scale);
}

void drawHud(app::App& app, Game& game, renderer::SpriteBatch& batch) {
    const GameState& st    = game.state;
    const f32        halfW = app.camera().viewportWidth * 0.5f;
    const f32        halfH = app.camera().viewportHeight * 0.5f;
    const Color      ink   = Color::fromRgb(0xF4F7FF);

    char line[64];
    std::snprintf(line, sizeof(line), "Gems  %d / %zu", st.collected, st.gems.size());
    text(game, batch, line, {-halfW + 24.0f, halfH - 20.0f}, ink);

    std::snprintf(line, sizeof(line), "%04.1f", static_cast<f64>(std::max(0.0f, st.timeLeft)));
    const Color clock = st.timeLeft < 10.0f ? Color::fromRgb(0xFF6B5B) : ink;
    textCentered(game, batch, line, {0.0f, halfH - 20.0f}, clock);

    if (st.phase == Phase::Won)
        textCentered(game, batch, "You cleared the board!", {0.0f, 40.0f},
                     Color::fromRgb(0x8CF2C4), 1.6f);
    else if (st.phase == Phase::Lost)
        textCentered(game, batch, "Out of time", {0.0f, 40.0f}, Color::fromRgb(0xFF7A6B), 1.6f);

    if (st.phase != Phase::Playing)
        textCentered(game, batch, "Press R to play again", {0.0f, -10.0f}, ink);
}

}   // namespace

int main() {
    app::AppConfig config;
    config.title      = "Vortex Roller";
    config.width      = 1280;
    config.height     = 720;
    config.clearColor = Color::fromRgb(0x8FB8D8);   // the sky, and the 3D clear
    config.render3D   = true;                        // brings the mesh + shadow + HDR stack
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);
    Game     game;
    game.checkMode = std::getenv("VORTEX_ROLLER_CHECK") != nullptr;
    if (const char* shot = std::getenv("VORTEX_SCREENSHOT")) game.shotPath = shot;
    if (const char* at = std::getenv("VORTEX_SHOT_FRAME")) game.shotAt = std::atoi(at);

    app.onStart([&game](app::App& a) {
        renderer::MeshRenderer* mesh3d = a.mesh3d();
        if (mesh3d == nullptr) {   // render3D was not set — nothing to draw into
            VORTEX_ERROR("Roller", "render3D is off; the game has no 3D renderer.");
            a.quit();
            return;
        }
        renderer::MeshRenderer& mesh = *mesh3d;
        game.art.floor = mesh.createMesh(renderer::makePlane(kArenaHalf * 2.0f));
        game.art.wall  = mesh.createMesh(renderer::makeCube(1.0f));
        game.art.ball  = mesh.createMesh(renderer::makeSphere(24, 32, 1.0f));
        game.art.gem   = mesh.createMesh(renderer::makeTorus(0.7f, 0.28f, 20, 28));

        // The sun: angled so the ball and walls throw shadows across the floor, which is the
        // whole point of turning shadows on — a flat-lit 3D scene could be a painting.
        renderer::SceneLighting& lit = a.lighting3d();
        lit.sun.direction = normalize(Vec3{-0.5f, -1.0f, -0.35f});
        lit.sun.intensity = 3.2f;
        lit.sun.color     = {1.0f, 0.96f, 0.88f};
        lit.sun.ambient   = {0.35f, 0.4f, 0.5f};   // cool sky fill, so shadows are not black
        lit.shadow.enabled     = true;
        lit.shadow.resolution  = 2048;
        lit.shadow.maxDistance = 60.0f;

        game.font = text::Font::loadDefault(a.device(), a.fileSystem(), 26.0f);
        if (!game.font) VORTEX_WARN("Roller", "No system font; the HUD will be invisible.");

        resetGame(game.state);
        spawnWorld(a.registry(), game);
        VORTEX_INFO("Roller", "Collect %zu gems in %.0fs. WASD roll, R restart, Esc quit.",
                    game.state.gems.size(), static_cast<f64>(kRoundSeconds));
    });

    app.onFixedUpdate([&game](app::App& a, f32 dt) {
        game.time += dt;
        GameState& st = game.state;

        if (st.phase == Phase::Playing) {
            updateBall(a, game, dt);
            collectGems(game);
            st.timeLeft -= dt;
            if (st.timeLeft <= 0.0f && st.remaining() > 0) {
                st.timeLeft = 0.0f;
                st.phase    = Phase::Lost;
            }
        } else {
            game.state.bannerTimer += dt;
        }

        updateCamera(game, dt);
        syncEntities(a.registry(), game);

        // Drive the loop's 3D camera from the game's follow state.
        renderer::Camera& cam = a.camera3d();
        cam.position = st.camPos;
        cam.target   = st.ballPos + Vec3{0.0f, kBallRadius, 0.0f};
        cam.fovYRadians = 1.05f;

        if (game.checkMode && (st.phase != Phase::Playing || game.time > 60.0f)) a.quit();
    });

    app.onUpdate([&game](app::App& a, f32) {
        if (!game.shotPath.empty() && ++game.shotFrame >= game.shotAt) {
            a.requestScreenshot(game.shotPath);
            game.shotPath.clear();
        }
        if (game.checkMode) return;
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();
        if (in.isKeyPressed(pf::Key::R)) {
            resetGame(game.state);
            for (const ecs::Entity e : game.gemEntities)
                if (a.registry().alive(e)) a.registry().enable(e);
        }
    });

    app.onUi([&game](app::App& a, renderer::SpriteBatch& batch) {
        if (game.font) drawHud(a, game, batch);
    });

    const int rc = app.run();

    if (game.checkMode) {
        const bool ok = game.state.phase == Phase::Won;
        std::printf("\n[%s] Roller self-check: %d/%zu gems, phase=%s\n", ok ? "PASS" : "FAIL",
                    game.state.collected, game.state.gems.size(),
                    game.state.phase == Phase::Won ? "Won"
                        : game.state.phase == Phase::Lost ? "Lost" : "Playing");
        return ok ? 0 : 1;
    }
    return rc;
}
