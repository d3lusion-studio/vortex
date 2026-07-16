#include "world.hpp"

#include "vortex/core/log.hpp"
#include "vortex/core/math/random.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/scene.hpp"

#include <algorithm>

namespace farm {

using namespace vortex;

namespace {

// Tile layers, back to front. The soil sits on the ground and the wet overlay on the
// soil; everything alive is a sprite above them and sorts by depth instead.
constexpr i32 kLayerGround  = -1000;
constexpr i32 kLayerDecor   = -950;
constexpr i32 kLayerSoil    = -900;
constexpr i32 kLayerSoilWet = -890;

// Seed for the decor scatter. Fixed, so the same lawn comes back every season and
// every launch: tufts that teleport when the calendar turns would read as a glitch.
constexpr u32 kDecorSeed = 90210u;

// The tillable field: everything outside it is yard, path or building.
constexpr i32 kFieldX0 = 6;
constexpr i32 kFieldY0 = 12;
constexpr i32 kFieldX1 = 30;
constexpr i32 kFieldY1 = 30;

// Watered soil ships as periwinkle in this pack — a colour no farm has ever been.
// Rather than use it, the wet layer redraws the DRY art under a dark tint, which is
// what "watered" reads as everywhere else in the genre.
const Color kWetTint{0.55f, 0.42f, 0.38f, 1.0f};

}   // namespace

Vec2 World::tileCenter(i32 tx, i32 ty) {
    return {(static_cast<f32>(tx) + 0.5f) * kTile, -(static_cast<f32>(ty) + 0.5f) * kTile};
}

void World::worldToTile(Vec2 world, i32& tx, i32& ty) {
    tx = static_cast<i32>(std::floor(world.x / kTile));
    ty = static_cast<i32>(std::floor(-world.y / kTile));
}

bool World::blocked(i32 tx, i32 ty) const {
    if (!GameState::inBounds(tx, ty)) return true;   // the world edge is a wall
    return m_blocked[static_cast<usize>(ty) * kMapW + tx] != 0;
}

bool World::blockedAt(Vec2 world) const {
    i32 tx = 0, ty = 0;
    worldToTile(world, tx, ty);
    return blocked(tx, ty);
}

bool World::tillable(i32 tx, i32 ty) const {
    if (!GameState::inBounds(tx, ty)) return false;
    return m_tillable[static_cast<usize>(ty) * kMapW + tx] != 0 && !blocked(tx, ty);
}

void World::setBlocked(i32 tx, i32 ty, bool value) {
    if (!GameState::inBounds(tx, ty)) return;
    m_blocked[static_cast<usize>(ty) * kMapW + tx] = value ? 1u : 0u;
}

Vec2 World::binTileCenter() const { return tileCenter(m_binTx, m_binTy); }

Vec2 World::spawnPoint() const {
    return tileCenter(m_house.doorTx, m_house.doorTy + 1);
}

void World::build(ecs::Scene& scene, const Assets& assets, GameState& state) {
    m_blocked.assign(static_cast<usize>(kMapW) * kMapH, 0u);
    m_tillable.assign(static_cast<usize>(kMapW) * kMapH, 0u);

    for (i32 ty = kFieldY0; ty <= kFieldY1; ++ty)
        for (i32 tx = kFieldX0; tx <= kFieldX1; ++tx)
            m_tillable[static_cast<usize>(ty) * kMapW + tx] = 1u;

    // --- Ground ----------------------------------------------------------------
    renderer::TileLayer ground(kMapW, kMapH, {kTile, kTile});
    ground.name    = "ground";
    ground.tileset = assets.groundSheet[0];
    ground.origin  = {0.0f, 0.0f};
    ground.layer   = kLayerGround;
    ground.sampler = renderer::SpriteSampler::NearestClamp;

    // One tile, everywhere. The page's other solid green belongs to a different
    // terrain, and mixing the two does not read as variation — it reads as a
    // checkerboard of flat squares. The variation comes from the decor layer instead.
    Random rng{1337u};
    for (i32 ty = 0; ty < kMapH; ++ty)
        for (i32 tx = 0; tx < kMapW; ++tx)
            ground.setTile(tx, ty, kGrassTile);
    scene.tilemap.addLayer(std::move(ground));

    // --- Decor -----------------------------------------------------------------
    renderer::TileLayer decor(kMapW, kMapH, {kTile, kTile});
    decor.name    = "decor";
    decor.tileset = assets.propsSheet;
    decor.origin  = {0.0f, 0.0f};
    decor.layer   = kLayerDecor;
    decor.sampler = renderer::SpriteSampler::NearestClamp;
    scene.tilemap.addLayer(std::move(decor));

    // --- Soil ------------------------------------------------------------------
    renderer::TileLayer soil(kMapW, kMapH, {kTile, kTile});
    soil.name    = "soil";
    soil.tileset = assets.soilSheet;
    soil.origin  = {0.0f, 0.0f};
    soil.layer   = kLayerSoil;
    soil.sampler = renderer::SpriteSampler::NearestClamp;
    scene.tilemap.addLayer(std::move(soil));

    renderer::TileLayer wet(kMapW, kMapH, {kTile, kTile});
    wet.name    = "soil_wet";
    wet.tileset = assets.soilSheet;
    wet.origin  = {0.0f, 0.0f};
    wet.layer   = kLayerSoilWet;
    wet.tint    = kWetTint;
    wet.sampler = renderer::SpriteSampler::NearestClamp;
    scene.tilemap.addLayer(std::move(wet));

    // --- Buildings -------------------------------------------------------------
    //
    // Source rects were read off the sheet by scanning its alpha for the assembled
    // houses along the bottom; the footprints are deliberately smaller than the art,
    // so the player can walk behind a roof but not through a wall.
    m_house = {.source = {84.0f, 377.0f, 72.0f, 96.0f},
               .tx = 4, .ty = 3, .w = 4, .h = 3, .doorTx = 6, .doorTy = 6};
    m_store = {.source = {180.0f, 377.0f, 124.0f, 96.0f},
               .tx = 32, .ty = 3, .w = 7, .h = 3, .doorTx = 35, .doorTy = 6};

    for (const Building* b : {&m_house, &m_store})
        for (i32 y = 0; y < b->h; ++y)
            for (i32 x = 0; x < b->w; ++x)
                setBlocked(b->tx + x, b->ty + y, true);

    m_binTx = 9;
    m_binTy = 7;
    setBlocked(m_binTx, m_binTy, true);

    // --- Trees -----------------------------------------------------------------
    //
    // Scattered outside the field so they never sit on farmable ground, and blocking
    // only their trunk cell — a tree you cannot walk *under* looks wrong at this scale.
    // The yard: the walkable apron around the house, the bin and the path to the
    // field. Trees are kept out of it, or the farm opens on a thicket.
    const auto inYard = [&](i32 tx, i32 ty) {
        return tx >= m_house.tx - 3 && tx <= m_house.tx + m_house.w + 6 && ty >= 1 &&
               ty <= kFieldY0 - 1;
    };

    m_trees.clear();
    for (i32 attempt = 0; attempt < 400; ++attempt) {
        const i32 tx = rng.range(1, kMapW - 2);
        const i32 ty = rng.range(2, kMapH - 2);
        if (tillable(tx, ty) || blocked(tx, ty) || inYard(tx, ty)) continue;
        // A one-tile margin around the field, so a canopy never hangs over a crop.
        if (tx >= kFieldX0 - 1 && tx <= kFieldX1 + 1 && ty >= kFieldY0 - 1 && ty <= kFieldY1 + 1)
            continue;
        // Trees are 3 tiles tall: spaced any tighter and the grove is a green wall.
        if (std::any_of(m_trees.begin(), m_trees.end(), [&](const Tree& t) {
                return std::abs(t.tx - tx) < 3 && std::abs(t.ty - ty) < 4;
            }))
            continue;

        m_trees.push_back({tx, ty});
        setBlocked(tx, ty, true);
    }

    // Keep the walk from the door to the field clear whatever the scatter decided.
    for (i32 ty = m_house.doorTy; ty <= kFieldY0; ++ty)
        setBlocked(m_house.doorTx, ty, false);

    spawnProps(scene, assets);
    syncFarm(scene, assets, state);
    applySeason(scene, assets, state.season);

    VORTEX_INFO("Farm", "World built: %dx%d tiles, %zu trees, field %dx%d",
                kMapW, kMapH, m_trees.size(), kFieldX1 - kFieldX0 + 1, kFieldY1 - kFieldY0 + 1);
}

void World::applySeason(ecs::Scene& scene, const Assets& assets, Season season) {
    if (renderer::TileLayer* ground = scene.tilemap.layer("ground"))
        ground->tileset = assets.groundSheet[static_cast<usize>(season)];

    // Row 1 of the tree page is the mature tree, one column per season.
    const Rect source{static_cast<f32>(season) * assets.treeSize.x, assets.treeSize.y,
                      assets.treeSize.x, assets.treeSize.y};
    for (const ecs::Entity e : m_treeEntities)
        if (auto* sprite = scene.registry().tryGet<ecs::SpriteComp>(e))
            sprite->uv = renderer::pixelsToUV(source, 288, 192);

    // Re-scatter from the same seed, so only the props' dress changes: the tuft that
    // was by the gate in spring is the same tuft, wearing snow.
    renderer::TileLayer* decor = scene.tilemap.layer("decor");
    if (decor == nullptr) return;

    Random rng{kDecorSeed};
    for (i32 ty = 0; ty < kMapH; ++ty) {
        for (i32 tx = 0; tx < kMapW; ++tx) {
            const bool place = rng.range(0, 6) == 0;
            const auto variant = static_cast<u32>(rng.range(0, kDecorVariants - 1));
            // The field stays bare: a tuft under a crop reads as a weed you cannot pull.
            decor->setTile(tx, ty, (place && !tillable(tx, ty) && !blocked(tx, ty))
                                       ? decorTile(season, variant)
                                       : renderer::kEmptyTile);
        }
    }
}

namespace {

// Anything standing on the ground: anchored at its base and left to the engine's
// depth sort. `ySort` is the whole reason this game no longer computes a draw order.
ecs::Entity spawnStanding(ecs::Scene& scene, Vec2 base, Vec2 size, rhi::TextureHandle texture,
                          Rect uv) {
    const ecs::Entity e = scene.spawn();
    scene.registry().get<ecs::Transform2D>(e).position = base;
    scene.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = texture,
        .uv      = uv,
        .size    = size,
        .ySort   = true,
        .anchor  = {0.5f, 0.0f},
        .sampler = renderer::SpriteSampler::NearestClamp});
    return e;
}

