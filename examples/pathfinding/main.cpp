// Headless: A* over a tile grid — the walls, the corners, the dead ends and the budget.
//
// No window and no GPU, so it doubles as a CI check for the pathfinder. Pathfinding is
// exactly the kind of code that looks right and is subtly wrong — an inadmissible
// heuristic still returns a path, just a worse one; a diagonal that cuts a corner still
// returns a path, just one that walks through a wall. Each case below pins one of those.

#include "vortex/core/log.hpp"
#include "vortex/renderer/pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vortex;
using renderer::PathRequest;
using renderer::PathResult;
using renderer::TileCoord;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A grid drawn as text: '#' is wall, everything else is floor. Rows top to bottom, so
// (0,0) is the top-left character — the same way a tilemap is indexed.
struct Grid {
    std::vector<std::string> rows;

    [[nodiscard]] bool passable(i32 x, i32 y) const {
        if (y < 0 || y >= static_cast<i32>(rows.size())) return false;
        if (x < 0 || x >= static_cast<i32>(rows[static_cast<usize>(y)].size())) return false;
        return rows[static_cast<usize>(y)][static_cast<usize>(x)] != '#';
    }

    [[nodiscard]] auto fn() const {
        return [this](i32 x, i32 y) { return passable(x, y); };
    }
};

// Every step of a path must be adjacent to the last and land on open floor. This is what
// catches a path that teleports or clips a wall — the failure a length check misses.
[[nodiscard]] bool pathIsWalkable(const Grid& grid, const PathResult& path, bool diagonal) {
    for (usize i = 0; i < path.cells.size(); ++i) {
        if (!grid.passable(path.cells[i].x, path.cells[i].y)) return false;
        if (i == 0) continue;
        const i32 dx = std::abs(path.cells[i].x - path.cells[i - 1].x);
        const i32 dy = std::abs(path.cells[i].y - path.cells[i - 1].y);
        if (dx > 1 || dy > 1) return false;                    // jumped
        if (!diagonal && dx + dy != 1) return false;           // moved diagonally when it may not
        if (dx + dy == 0) return false;                        // stood still
    }
    return true;
}

}   // namespace

int main() {
    std::printf("A* over a tile grid\n\n");

    // --- An open room: the path is the straight line, and its length is forced ---------
    {
        Grid grid{{".....",
                   ".....",
                   "....."}};
        const PathResult path = renderer::findPath({.start = {0, 0}, .goal = {4, 0},
                                                    .passable = grid.fn()});
        check(path.found() && path.cells.size() == 5, "open room: 5 cells for a 4-step walk");
        check(pathIsWalkable(grid, path, false), "open room: every step is legal");
    }

    // --- A wall with one door: the path must go through it ------------------------------
    {
        Grid grid{{".....",
                   "###.#",
                   "....."}};
        const PathResult path = renderer::findPath({.start = {0, 0}, .goal = {0, 2},
                                                   .passable = grid.fn()});
        check(path.found(), "one door: a path exists");
        check(pathIsWalkable(grid, path, false), "one door: every step is legal");

        const bool throughDoor = std::any_of(path.cells.begin(), path.cells.end(),
                                             [](TileCoord c) { return c.x == 3 && c.y == 1; });
        check(throughDoor, "one door: the path goes through the only gap");
    }

    // --- Sealed off: "no path" is a fact, and it must be reported as one -----------------
    {
        Grid grid{{".....",
                   "#####",
                   "....."}};
        const PathResult path = renderer::findPath({.start = {0, 0}, .goal = {0, 2},
                                                    .passable = grid.fn()});
        check(!path.found() && !path.truncated, "sealed: no path, and not merely out of budget");
    }

    // --- A goal inside a wall is a caller error, not a crash ------------------------------
    {
        Grid grid{{"..#.."}};
        const PathResult path = renderer::findPath({.start = {0, 0}, .goal = {2, 0},
                                                    .passable = grid.fn()});
        check(!path.found(), "blocked goal: refused");
    }

    // --- Corner cutting: the case 8-way pathing gets wrong ------------------------------
    //
    // Going from (0,0) to (1,1) diagonally squeezes between the walls at (1,0) and (0,1).
    // With cutCorners off the walker must go around; with it on it may slip through.
    {
        Grid grid{{".#.",
                   "#..",
                   "..."}};

        const PathResult strict = renderer::findPath({.start = {0, 0}, .goal = {1, 1},
                                                      .passable = grid.fn(),
                                                      .allowDiagonal = true,
                                                      .cutCorners = false});
        check(!strict.found(), "corners: refuses to squeeze between two walls");

        const PathResult loose = renderer::findPath({.start = {0, 0}, .goal = {1, 1},
                                                     .passable = grid.fn(),
                                                     .allowDiagonal = true,
                                                     .cutCorners = true});
        check(loose.found() && loose.cells.size() == 2, "corners: cuts it when told to");
    }

    // --- Diagonals are actually shorter, and still legal ---------------------------------
    {
        Grid grid{{".....",
                   ".....",
                   ".....",
                   ".....",
                   "....."}};
        const PathResult straight = renderer::findPath({.start = {0, 0}, .goal = {4, 4},
                                                        .passable = grid.fn()});
        const PathResult diagonal = renderer::findPath({.start = {0, 0}, .goal = {4, 4},
                                                        .passable = grid.fn(),
                                                        .allowDiagonal = true});
        check(straight.cells.size() == 9, "4-way: 8 steps across the diagonal of a 5x5");
        check(diagonal.cells.size() == 5, "8-way: 4 steps, the actual diagonal");
        check(pathIsWalkable(grid, diagonal, true), "8-way: every step is legal");
    }

    // --- The budget bounds the worst case ------------------------------------------------
    //
    // A big open room with an unreachable goal is the pathological case: A* must explore
    // everything before it can say no. maxExpansions is what stops that being a freeze.
    {
        Grid grid;
        for (int y = 0; y < 60; ++y) grid.rows.emplace_back(60, '.');
        for (int y = 0; y < 60; ++y) grid.rows[static_cast<usize>(y)][30] = '#';   // a full wall

        const PathResult path = renderer::findPath({.start = {0, 0}, .goal = {59, 59},
                                                    .passable = grid.fn(),
                                                    .maxExpansions = 50});
        check(!path.found() && path.truncated, "budget: gives up and says so");
    }

    // --- simplifyPath keeps the corners and drops the rest --------------------------------
    {
        const std::vector<TileCoord> line{{0, 0}, {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}};
        const std::vector<TileCoord> simple = renderer::simplifyPath(line);
        check(simple.size() == 3, "simplify: a line with one corner becomes 3 points");
        check(simple.front() == TileCoord{0, 0} && simple.back() == TileCoord{3, 2},
              "simplify: keeps both ends");
    }

    std::printf("\n[%s] Pathfinding self-check: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL",
                failures);
    return failures == 0 ? 0 : 1;
}
