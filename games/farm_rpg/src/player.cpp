#include "player.hpp"

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/scene.hpp"
#include "vortex/platform/input.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace farm {

using namespace vortex;

namespace {

constexpr f32 kWalkSpeed = 62.0f;    // world units/second — a shade under 4 tiles
constexpr f32 kRunSpeed  = 112.0f;

// The player's footprint. Much narrower than the 32px sprite: the art is mostly
// head and hat, and a collider the size of the image cannot fit down a 1-tile path.
constexpr f32 kBodyHalfW = 5.0f;
constexpr f32 kBodyHalfH = 3.5f;

// Energy per swing. Tuned so a full bar is a solid day's work and not much more.
constexpr f32 kEnergyTill    = 2.0f;
constexpr f32 kEnergyWater   = 1.5f;
constexpr f32 kEnergyHarvest = 0.5f;

// Feet, not centre: the character stands at the bottom of its 32x32 frame, so the
// anchor has to sit where the shoes are or the sprite floats over its own shadow.
constexpr Vec2 kFootAnchor{0.5f, 0.125f};

[[nodiscard]] bool freeAt(const World& world, Vec2 feet) {
    const Vec2 corners[4] = {
        {feet.x - kBodyHalfW, feet.y - kBodyHalfH},
        {feet.x + kBodyHalfW, feet.y - kBodyHalfH},
        {feet.x - kBodyHalfW, feet.y + kBodyHalfH},
        {feet.x + kBodyHalfW, feet.y + kBodyHalfH},
    };
    for (const Vec2& c : corners)
        if (world.blockedAt(c)) return false;
    return true;
}

// Move on each axis separately so running into a wall diagonally slides along it
// instead of stopping dead — the difference between "solid" and "sticky".
void moveWithCollision(const World& world, Player& player, Vec2 delta) {
    const Vec2 start = player.position;

    Vec2 next{start.x + delta.x, start.y};
    if (freeAt(world, next)) player.position.x = next.x;

    next = {player.position.x, start.y + delta.y};
    if (freeAt(world, next)) player.position.y = next.y;
}

[[nodiscard]] renderer::AnimationHandle clipFor(const Assets& assets, const GameState& state) {
    const Player& p   = state.player;
    const auto    dir = static_cast<usize>(p.facing);

    if (p.useTimer > 0.0f) {
        if (p.useItem == kToolHoe) return assets.clips.hoe.byDir[dir];
        if (p.useItem == kToolCan) return assets.clips.water.byDir[dir];
        return assets.clips.sickle.byDir[dir];
    }
    if (!p.moving) return assets.clips.idle.byDir[dir];
    return p.running ? assets.clips.run.byDir[dir] : assets.clips.walk.byDir[dir];
}

// How long the swing for this item lasts, so the lock matches the clip.
[[nodiscard]] f32 swingDuration(const Assets& assets, ItemId item) {
    if (item == kToolHoe) return assets.clips.hoeDuration;
    if (item == kToolCan) return assets.clips.waterDuration;
    return assets.clips.sickleDuration;
}

// What the selected item would do to the tile in front, if anything. Deciding this up
// front is what lets a swing start only when it will actually land.
[[nodiscard]] ToolAction plannedAction(const World& world, const GameState& state, i32 tx,
                                       i32 ty) {
    if (!GameState::inBounds(tx, ty)) return ToolAction::None;

    const FarmTile& tile = state.tile(tx, ty);
    const ItemId    held = state.inventory.slots[static_cast<usize>(state.inventory.selected)].id;

    // Picking a ripe crop beats whatever is in hand: it is what the player means.
    if (cropReady(tile)) return ToolAction::Harvest;
    if (tile.dead && tile.crop >= 0 && held == kToolScythe) return ToolAction::Harvest;

    if (held == kToolHoe && world.tillable(tx, ty) && !tile.tilled) return ToolAction::Till;
    if (held == kToolCan && tile.tilled && !tile.watered) return ToolAction::Water;
    if (isSeed(held) && tile.tilled && tile.crop < 0) return ToolAction::Plant;
    return ToolAction::None;
}

}   // namespace

void facingTile(const GameState& state, i32& tx, i32& ty) {
    const Vec2 ahead = state.player.position + dirToVec(state.player.facing) * kTile;
    World::worldToTile(ahead, tx, ty);
}

void spawnPlayer(ecs::Scene& scene, const Assets& assets, GameState& state, const World& world) {
    const ecs::Entity e = scene.spawn();
    state.player.entity = e;
    state.player.position = world.spawnPoint();

    scene.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .size    = {kCharPx, kCharPx},
        .ySort   = true,   // the engine sorts the player against the trees and crops
        .anchor  = kFootAnchor,
        .sampler = renderer::SpriteSampler::NearestClamp});
    scene.registry().emplace<ecs::SpriteAnimator>(e, ecs::SpriteAnimator{
        .clip = assets.clips.idle.byDir[static_cast<usize>(Dir::Down)]});
}