// The world position of the bottom-centre of a tile's cell — where a prop standing on
// that tile has its feet.
[[nodiscard]] Vec2 tileBase(i32 tx, i32 ty) {
    return {(static_cast<f32>(tx) + 0.5f) * kTile, -static_cast<f32>(ty + 1) * kTile};
}

}   // namespace

void World::spawnProps(ecs::Scene& scene, const Assets& assets) {
    // Buildings, anchored to the bottom-centre of their footprint so the art rises out
    // of the tiles it blocks instead of being centred on them.
    for (const Building* b : {&m_house, &m_store}) {
        const Vec2 base{(static_cast<f32>(b->tx) + static_cast<f32>(b->w) * 0.5f) * kTile,
                        -static_cast<f32>(b->ty + b->h) * kTile};
        spawnStanding(scene, base, {b->source.width, b->source.height}, assets.houseTex,
                      renderer::pixelsToUV(b->source, 688, 480));
    }

    if (assets.binTex.valid())
        spawnStanding(scene, tileBase(m_binTx, m_binTy), {kTile, kTile}, assets.binTex,
                      assets.binUV);

    // Trees keep their handles: their UV changes every season.
    m_treeEntities.clear();
    m_treeEntities.reserve(m_trees.size());
    for (const Tree& tree : m_trees)
        m_treeEntities.push_back(spawnStanding(scene, tileBase(tree.tx, tree.ty),
                                               assets.treeSize, assets.treeTex, kFullUV));
}

