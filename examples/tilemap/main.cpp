// A tiled level: a parallax star layer, a solid ground layer, and a player that
// collides against the tiles by querying tile flags — no physics engine involved.
//
// The map is 512x64 tiles (32k cells). Only the screenful under the camera is ever
// walked, so panning across the whole level costs the same as standing still.

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/math.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/renderer/tilemap.hpp"
#include "vortex/rhi/device.hpp"

#include <cmath>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

constexpr u32 kTilePx    = 16;      // one tile in the source texture
constexpr u32 kTileTypes = 3;
constexpr f32 kTile      = 32.0f;   // world units per tile
constexpr u32 kMapW      = 512;
constexpr u32 kMapH      = 64;

// Tile ids are 1-based — 0 means empty — so these index frames 0..2 of the sheet.
enum : renderer::TileId { kDirt = 1, kGrass = 2, kStar = 3 };

// A 3-frame strip painted in code, so the example needs no art on disk.
rhi::TextureHandle makeTileset(rhi::IGraphicsDevice& device) {
    const u32 width = kTilePx * kTileTypes;
    std::vector<u8> px(static_cast<usize>(width) * kTilePx * 4, 0);

    const auto put = [&](u32 frame, u32 x, u32 y, u8 r, u8 g, u8 b, u8 a) {
        u8* p = &px[(static_cast<usize>(y) * width + frame * kTilePx + x) * 4];
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    };

    for (u32 y = 0; y < kTilePx; ++y) {
        for (u32 x = 0; x < kTilePx; ++x) {
            const bool edge = x == 0 || y == 0;                    // grid seam
            put(0, x, y, edge ? 88 : 122, edge ? 58 : 82, edge ? 40 : 56, 255);   // dirt

            const bool cap = y < 5;                                // grass on top
            put(1, x, y, cap ? 96 : 122, cap ? 170 : 82, cap ? 74 : 56, 255);

            const Vec2 d{static_cast<f32>(x) - 8.0f, static_cast<f32>(y) - 8.0f};
            put(2, x, y, 255, 255, 255, length(d) < 2.0f ? 200 : 0);              // star
        }
    }
    return device.createTexture({.width = width, .height = kTilePx, .debugName = "tileset"},
                                px.data());
}

// The player. Lives outside the callbacks so fixed-update can move it and render
// can draw it without either owning the other.
struct Player {
    Vec2 position{200.0f, 1400.0f};
    Vec2 velocity{};
    bool grounded = false;

    static constexpr Vec2 kSize{24.0f, 40.0f};
};

}

int main() {
    app::AppConfig config;
    config.title = "Vortex Tilemap";
    if (const char* frames = std::getenv("VORTEX_MAX_FRAMES"))
        config.maxFrames = std::strtoull(frames, nullptr, 10);

    app::App app(config);
    Player   player;

    app.onStart([](app::App& a) {
        const renderer::SpriteSheet sheet{.texture       = makeTileset(a.device()),
                                          .textureWidth  = kTilePx * kTileTypes,
                                          .textureHeight = kTilePx,
                                          .columns       = kTileTypes,
                                          .rows          = 1};

        renderer::Tilemap& map = a.scene().tilemap;
        map.setTileFlags(kDirt,  renderer::TileSolid);
        map.setTileFlags(kGrass, renderer::TileSolid);
        // kStar carries no flags: it is decoration, and nothing collides with it.

        const f32 topY = static_cast<f32>(kMapH) * kTile;

        // Background: sparse stars, drifting at a third of the camera's speed.
        renderer::TileLayer stars(kMapW, kMapH, {kTile, kTile});
        stars.name     = "stars";
        stars.tileset  = sheet;
        stars.origin   = {0.0f, topY};
        stars.parallax = {0.3f, 0.3f};
        stars.layer    = -10;
        stars.tint     = Color{0.7f, 0.75f, 1.0f, 1.0f};
        Random rng{7u};
        for (u32 i = 0; i < 4000; ++i)
            stars.setTile(rng.range(0, static_cast<i32>(kMapW) - 1),
                          rng.range(0, static_cast<i32>(kMapH) - 1), kStar);
        map.addLayer(std::move(stars));

        // Ground: a rolling surface with the occasional gap to fall into.
        renderer::TileLayer ground(kMapW, kMapH, {kTile, kTile});
        ground.name    = "ground";
        ground.tileset = sheet;
        ground.origin  = {0.0f, topY};
        ground.layer   = 0;
        for (u32 tx = 0; tx < kMapW; ++tx) {
            if (tx % 37 > 32) continue;   // gaps
            const i32 top = 44 + static_cast<i32>(std::sin(static_cast<f32>(tx) * 0.06f) * 4.0f);
            ground.setTile(static_cast<i32>(tx), top, kGrass);
            for (i32 ty = top + 1; ty < static_cast<i32>(kMapH); ++ty)
                ground.setTile(static_cast<i32>(tx), ty, kDirt);
        }
        map.addLayer(std::move(ground));

        // The same grid, merged into as few boxes as possible. Nothing here consumes
        // them — this is what you would hand PhysicsWorld as static level geometry.
        VORTEX_INFO("Tilemap", "%ux%u tiles -> %zu merged solid boxes. A/D run, "
                               "SPACE jump, ESC quits.",
                    kMapW, kMapH, map.solidBoxes("ground").size());
    });

    // Gravity, then a downward probe against TileSolid. This is what tile flags buy:
    // a character controller that never allocates and never talks to a physics world.
    app.onFixedUpdate([&player](app::App& a, f32 dt) {
        pf::IInputProvider& in = a.input();
        if (in.isKeyPressed(pf::Key::Escape)) a.quit();

        const renderer::Tilemap&  map    = a.scene().tilemap;
        const renderer::TileLayer* ground = map.layer("ground");

        f32 move = 0.0f;
        if (in.isKeyDown(pf::Key::A)) move -= 1.0f;
        if (in.isKeyDown(pf::Key::D)) move += 1.0f;
        player.velocity.x = move * 320.0f;

        if (player.grounded && in.isKeyDown(pf::Key::Space)) player.velocity.y = 620.0f;
        player.velocity.y -= 1600.0f * dt;

        player.position += player.velocity * dt;

        // Only a falling player lands; a rising one passes through, so jumps work.
        const f32 feet = player.position.y - Player::kSize.y * 0.5f;
        player.grounded = false;
        if (player.velocity.y <= 0.0f && map.solid({player.position.x, feet})) {
            i32 tx = 0, ty = 0;
            ground->worldToTile({player.position.x, feet}, tx, ty);
            player.position.y = ground->tileCenter(tx, ty).y + ground->tileSize.y * 0.5f
                              + Player::kSize.y * 0.5f;
            player.velocity.y = 0.0f;
            player.grounded   = true;
        }

        if (player.position.y < -200.0f) player = {};   // fell in a gap; respawn

        a.camera().position = damp(a.camera().position, player.position, 8.0f, dt);
    });

    // The player goes into the same batch as the tiles, so the whole frame is one
    // sorted draw list — tiles, sprite and all.
    app.onRender([&player](app::App& a, renderer::SpriteBatch& batch) {
        batch.drawSprite(a.whiteTexture(), player.position, Player::kSize,
                         Color::fromRgb(0xFFD166), kFullUV, /*layer=*/10);
    });

    return app.run();
}
