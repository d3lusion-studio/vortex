// The map: the tile layers the renderer draws, the collision grid the player walks
// against, and the rules that move the farm forward one day.
#pragma once

#include "assets.hpp"
#include "farm.hpp"

#include "vortex/core/math/rect.hpp"
#include "vortex/ecs/entity.hpp"

#include <vector>

namespace vortex::ecs { class Scene; }

namespace farm {

struct Tree {
    i32 tx = 0;
    i32 ty = 0;
};

// A building placed by its footprint, drawn from a rect on the shared house page.
struct Building {
    Rect source;              // pixels on Assets::houseTex
    i32  tx = 0, ty = 0;      // top-left tile of the footprint
    i32  w  = 0, h  = 0;      // footprint in tiles (what blocks movement)
    i32  doorTx = 0;          // the tile you interact from
    i32  doorTy = 0;
};

class World {
public:
    // Builds the tile layers, scatters the trees and places the buildings. Everything
    // that stands on the ground is spawned as an entity with SpriteComp::ySort, so the
    // engine sorts the whole scene by depth and the game never touches a draw order.
    void build(vortex::ecs::Scene& scene, const Assets& assets, GameState& state);

    // Swap the ground page, re-dress the decor and re-leaf the trees when the calendar
    // turns.
    void applySeason(vortex::ecs::Scene& scene, const Assets& assets, Season season);

    // Push the farm grid into the scene: the soil layers, and one entity per growing
    // crop. Called when a tile changes rather than every frame — the map is 1,496 cells
    // and almost none of them move on a given frame.
    void syncFarm(vortex::ecs::Scene& scene, const Assets& assets, const GameState& state);

    [[nodiscard]] bool blocked(i32 tx, i32 ty) const;
    [[nodiscard]] bool blockedAt(Vec2 world) const;

    // Can this cell ever be hoed? Keeps the farm out of the paths and buildings.
    [[nodiscard]] bool tillable(i32 tx, i32 ty) const;

    [[nodiscard]] const Building& house() const { return m_house; }
    [[nodiscard]] const Building& store() const { return m_store; }
    [[nodiscard]] Vec2 binTileCenter() const;
    [[nodiscard]] i32  binTx() const { return m_binTx; }
    [[nodiscard]] i32  binTy() const { return m_binTy; }

    // Where the player wakes up.
    [[nodiscard]] Vec2 spawnPoint() const;

    [[nodiscard]] static Vec2 tileCenter(i32 tx, i32 ty);
    static void               worldToTile(Vec2 world, i32& tx, i32& ty);

private:
    void setBlocked(i32 tx, i32 ty, bool value);
    void spawnProps(vortex::ecs::Scene& scene, const Assets& assets);

    std::vector<vortex::u8>          m_blocked;
    std::vector<vortex::u8>          m_tillable;
    std::vector<Tree>                m_trees;
    std::vector<vortex::ecs::Entity> m_treeEntities;
    // One slot per cell; invalid where nothing grows. Indexed rather than searched, so
    // syncing a tile is a lookup and not a walk of every crop in the world.
    std::vector<vortex::ecs::Entity> m_cropEntities;
    Building                         m_house;
    Building                         m_store;
    i32                              m_binTx = 0;
    i32                              m_binTy = 0;
};

// One in-game day passes: crops drink what they were given, the calendar turns, and
// anything left in the wrong season dies. Returns the money the shipping bin paid.
i32 advanceDay(vortex::ecs::Scene& scene, const Assets& assets, World& world, GameState& state);

// The growth frame a tile's crop should draw, and whether it is ready to pick.
[[nodiscard]] u32  cropStage(const Assets& assets, const FarmTile& tile);
[[nodiscard]] bool cropReady(const FarmTile& tile);

}