void World::syncFarm(ecs::Scene& scene, const Assets& assets, const GameState& state) {
    renderer::TileLayer* soil = scene.tilemap.layer("soil");
    renderer::TileLayer* wet  = scene.tilemap.layer("soil_wet");
    if (soil == nullptr || wet == nullptr) return;

    m_cropEntities.resize(static_cast<usize>(kMapW) * kMapH);

    // Soil, autotiled. Each layer rounds itself off against its OWN terrain — the wet
    // overlay borders the watered tiles, not the tilled ones — which is what makes a
    // half-watered patch read as two shapes rather than one square with a stripe.
    const renderer::BlobSet set = soilBlobSet();
    renderer::applyBlobSet(*soil, set, [&](i32 x, i32 y) {
        return GameState::inBounds(x, y) && state.tile(x, y).tilled;
    });
    renderer::applyBlobSet(*wet, set, [&](i32 x, i32 y) {
        if (!GameState::inBounds(x, y)) return false;
        const FarmTile& t = state.tile(x, y);
        return t.tilled && t.watered;
    });

    for (i32 ty = 0; ty < kMapH; ++ty) {
        for (i32 tx = 0; tx < kMapW; ++tx) {
            const FarmTile& t = state.tile(tx, ty);
            ecs::Entity& e = m_cropEntities[static_cast<usize>(ty) * kMapW + tx];

            if (t.crop < 0) {
                if (e.valid()) {
                    scene.destroy(e);
                    e = {};
                }
                continue;
            }

            const CropArt& art = assets.crops[static_cast<usize>(t.crop)];
            if (!e.valid())
                e = spawnStanding(scene, tileBase(tx, ty), art.frameSize, art.texture, kFullUV);

            // A living plant grows and a dead one greys out; both are the same entity
            // wearing a different frame, so the sprite is rewritten rather than respawned.
            auto& sprite   = scene.registry().get<ecs::SpriteComp>(e);
            sprite.texture = art.texture;
            sprite.uv      = art.stageUV[cropStage(assets, t)];
            sprite.color   = t.dead ? Vec4{0.45f, 0.38f, 0.30f, 1.0f}
                                    : Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        }
    }
}