void applyToolUse(ecs::Scene& scene, const Assets& assets, World& world, GameState& state) {
    Player& p = state.player;
    if (p.useAction == ToolAction::None || !GameState::inBounds(p.useTx, p.useTy)) return;

    const i32  tx = p.useTx;
    const i32  ty = p.useTy;
    FarmTile&  tile = state.tile(tx, ty);
    Inventory& inv  = state.inventory;

    switch (p.useAction) {
        case ToolAction::Till:
            tile.tilled = true;
            p.energy   -= kEnergyTill;
            break;

        case ToolAction::Water:
            tile.watered = true;
            p.energy    -= kEnergyWater;
            break;

        case ToolAction::Plant: {
            const ItemId seed = p.useSeed;
            if (!isSeed(seed)) return;
            const i32      crop = cropOfSeed(seed);
            const CropDef& def  = cropDefs()[static_cast<usize>(crop)];
            if (def.season != state.season) {
                state.note(std::string(def.name) + " seeds only grow in " + seasonName(def.season));
                return;
            }
            if (!inv.remove(seed, 1)) return;
            tile.crop = static_cast<i8>(crop);
            tile.age  = 0;
            tile.dead = false;
            break;
        }

        case ToolAction::Harvest: {
            if (tile.dead) {
                tile = FarmTile{.tilled = tile.tilled};
                break;
            }

            const i32      crop = tile.crop;
            const CropDef& def  = cropDefs()[static_cast<usize>(crop)];
            if (inv.add(produceOfCrop(crop), 1) != 0) {
                state.note("Your bag is full");
                return;
            }
            p.energy -= kEnergyHarvest;
            state.note(std::string("Harvested ") + def.name);

            if (def.regrowDays > 0) {
                // Regrowing crops keep the plant and rewind just far enough that the
                // next fruit costs regrowDays, not a whole growth cycle.
                tile.age = std::max(0, def.growthDays - def.regrowDays);
            } else {
                tile.crop     = -1;
                tile.age      = 0;
                tile.everRipe = false;   // the plant is gone; the next one starts as a seed
            }
            break;
        }

        case ToolAction::None: break;
    }

    p.energy = std::max(0.0f, p.energy);

    // One sync for every action, rather than one per branch: the branches that changed
    // the grid without refreshing it (sowing) were a bug waiting for a witness.
    world.syncFarm(scene, assets, state);
}

void updatePlayer(app::App& app, const Assets& assets, World& world, GameState& state, f32 dt) {
    Player&             p  = state.player;
    pf::IInputProvider& in = app.input();

    // A swing owns the player until it finishes; the effect lands halfway through,
    // which is where the tool meets the ground in every one of these clips.
    if (p.useTimer > 0.0f) {
        const f32 total = swingDuration(assets, p.useItem);
        p.useTimer -= dt;
        if (!p.useApplied && p.useTimer <= total * 0.5f) {
            p.useApplied = true;
            applyToolUse(app.scene(), assets, world, state);
        }
        if (p.useTimer <= 0.0f) {
            p.useTimer  = 0.0f;
            p.useItem   = kItemNone;
            p.useAction = ToolAction::None;
            p.useSeed   = kItemNone;
        }
        p.moving = false;
    } else {
        Vec2 move{0.0f, 0.0f};
        if (in.isKeyDown(pf::Key::A) || in.isKeyDown(pf::Key::Left))  move.x -= 1.0f;
        if (in.isKeyDown(pf::Key::D) || in.isKeyDown(pf::Key::Right)) move.x += 1.0f;
        if (in.isKeyDown(pf::Key::W) || in.isKeyDown(pf::Key::Up))    move.y += 1.0f;
        if (in.isKeyDown(pf::Key::S) || in.isKeyDown(pf::Key::Down))  move.y -= 1.0f;

        p.moving  = move.x != 0.0f || move.y != 0.0f;
        p.running = p.moving && (in.isKeyDown(pf::Key::LeftShift) || in.isKeyDown(pf::Key::RightShift));

        if (p.moving) {
            // Vertical wins ties, so a diagonal walk still picks one sensible facing
            // rather than flickering between two.
            if (move.y != 0.0f) p.facing = move.y > 0.0f ? Dir::Up : Dir::Down;
            else                p.facing = move.x > 0.0f ? Dir::Right : Dir::Left;

            const f32 speed = p.running ? kRunSpeed : kWalkSpeed;
            moveWithCollision(world, p, normalize(move) * speed * dt);
        }
    }

    // --- Drive the animator ----------------------------------------------------
    ecs::Registry& reg = app.registry();
    if (auto* animator = reg.tryGet<ecs::SpriteAnimator>(p.entity)) {
        const renderer::AnimationHandle want = clipFor(assets, state);
        if (animator->clip != want) {
            animator->play(want);
        } else if (p.useTimer <= 0.0f && animator->finished) {
            animator->restart();
        }
    }
    if (auto* transform = reg.tryGet<ecs::Transform2D>(p.entity))
        transform->position = p.position;
}

// Start a swing if the held item has something to do with the tile ahead. Everything the
// swing will need on impact is committed here — the action, the target tile and the seed
// — because the half-second the animation takes is long enough for the player to change
// hotbar slot, turn, or walk away.
void beginToolUse(const Assets& assets, const World& world, GameState& state) {
    Player& p = state.player;
    if (p.useTimer > 0.0f) return;

    i32 tx = 0, ty = 0;
    facingTile(state, tx, ty);
    const ToolAction action = plannedAction(world, state, tx, ty);
    if (action == ToolAction::None) return;

    if (p.energy <= 0.0f) {
        state.note("Too exhausted. Go to bed.");
        return;
    }

    // Harvesting swings the sickle and sowing bends down over the soil, whatever is in
    // hand; tilling and watering swing the tool itself.
    p.useItem = (action == ToolAction::Harvest) ? kToolScythe
              : (action == ToolAction::Plant)   ? kToolHoe
                                                : state.inventory.selectedSlot().id;
    p.useAction  = action;
    p.useTx      = tx;
    p.useTy      = ty;
    p.useSeed    = (action == ToolAction::Plant) ? state.inventory.selectedSlot().id : kItemNone;
    p.useTimer   = swingDuration(assets, p.useItem);
    p.useApplied = false;
}

}
