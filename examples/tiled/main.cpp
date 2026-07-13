// The same kind of level as examples/tilemap, except nothing about it is in this
// file: the tiles, the tileset, the collision flags and the player's spawn point all
// come out of level.tmj, drawn in Tiled.
//
// What the code still owns is behaviour — gravity, jumping, what "pickup" means.
// That split is the whole point: data says where the world is, code says what it does.

#include "vortex/app/app.hpp"
#include "vortex/asset/tiled.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/renderer/tilemap.hpp"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace vortex;

namespace {

struct Player {
    Vec2 position{};
    Vec2 velocity{};
    Vec2 size{16.0f, 24.0f};
    bool grounded = false;
};

struct Pickup {
    Vec2 position{};
    Vec2 size{};
    i32  value = 0;
    bool taken = false;
};

}

int main() {
    app::AppConfig config;
    config.title = "Vortex Tiled";
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);

    Player              player;
    std::vector<Pickup> pickups;
    i32                 score = 0;

    // Outlives onStart because the watcher keeps re-importing for as long as the game
    // runs: edit level.tmj in Tiled, hit Ctrl+S, and the running game has the new level.
    std::unique_ptr<assets::TiledWatcher> level;

    app.onStart([&](app::App& a) {
        assets::TiledImportOptions options;
        options.unitsPerPixel = 2.0f;   // 16px art, 32-unit tiles
        options.origin        = {0.0f, 320.0f};

        // A reload re-runs the objects too, so anything derived from the last import
        // goes first. Without this, every save would duplicate the pickups.
        options.onBeginImport = [&] { pickups.clear(); };

        // Object layers are how a map says more than "these tiles are here". The
        // importer hands each object back; what they mean is this game's business.
        options.onObject = [&](const assets::TiledObject& o) {
            if (o.type == "spawn") {
                player.position = o.bounds.center();
                player.size     = o.bounds.size();
                player.velocity = {};
            } else if (o.type == "pickup") {
                pickups.push_back({.position = o.bounds.center(),
                                   .size     = o.bounds.size(),
                                   .value    = (*o.properties)["value"].asI32(10)});
            }
        };

        const std::string path = (std::filesystem::path(VORTEX_TILED_DIR) / "level.tmj").string();
        level = std::make_unique<assets::TiledWatcher>(path, std::move(options));

        if (!level->load(a.scene().tilemap, a.assets(), a.fileSystem())) {
            a.quit();
            return;
        }

        VORTEX_INFO("Tiled", "spawn at (%.0f, %.0f), %zu pickup(s), %zu merged solid box(es). "
                             "A/D run, SPACE jump, ESC quits. Edit level.tmj to reload live.",
                    player.position.x, player.position.y, pickups.size(),
                    a.scene().tilemap.solidBoxes("ground").size());
    });

    app.onUpdate([&](app::App& a, f32) {
        if (level && level->poll(a.scene().tilemap, a.assets(), a.fileSystem()))
            VORTEX_INFO("Tiled", "level reloaded: %zu pickup(s), %zu solid box(es)",
                        pickups.size(), a.scene().tilemap.solidBoxes("ground").size());
    });

    app.onFixedUpdate([&](app::App& a, f32 dt) {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();

        const renderer::Tilemap&   map    = a.scene().tilemap;
        const renderer::TileLayer* ground = map.layer("ground");
        if (ground == nullptr) return;

        f32 move = 0.0f;
        if (in.isKeyDown(pf::Key::A)) move -= 1.0f;
        if (in.isKeyDown(pf::Key::D)) move += 1.0f;
        player.velocity.x = move * 260.0f;

        if (player.grounded && in.isKeyDown(pf::Key::Space)) player.velocity.y = 620.0f;
        player.velocity.y -= 1600.0f * dt;
        player.position   += player.velocity * dt;

        // Tiles ticked "solid" in Tiled land here, with no code naming a single tile id.
        const f32 feet = player.position.y - player.size.y * 0.5f;
        player.grounded = false;
        if (player.velocity.y <= 0.0f && map.solid({player.position.x, feet})) {
            i32 tx = 0, ty = 0;
            ground->worldToTile({player.position.x, feet}, tx, ty);
            player.position.y = ground->tileCenter(tx, ty).y + ground->tileSize.y * 0.5f
                              + player.size.y * 0.5f;
            player.velocity.y = 0.0f;
            player.grounded   = true;
        }

        if (player.position.y < -200.0f) {   // fell through the gap
            player.position = {96.0f, 200.0f};
            player.velocity = {};
        }

        for (Pickup& p : pickups) {
            if (p.taken) continue;
            if (length(p.position - player.position) > 24.0f) continue;
            p.taken = true;
            score  += p.value;
            VORTEX_INFO("Tiled", "picked up %d, score %d", p.value, score);
        }

        a.camera().position = damp(a.camera().position, player.position, 8.0f, dt);
    });

    app.onRender([&](app::App& a, renderer::SpriteBatch& batch) {
        for (const Pickup& p : pickups)
            if (!p.taken)
                batch.drawSprite(a.whiteTexture(), p.position, p.size, Color::fromRgb(0xFFD166),
                                 kFullUV, /*layer=*/9);

        batch.drawSprite(a.whiteTexture(), player.position, player.size,
                         Color::fromRgb(0x4CC9F0), kFullUV, /*layer=*/10);
    });

    return app.run();
}
