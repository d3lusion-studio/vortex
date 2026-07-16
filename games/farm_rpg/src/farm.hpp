// Core types shared by every part of the farm game: the calendar, the tile grid,
// the item vocabulary and the one GameState everything hangs off.
#pragma once

#include "vortex/core/math/color.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <array>
#include <string>
#include <vector>

namespace farm {

using namespace vortex;

// --- World metrics -----------------------------------------------------------
//
// The art is a 16px grid and the world is 1 unit per art pixel, so a tile is 16
// world units and every sprite can be placed at its natural pixel size. The zoom
// is what turns that into a chunky on-screen tile; nothing else scales.

constexpr u32 kTilePx = 16;      // one tile in the source textures
constexpr f32 kTile   = 16.0f;   // world units per tile
constexpr f32 kZoom   = 3.0f;    // world units -> screen pixels

constexpr i32 kMapW = 44;
constexpr i32 kMapH = 34;

// The character sheets are 32x32 per frame: two tiles tall, so the sprite overhangs
// the tile it stands on and the feet — not the centre — mark the position.
constexpr f32 kCharPx = 32.0f;

// --- Calendar ----------------------------------------------------------------

enum class Season : u8 { Spring, Summer, Fall, Winter, Count };

constexpr i32 kDaysPerSeason = 28;

// A day runs 6:00 -> 26:00 (2am), the span the player can act in, compressed into
// this many real seconds. Sleeping skips whatever is left.
constexpr f32 kDayLengthSeconds = 480.0f;
constexpr f32 kDayStartHour     = 6.0f;
constexpr f32 kDayEndHour       = 26.0f;

[[nodiscard]] const char* seasonName(Season s);

// --- Facing ------------------------------------------------------------------
//
// The order matches the row order of every character sheet in the pack (probed from
// the art: rows are Down, Up, Right, Left), so a Dir doubles as a row index.
enum class Dir : u8 { Down = 0, Up = 1, Right = 2, Left = 3, Count = 4 };

[[nodiscard]] Vec2 dirToVec(Dir d);

// --- Items -------------------------------------------------------------------
//
// One flat id space. Tools occupy the low ids; seeds and produce are the crop index
// offset by a base, which is what lets a crop's seed, its produce and its art all be
// derived from the same number instead of three parallel tables.

using ItemId = i32;

constexpr ItemId kItemNone   = 0;
constexpr ItemId kToolHoe    = 1;
constexpr ItemId kToolCan    = 2;
constexpr ItemId kToolScythe = 3;
constexpr ItemId kToolFirst  = kToolHoe;
constexpr ItemId kToolLast   = kToolScythe;

constexpr ItemId kSeedBase    = 100;
constexpr ItemId kProduceBase = 200;

[[nodiscard]] constexpr bool isTool(ItemId id) { return id >= kToolFirst && id <= kToolLast; }
[[nodiscard]] constexpr bool isSeed(ItemId id) { return id >= kSeedBase && id < kProduceBase; }
[[nodiscard]] constexpr bool isProduce(ItemId id) { return id >= kProduceBase; }
[[nodiscard]] constexpr i32  cropOfSeed(ItemId id) { return id - kSeedBase; }
[[nodiscard]] constexpr i32  cropOfProduce(ItemId id) { return id - kProduceBase; }
[[nodiscard]] constexpr ItemId seedOfCrop(i32 crop) { return kSeedBase + crop; }
[[nodiscard]] constexpr ItemId produceOfCrop(i32 crop) { return kProduceBase + crop; }

// --- Crops -------------------------------------------------------------------

// The design-time facts about a crop. Everything visual is probed off the sheet at
// load time instead of being repeated here — see CropArt.
struct CropDef {
    const char* name;
    const char* path;         // relative to the FarmRPG/Crops directory
    Season      season;
    i32         growthDays;   // days from sowing to first harvest
    i32         seedPrice;
    i32         sellPrice;
    i32         regrowDays;   // 0 = single harvest, else days between re-harvests
};

// What the sheet actually contains, discovered by scanning it: the growth frames in
// order and the icon of the harvested item, which the pack always parks in the last
// non-empty frame with a gap before it.
struct CropArt {
    rhi::TextureHandle       texture;
    std::vector<Rect>        stageUV;   // growth stages, sowing -> mature
    Rect                     itemUV;    // the produce icon
    Vec2                     frameSize{kTilePx, kTilePx};
};

[[nodiscard]] const std::vector<CropDef>& cropDefs();

// --- The farm grid -----------------------------------------------------------

// One cell of the farm. `crop` is an index into cropDefs(), or -1 for bare ground.
struct FarmTile {
    bool tilled  = false;
    bool watered = false;
    i8   crop    = -1;
    i32  age     = 0;       // days since sown (or since the last harvest, if regrowing)
    bool dead    = false;   // sown out of season and caught by a season change

