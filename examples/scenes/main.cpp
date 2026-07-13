// Persistence and multi-scene: build a level, save it to JSON, reload it, stamp a
// prefab into it, and flip between two scenes at runtime.
//
// The round-trip is checked in-process on start-up: the entity count, a sampled
// transform and the tilemap all have to come back identical, or the example says so.

#include "vortex/app/app.hpp"
#include "vortex/core/json.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/serialize.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/tilemap.hpp"
#include "vortex/rhi/device.hpp"

#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

constexpr const char* kScenePath = "level1.scene.json";
constexpr u32         kEntities  = 500;

// A prefab: a parent with two orbiting children. Built once, saved, then stamped.
json::Value buildTurretPrefab(app::App& a, rhi::TextureHandle texture) {
    ecs::Registry& reg = a.registry();

    const ecs::Entity base = a.scene().spawn();
    reg.emplace<ecs::SpriteComp>(base, ecs::SpriteComp{
        .texture = texture, .color = Color::fromRgb(0x4C6EF5), .size = {40.0f, 40.0f}, .layer = 2});

    for (int i = 0; i < 2; ++i) {
        const ecs::Entity child = a.scene().spawn();
        reg.get<ecs::Transform2D>(child).position = {i == 0 ? 34.0f : -34.0f, 0.0f};
        reg.emplace<ecs::SpriteComp>(child, ecs::SpriteComp{
            .texture = texture, .color = Color::fromRgb(0xFFD166),
            .size = {16.0f, 16.0f}, .layer = 3});
        reg.emplace<ecs::Parent>(child, ecs::Parent{base});
    }

    const json::Value prefab = ecs::savePrefab(a.scene(), base, a.serializeContext());

    // The template was only ever scaffolding — the scene keeps the instances, not it.
    a.scene().destroy(base);
    return prefab;
}

}

int main() {
    app::AppConfig config;
    config.title = "Vortex Scenes";
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);

    app.onStart([](app::App& a) {
        const rhi::TextureHandle white = a.whiteTexture();

        // --- Author a level in the active scene ("main" exists from the start) ---
        Random rng{99u};
        for (u32 i = 0; i < kEntities; ++i) {
            const ecs::Entity e = a.scene().spawn();
            a.registry().get<ecs::Transform2D>(e).position =
                rng.insideRect({-600.0f, -350.0f}, {600.0f, 350.0f});
            a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
                .texture = white,
                .color   = Color::fromHsv(rng.range(0.0f, 360.0f), 0.6f, 1.0f),
                .size    = Vec2::one() * rng.range(6.0f, 16.0f)});
            a.registry().emplace<ecs::Velocity>(e, ecs::Velocity{rng.insideUnitCircle() * 40.0f});
        }

        renderer::TileLayer floorLayer(24, 8, {48.0f, 48.0f});
        floorLayer.name    = "floor";
        floorLayer.tileset = {.texture = white, .textureWidth = 1, .textureHeight = 1,
                              .columns = 1, .rows = 1};
        floorLayer.origin  = {-576.0f, -200.0f};
        floorLayer.layer   = -1;
        floorLayer.tint    = Color{0.18f, 0.20f, 0.28f, 1.0f};
        for (i32 tx = 0; tx < 24; ++tx) floorLayer.setTile(tx, 7, 1);
        a.scene().tilemap.setTileFlags(1, renderer::TileSolid);
        a.scene().tilemap.addLayer(std::move(floorLayer));

        // Sample something we can compare across the round-trip.
        const usize entitiesBefore = a.registry().aliveCount();
        Vec2        sampleBefore{};
        a.registry().view<ecs::Transform2D, ecs::SpriteComp>(
            [&](ecs::Entity, ecs::Transform2D& t, ecs::SpriteComp&) { sampleBefore = t.position; });

        // --- Save, wipe, reload ---
        if (!a.saveScene(kScenePath)) { a.quit(); return; }
        if (!a.loadScene(kScenePath)) { a.quit(); return; }

        const usize entitiesAfter = a.registry().aliveCount();
        Vec2        sampleAfter{};
        a.registry().view<ecs::Transform2D, ecs::SpriteComp>(
            [&](ecs::Entity, ecs::Transform2D& t, ecs::SpriteComp&) { sampleAfter = t.position; });

        const bool sameCount = entitiesBefore == entitiesAfter;
        const bool sameXform = length(sampleAfter - sampleBefore) < 0.001f;
        const bool sameTiles = a.scene().tilemap.layers().size() == 1
                            && a.scene().tilemap.solid({-576.0f + 24.0f, -200.0f - 7.5f * 48.0f});

        VORTEX_INFO("Scenes", "round-trip: entities %zu -> %zu [%s], transform [%s], tilemap [%s]",
                    entitiesBefore, entitiesAfter,
                    sameCount ? "ok" : "MISMATCH",
                    sameXform ? "ok" : "MISMATCH",
                    sameTiles ? "ok" : "MISMATCH");

        // --- Prefabs: author once, stamp many ---
        const json::Value turret = buildTurretPrefab(a, white);
        for (int i = 0; i < 5; ++i)
            ecs::instantiate(a.scene(), turret,
                             {-400.0f + static_cast<f32>(i) * 200.0f, 260.0f},
                             a.serializeContext());

        // --- A second scene, with nothing in common with the first ---
        ecs::Scene& menu = a.scenes().create("menu");
        menu.camera.zoom = 1.0f;
        for (int i = 0; i < 3; ++i) {
            const ecs::Entity e = menu.spawn();
            menu.registry().get<ecs::Transform2D>(e).position = {0.0f, 120.0f - 120.0f * i};
            menu.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
                .texture = white, .color = Color::gray(0.35f + 0.2f * static_cast<f32>(i)),
                .size = {360.0f, 64.0f}});
        }

        // Drift the level's sprites, so it is obvious which scene is on screen.
        a.scene().addSystem([](ecs::Registry& r, f32 dt) {
            r.view<ecs::Transform2D, ecs::Velocity>(
                [dt](ecs::Entity, ecs::Transform2D& t, ecs::Velocity& v) {
                    t.position += v.value * dt;
                    if (std::fabs(t.position.x) > 600.0f) v.value.x = -v.value.x;
                    if (std::fabs(t.position.y) > 350.0f) v.value.y = -v.value.y;
                });
        });

        VORTEX_INFO("Scenes", "TAB switches main <-> menu. F5 saves, F9 reloads. ESC quits.");
    });

    app.onUpdate([](app::App& a, f32) {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();

        // The switch is queued and applied at the top of the next frame, so the
        // system that is mid-iteration right now never sees the world change.
        if (in.isKeyPressed(pf::Key::Tab))
            a.scenes().requestSwitch(a.scenes().activeName() == "main" ? "menu" : "main");

        if (in.isKeyPressed(pf::Key::F5)) a.saveScene(kScenePath);
        if (in.isKeyPressed(pf::Key::F9)) a.loadScene(kScenePath);
    });

    return app.run();
}