u32 cropStage(const Assets& assets, const FarmTile& tile) {
    if (tile.crop < 0) return 0;
    const CropArt& art    = assets.crops[static_cast<usize>(tile.crop)];
    const CropDef& def    = cropDefs()[static_cast<usize>(tile.crop)];
    const auto     stages = static_cast<i32>(art.stageUV.size());
    if (stages <= 1) return 0;

    if (tile.age >= def.growthDays || tile.everRipe) return static_cast<u32>(stages - 1);
    // Spread the intermediate stages over the growing days; the last stage is
    // reserved for "ready", so a crop never looks harvestable before it is.
    const i32 stage = tile.age * (stages - 1) / std::max(1, def.growthDays);
    return static_cast<u32>(std::clamp(stage, 0, stages - 2));
}

bool cropReady(const FarmTile& tile) {
    if (tile.crop < 0 || tile.dead) return false;
    return tile.age >= cropDefs()[static_cast<usize>(tile.crop)].growthDays;
}

i32 advanceDay(ecs::Scene& scene, const Assets& assets, World& world, GameState& state) {
    // --- Ship whatever is in the bin ------------------------------------------
    i32 earned  = 0;
    i32 shipped = 0;
    for (const Slot& slot : state.shipped) {
        if (slot.empty() || !isProduce(slot.id)) continue;
        const CropDef& def = cropDefs()[static_cast<usize>(cropOfProduce(slot.id))];
        earned  += def.sellPrice * slot.count;
        shipped += slot.count;
    }
    state.shipped.clear();
    state.money             += earned;
    state.lastNightEarnings  = earned;
    state.lastNightShipped   = shipped;

    // --- Turn the calendar -----------------------------------------------------
    const Season previous = state.season;
    if (++state.day > kDaysPerSeason) {
        state.day = 1;
        if (state.season == Season::Winter) {
            state.season = Season::Spring;
            ++state.year;
        } else {
            state.season = static_cast<Season>(static_cast<u8>(state.season) + 1);
        }
    }
    const bool seasonTurned = previous != state.season;

    // --- Grow ------------------------------------------------------------------
    for (FarmTile& tile : state.tiles) {
        if (tile.crop < 0) {
            tile.watered = false;
            continue;
        }

        const CropDef& def = cropDefs()[static_cast<usize>(tile.crop)];

        // A crop caught by the turn of the season dies where it stands. This is the
        // rule that makes the calendar matter rather than decorate.
        if (seasonTurned && def.season != state.season) {
            tile.dead = true;
        }

        if (!tile.dead && tile.watered) ++tile.age;
        if (tile.age >= def.growthDays) tile.everRipe = true;
        tile.watered = false;
    }

    state.clock = 0.0f;
    if (seasonTurned) world.applySeason(scene, assets, state.season);
    world.syncFarm(scene, assets, state);

    return earned;
}

}