    // Set the first time a regrowing crop ripens, and never cleared while the plant
    // lives. Harvesting rewinds `age` so the next fruit costs regrowDays — without this
    // flag the sprite would follow age back down and the bush the player just picked
    // from would visibly collapse into a seedling.
    bool everRipe = false;
};

// --- Inventory ---------------------------------------------------------------

struct Slot {
    ItemId id    = kItemNone;
    i32    count = 0;

    [[nodiscard]] bool empty() const { return id == kItemNone || count <= 0; }
};

constexpr i32 kHotbarSlots = 10;
constexpr i32 kTotalSlots  = 30;

struct Inventory {
    std::array<Slot, kTotalSlots> slots{};
    i32                           selected = 0;

    // Adds to an existing stack first, then the first free slot. Returns how many
    // did not fit, so a full bag drops the remainder rather than eating it.
    i32  add(ItemId id, i32 count);
    bool remove(ItemId id, i32 count);
    [[nodiscard]] i32 countOf(ItemId id) const;
    [[nodiscard]] Slot& selectedSlot() { return slots[static_cast<usize>(selected)]; }
};

// --- Game state --------------------------------------------------------------

enum class Screen : u8 { Playing, Shop, Sleeping, DaySummary };

// What a swing will do when it lands. Decided when the swing STARTS and carried on the
// Player, not recomputed on impact: the player can change hotbar slot or be nudged
// mid-swing, and a hoe that turns into a seed packet halfway through its arc is a swing
// that quietly does nothing.
enum class ToolAction : u8 { None, Till, Water, Plant, Harvest };

struct Player {
    ecs::Entity entity;
    Vec2        position{0.0f, 0.0f};   // world position of the feet
    Dir         facing = Dir::Down;
    bool        moving = false;
    bool        running = false;

    // Tool swings lock movement for their duration; this is what makes a swing feel
    // like an action rather than a cosmetic overlay.
    f32        useTimer   = 0.0f;
    ItemId     useItem    = kItemNone;
    bool       useApplied = false;   // the effect fires once, mid-swing
    ToolAction useAction  = ToolAction::None;
    i32        useTx      = 0;       // the tile the swing was aimed at
    i32        useTy      = 0;
    ItemId     useSeed    = kItemNone;   // for Plant: the seed committed to the ground

    f32 energy    = 100.0f;
    f32 maxEnergy = 100.0f;
};

struct GameState {
    Player    player;
    Inventory inventory;

    std::vector<FarmTile> tiles = std::vector<FarmTile>(static_cast<usize>(kMapW) * kMapH);

    i32    day    = 1;
    Season season = Season::Spring;
    i32    year   = 1;
    f32    clock  = 0.0f;   // seconds into the day
    i32    money  = 500;

    Screen screen = Screen::Playing;

    // Filled while the player sleeps, shown on the summary card the next morning.
    i32 lastNightEarnings = 0;
    i32 lastNightShipped  = 0;

    // The shipping bin's contents, cashed in overnight — Stardew's rule, and the
    // reason selling is a decision you make before bed rather than a click.
    std::vector<Slot> shipped;

    f32  fadeAlpha = 0.0f;   // screen fade for the sleep transition
    bool quitting  = false;

    std::string toast;       // transient message under the HUD
    f32         toastTimer = 0.0f;

    void note(std::string message, f32 seconds = 2.5f) {
        toast      = std::move(message);
        toastTimer = seconds;
    }

    [[nodiscard]] FarmTile& tile(i32 tx, i32 ty) {
        return tiles[static_cast<usize>(ty) * kMapW + tx];
    }
    [[nodiscard]] const FarmTile& tile(i32 tx, i32 ty) const {
        return tiles[static_cast<usize>(ty) * kMapW + tx];
    }
    [[nodiscard]] static bool inBounds(i32 tx, i32 ty) {
        return tx >= 0 && ty >= 0 && tx < kMapW && ty < kMapH;
    }

    // 6.0 -> 26.0
    [[nodiscard]] f32 hour() const {
        return kDayStartHour + (clock / kDayLengthSeconds) * (kDayEndHour - kDayStartHour);
    }
    [[nodiscard]] std::string clockText() const;
};

}
