// A* over a tile grid.
//
// Every game with something that walks needs this, and every one of them writes it again:
// an NPC keeping a schedule, a slime crossing a cave, a villager going home at dusk. It is
// small, it is well understood, and getting it subtly wrong (a heuristic that overestimates,
// a diagonal that cuts a corner through a wall) produces paths that look drunk rather than
// paths that crash.
//
// The grid is not owned here. `PathRequest::passable` answers "can something stand on this
// cell", which is the only question A* asks — so this works against a Tilemap, a bitset, a
// std::vector<bool>, or a query that also excludes the tile a rival is standing on.
#pragma once

#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

#include <functional>
#include <vector>

namespace vortex::renderer {

struct TileCoord {
    i32 x = 0;
    i32 y = 0;

    [[nodiscard]] constexpr bool operator==(const TileCoord&) const noexcept = default;
};

struct PathRequest {
    TileCoord start;
    TileCoord goal;

    // True when a walker may occupy this cell. Asked about cells outside the map too —
    // answer false there unless the world genuinely wraps.
    std::function<bool(i32 x, i32 y)> passable;

    // Diagonal steps. Off gives the 4-way movement a tile-based farm or a dungeon wants;
    // on gives the smoother 8-way an open field wants.
    bool allowDiagonal = false;

    // With diagonals on, refuse a diagonal that squeezes between two blocked cells. A
    // walker that takes it clips the corner of a wall, which is the single most common way
    // 8-way pathing looks broken.
    bool cutCorners = false;

    // Give up after expanding this many cells. A path to an unreachable goal explores the
    // whole reachable region before it can say "no" — on a Terraria-sized map that is
    // millions of cells and a visible freeze. This is what bounds the worst case; leave it
    // generous for short hops and tight for anything asked every frame.
    u32 maxExpansions = 20000;
};

struct PathResult {
    // Cells from start to goal INCLUSIVE, empty when no path was found.
    std::vector<TileCoord> cells;

    // True when the search ran out of budget rather than proving the goal unreachable.
    // Worth telling apart: "no path" is a fact the caller can cache, "gave up" is not.
    bool truncated = false;

    [[nodiscard]] bool found() const noexcept { return !cells.empty(); }
};

// Search. A blocked start is a failure; a blocked GOAL is too — ask for the cell next to
// the door, not the door.
[[nodiscard]] PathResult findPath(const PathRequest& request);

// Drop the cells that only continue a straight line, leaving the corners.
//
// A* returns one cell per step, which is what a walker following it moves through, not
// what it needs to be told. Steering toward the next corner instead of the next cell is
// the difference between gliding and stepping.
[[nodiscard]] std::vector<TileCoord> simplifyPath(const std::vector<TileCoord>& cells);

}
