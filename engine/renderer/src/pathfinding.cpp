#include "vortex/renderer/pathfinding.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace vortex::renderer {

namespace {

struct Node {
    TileCoord cell;
    f32       f = 0.0f;   // g + h: what the queue orders by
};

struct ByF {
    bool operator()(const Node& a, const Node& b) const noexcept { return a.f > b.f; }
};

// Cells are keyed by a single integer so the open/closed sets can be a hash map rather
// than a grid the size of the world — the whole point of A* on a Terraria-sized map is
// that it touches only what it explores.
[[nodiscard]] u64 key(TileCoord c) noexcept {
    return (static_cast<u64>(static_cast<u32>(c.x)) << 32) | static_cast<u32>(c.y);
}

// The heuristic has to be ADMISSIBLE — never an overestimate — or A* stops being optimal
// and starts returning paths that visibly wander. Each of these is the exact cost of the
// unobstructed path, which is the tightest an admissible heuristic can be.
[[nodiscard]] f32 heuristic(TileCoord a, TileCoord b, bool diagonal) noexcept {
    const f32 dx = static_cast<f32>(std::abs(a.x - b.x));
    const f32 dy = static_cast<f32>(std::abs(a.y - b.y));
    if (!diagonal) return dx + dy;   // Manhattan: 4-way movement cannot do better

    // Octile: take the diagonals first, then walk the remainder straight.
    constexpr f32 kDiag = 1.41421356f;
    return (dx + dy) + (kDiag - 2.0f) * std::min(dx, dy);
}

}   // namespace

PathResult findPath(const PathRequest& request) {
    PathResult result;
    if (!request.passable) return result;
    if (!request.passable(request.start.x, request.start.y)) return result;
    if (!request.passable(request.goal.x, request.goal.y)) return result;

    if (request.start == request.goal) {
        result.cells.push_back(request.start);
        return result;
    }

    struct Visited {
        TileCoord from;
        f32       g = 0.0f;
        bool      closed = false;
    };

    std::unordered_map<u64, Visited> visited;
    std::priority_queue<Node, std::vector<Node>, ByF> open;

    visited[key(request.start)] = {request.start, 0.0f, false};
    open.push({request.start, heuristic(request.start, request.goal, request.allowDiagonal)});

    constexpr i32 kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    constexpr i32 kDy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
    const i32 dirCount = request.allowDiagonal ? 8 : 4;

    u32 expansions = 0;
    while (!open.empty()) {
        const Node current = open.top();
        open.pop();

        auto it = visited.find(key(current.cell));
        if (it == visited.end()) continue;
        // The queue has no decrease-key, so a cell can sit in it more than once with
        // different costs. Skipping the ones already closed is what makes that correct
        // instead of merely slow.
        if (it->second.closed) continue;
        it->second.closed = true;

        if (current.cell == request.goal) {
            for (TileCoord c = request.goal;; c = visited[key(c)].from) {
                result.cells.push_back(c);
                if (c == request.start) break;
            }
            std::reverse(result.cells.begin(), result.cells.end());
            return result;
        }

        if (++expansions > request.maxExpansions) {
            result.truncated = true;
            return result;
        }

        const f32 g = it->second.g;
        for (i32 d = 0; d < dirCount; ++d) {
            const TileCoord next{current.cell.x + kDx[d], current.cell.y + kDy[d]};
            if (!request.passable(next.x, next.y)) continue;

            const bool diagonal = d >= 4;
            if (diagonal && !request.cutCorners) {
                // Both cells this diagonal passes between must be open, or the walker
                // slides through the corner of a wall.
                if (!request.passable(current.cell.x + kDx[d], current.cell.y) ||
                    !request.passable(current.cell.x, current.cell.y + kDy[d]))
                    continue;
            }

            const f32 step   = diagonal ? 1.41421356f : 1.0f;
            const f32 nextG  = g + step;
            const u64 nextKey = key(next);

            auto found = visited.find(nextKey);
            if (found != visited.end() && nextG >= found->second.g) continue;

            visited[nextKey] = {current.cell, nextG, false};
            open.push({next, nextG + heuristic(next, request.goal, request.allowDiagonal)});
        }
    }

    return result;   // the reachable region was exhausted: there is genuinely no path
}

std::vector<TileCoord> simplifyPath(const std::vector<TileCoord>& cells) {
    if (cells.size() < 3) return cells;

    std::vector<TileCoord> out;
    out.push_back(cells.front());
    for (usize i = 1; i + 1 < cells.size(); ++i) {
        const i32 inX  = cells[i].x - cells[i - 1].x;
        const i32 inY  = cells[i].y - cells[i - 1].y;
        const i32 outX = cells[i + 1].x - cells[i].x;
        const i32 outY = cells[i + 1].y - cells[i].y;
        if (inX != outX || inY != outY) out.push_back(cells[i]);   // the line turns here
    }
    out.push_back(cells.back());
    return out;
}

}
