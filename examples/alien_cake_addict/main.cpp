// Alien Cake Addict: a small 3D game on a grid. A player cube hops around a board,
// and every time it lands on the cake, the score goes up and a new cake appears
// somewhere else. Each cake eaten also drops a block onto a growing bonus tower.
//
// It is the worked example of the 3D path — mesh rendering, a shadow-casting sun,
// tone-mapped HDR — married to plain grid gameplay. The gameplay is a small value
// type (Game) with no rendering in it at all, which is what lets the self-check
// verify the rules with no GPU: VORTEX_ALIENCAKE_CHECK=1 auto-plays toward the cake
// and asserts the score climbs, then exits (0 on success).
//
// Controls: WASD / arrows move one tile; Esc quits.

#include "vortex/core/log.hpp"
#include "vortex/core/math/random.hpp"
#include "vortex/core/math/scalar.hpp"
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
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

// ---------------------------------------------------------------- gameplay
// A pure value type: the rules of the game, no rendering. This is the part the
// self-check exercises without a GPU.
struct Game {
    static constexpr int kBoard = 8;

    int    playerX = kBoard / 2, playerZ = kBoard / 2;
    int    cakeX = 0, cakeZ = 0;
    int    score = 0;
    Random rng{1234u};

    Game() { spawnCake(); }

    void spawnCake() {
        do {
            cakeX = static_cast<int>(rng.nextBounded(kBoard));
            cakeZ = static_cast<int>(rng.nextBounded(kBoard));
        } while (cakeX == playerX && cakeZ == playerZ);   // never on the player
    }

    // Move one tile, clamped to the board. Landing on the cake eats it.
    void move(int dx, int dz) {
        playerX = clamp(playerX + dx, 0, kBoard - 1);
        playerZ = clamp(playerZ + dz, 0, kBoard - 1);
        if (playerX == cakeX && playerZ == cakeZ) {
            ++score;
            spawnCake();
        }
    }

    // World-space centre of a tile.
    [[nodiscard]] Vec3 tileToWorld(int x, int z, f32 y = 0.0f) const {
        constexpr f32 spacing = 1.2f;
        const f32 off = (kBoard - 1) * 0.5f;
        return {(static_cast<f32>(x) - off) * spacing, y, (static_cast<f32>(z) - off) * spacing};
    }
};

// The greedy auto-player the self-check uses: step one tile toward the cake.
void stepToward(Game& g) {
    const int dx = (g.cakeX > g.playerX) - (g.cakeX < g.playerX);
    if (dx != 0) { g.move(dx, 0); return; }
    const int dz = (g.cakeZ > g.playerZ) - (g.cakeZ < g.playerZ);
    g.move(0, dz);
}

int runSelfCheck() {
    Game g;
    constexpr int kTarget = 8;
    for (int step = 0; step < 2000 && g.score < kTarget; ++step) stepToward(g);

    // The player must stay on the board the whole time, and the cake never under it.
    const bool onBoard = g.playerX >= 0 && g.playerX < Game::kBoard &&
                         g.playerZ >= 0 && g.playerZ < Game::kBoard;
    const bool cakeOk  = !(g.cakeX == g.playerX && g.cakeZ == g.playerZ);
    const bool ok = g.score >= kTarget && onBoard && cakeOk;

    std::printf("\n[%s] Alien-cake self-check: reached score %d (target %d), player on board=%d\n",
                ok ? "PASS" : "FAIL", g.score, kTarget, onBoard ? 1 : 0);
    return ok ? 0 : 1;
}

}

int main() {
    if (std::getenv("VORTEX_ALIENCAKE_CHECK")) return runSelfCheck();

    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Alien Cake Addict"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    const rhi::Format swapFormat  = swapchain->format();
    const rhi::Format depthFormat = rhi::Format::D32_SFLOAT;
    const rhi::Format hdrFormat   = rhi::Format::R32G32B32A32_SFLOAT;
    constexpr u32     kShadowRes  = 2048;

    renderer::MeshRenderer mesh(*device, hdrFormat, depthFormat);
    renderer::PostProcess  post(*device, hdrFormat, swapFormat);
    renderer::RenderGraph  graph(*device);

    const renderer::MeshHandle cubeMesh  = mesh.createMesh(renderer::makeCube(1.0f));
    const renderer::MeshHandle floorMesh = mesh.createMesh(renderer::makePlane(Game::kBoard * 1.2f + 1.0f));
    const renderer::MeshHandle cakeMesh  = mesh.createMesh(renderer::makeSphere());

    // --- Scene entities driven from the Game state each frame -----------------
    Game          game;
    ecs::Registry reg;

    const ecs::Entity floor = reg.create();
    reg.emplace<ecs::Transform3D>(floor, ecs::Transform3D{.position = {0.0f, -0.5f, 0.0f}});
    reg.emplace<ecs::MeshComp>(floor, ecs::MeshComp{.mesh = floorMesh,
                                                    .color = {0.24f, 0.28f, 0.36f, 1.0f},
                                                    .roughness = 0.9f});

    const ecs::Entity player = reg.create();
    reg.emplace<ecs::Transform3D>(player, ecs::Transform3D{});
    reg.emplace<ecs::MeshComp>(player, ecs::MeshComp{.mesh = cubeMesh,
                                                     .color = {0.35f, 0.85f, 0.45f, 1.0f},
                                                     .roughness = 0.4f});

    const ecs::Entity cake = reg.create();
    reg.emplace<ecs::Transform3D>(cake, ecs::Transform3D{});
    reg.emplace<ecs::MeshComp>(cake, ecs::MeshComp{.mesh = cakeMesh,
                                                   .color = {0.95f, 0.75f, 0.35f, 1.0f},
                                                   .metallic = 0.1f, .roughness = 0.3f});

    // A pool of bonus blocks, all disabled until the score reveals them. Disabling
    // (not destroying) is what keeps them out of extractMeshes with no bookkeeping.
    constexpr int kMaxBonus = 32;
    std::vector<ecs::Entity> bonus;
    for (int i = 0; i < kMaxBonus; ++i) {
        const ecs::Entity e = reg.create();
        reg.emplace<ecs::Transform3D>(e, ecs::Transform3D{});
        reg.emplace<ecs::MeshComp>(e, ecs::MeshComp{.mesh = cubeMesh,
                                                    .color = {0.9f, 0.4f, 0.5f, 1.0f},
                                                    .roughness = 0.5f});
        reg.disable(e);
        bonus.push_back(e);
    }

    renderer::Camera cam;
    cam.mode        = renderer::Camera::Mode::Perspective;
    cam.fovYRadians = radians(50.0f);
    cam.nearZ = 0.1f;
    cam.farZ  = 100.0f;
    cam.up     = {0.0f, 1.0f, 0.0f};
    cam.target = {0.0f, 0.0f, 0.0f};
    cam.position = {0.0f, 12.0f, 13.0f};

    VORTEX_INFO("AlienCake", "WASD/arrows to move, eat the cake. Esc to quit.");

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;
    f32 hopT = 0.0f;
    int lastScore = 0;
    Vec3 playerVisual = game.tileToWorld(game.playerX, game.playerZ, 0.0f);

    std::vector<renderer::MeshInstance> instances;

    while (!window->shouldClose()) {
        clock->tick();
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;
        const f32 dt = static_cast<f32>(clock->deltaTime());

        // Input: one tile per key press.
        if (input->isKeyPressed(pf::Key::A) || input->isKeyPressed(pf::Key::Left))  game.move(-1, 0);
        if (input->isKeyPressed(pf::Key::D) || input->isKeyPressed(pf::Key::Right)) game.move(+1, 0);
        if (input->isKeyPressed(pf::Key::W) || input->isKeyPressed(pf::Key::Up))    game.move(0, -1);
        if (input->isKeyPressed(pf::Key::S) || input->isKeyPressed(pf::Key::Down))  game.move(0, +1);
        // Headless runs auto-play so the frames show real gameplay.
        if (maxFrames != 0 && frameCount % 6 == 5) stepToward(game);

        if (game.score != lastScore) {
            VORTEX_INFO("AlienCake", "Score: %d", game.score);
            lastScore = game.score;
        }

        // Smoothly hop the player toward its tile; bob the cake.
        const Vec3 target = game.tileToWorld(game.playerX, game.playerZ, 0.0f);
        playerVisual = lerp(playerVisual, target, saturate(dt * 12.0f));
        hopT += dt;

        reg.get<ecs::Transform3D>(player).position = {playerVisual.x, 0.0f, playerVisual.z};
        reg.get<ecs::Transform3D>(cake).position =
            game.tileToWorld(game.cakeX, game.cakeZ, 0.35f + 0.1f * std::sin(hopT * 3.0f));
        reg.get<ecs::Transform3D>(cake).rotation = Quat::fromAxisAngle({0, 1, 0}, hopT);

        // Reveal one bonus block per point, stacked in the far corner.
        for (int i = 0; i < kMaxBonus; ++i) {
            if (i < game.score) {
                reg.enable(bonus[i]);
                reg.get<ecs::Transform3D>(bonus[i]).position =
                    game.tileToWorld(0, 0, static_cast<f32>(i) * 1.02f);
            } else {
                reg.disable(bonus[i]);
            }
        }

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) { swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h)); lastW = w; lastH = h; }
        if (w == 0 || h == 0) continue;
        cam.aspect = static_cast<f32>(w) / static_cast<f32>(h);

        renderer::SceneLighting scene;
        scene.sun.direction = {-0.4f, -1.0f, -0.35f};
        scene.sun.intensity = 3.0f;
        scene.shadow.cascadeCount = 4;
        scene.shadow.maxDistance  = 40.0f;
        scene.shadow.resolution   = kShadowRes;

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        instances.clear();
        ecs::extractMeshes(reg, instances);
        mesh.begin(cam, scene);
        mesh.submit(instances.data(), instances.size());

        graph.beginFrame();
        const auto backbuffer = graph.importBackbuffer(frame.backbuffer, frame.width, frame.height);
        const auto sceneHdr   = graph.colorTarget("scene_hdr", frame.width, frame.height, hdrFormat);
        const auto sceneDepth = graph.depthTarget("scene_depth", frame.width, frame.height);
        const auto shadowMap  = graph.depthTarget("shadow_map", kShadowRes, kShadowRes, /*sampled=*/true);

        const f32 clear[4] = {0.05f, 0.06f, 0.09f, 1.0f};
        const rhi::Viewport vp{.x = 0.0f, .y = 0.0f,
                               .width = static_cast<f32>(frame.width), .height = static_cast<f32>(frame.height)};

        graph.addPass("shadow",
            [&](renderer::RenderGraph::PassBuilder& b) { b.writeDepth(shadowMap); },
            [&](rhi::ICommandList& cmd) { mesh.renderShadow(cmd); });
        mesh.setShadowMap(graph.texture(shadowMap));

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

        post.addPasses(graph, sceneHdr, backbuffer, frame.width, frame.height);
        graph.execute(*frame.cmd);
        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    swapchain.reset();
    VORTEX_INFO("AlienCake", "Final score: %d. Goodbye.", game.score);
    return 0;
}
