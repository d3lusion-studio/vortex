#include "vortex/renderer/tilemap.hpp"

#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/profiler.hpp"

#include <algorithm>
#include <cmath>

namespace vortex::renderer {

namespace {

// Rect is a min-corner + size box (see Camera2D::visibleBounds), so top()/bottom()
// read as min/max on the y axis regardless of which way the world's y points.
[[nodiscard]] i32 floorDiv(f32 numerator, f32 denominator) noexcept {
    return static_cast<i32>(std::floor(numerator / denominator));
}

}

// -------------------------------------------------------------------- TileLayer

TileLayer::TileLayer(u32 widthInTiles, u32 heightInTiles, Vec2 size) : tileSize(size) {
    resize(widthInTiles, heightInTiles);
}

void TileLayer::resize(u32 widthInTiles, u32 heightInTiles) {
    m_width  = widthInTiles;
    m_height = heightInTiles;
    m_tiles.assign(static_cast<usize>(widthInTiles) * heightInTiles, kEmptyTile);
}

TileId TileLayer::tile(i32 tx, i32 ty) const noexcept {
    if (tx < 0 || ty < 0 || static_cast<u32>(tx) >= m_width || static_cast<u32>(ty) >= m_height)
        return kEmptyTile;
    return m_tiles[static_cast<usize>(ty) * m_width + static_cast<usize>(tx)];
}

void TileLayer::setTile(i32 tx, i32 ty, TileId id) noexcept {
    if (tx < 0 || ty < 0 || static_cast<u32>(tx) >= m_width || static_cast<u32>(ty) >= m_height)
        return;
    m_tiles[static_cast<usize>(ty) * m_width + static_cast<usize>(tx)] = id;
}

void TileLayer::fill(TileId id) noexcept {
    std::fill(m_tiles.begin(), m_tiles.end(), id);
}

// Rows march away from `origin` along -y, so origin is the top-left corner.
Vec2 TileLayer::tileCenter(i32 tx, i32 ty) const noexcept {
    return {origin.x + (static_cast<f32>(tx) + 0.5f) * tileSize.x,
            origin.y - (static_cast<f32>(ty) + 0.5f) * tileSize.y};
}

void TileLayer::worldToTile(Vec2 world, i32& tx, i32& ty) const noexcept {
    tx = floorDiv(world.x - origin.x, tileSize.x);
    ty = floorDiv(origin.y - world.y, tileSize.y);
}

Rect TileLayer::worldBounds() const noexcept {
    const f32 w = static_cast<f32>(m_width)  * tileSize.x;
    const f32 h = static_cast<f32>(m_height) * tileSize.y;
    return {origin.x, origin.y - h, w, h};
}

void TileLayer::extract(std::vector<RenderItem>& out, const Rect* visibleBounds) const {
    VORTEX_PROFILE_ZONE("tilemap.extract");
    if (!visible || m_tiles.empty() || !tileset.texture.valid()) return;
    if (tileSize.x <= 0.0f || tileSize.y <= 0.0f) return;

    // Parallax slides the layer against the camera. Deriving the camera centre from
    // the visible rect keeps the layer ignorant of the Camera2D type; with no rect
    // to read there is no camera to parallax against, so the layer sits at origin.
    Vec2 base = origin;
    if (visibleBounds != nullptr && (parallax.x != 1.0f || parallax.y != 1.0f)) {
        const Vec2 eye = visibleBounds->center();
        base = {origin.x + eye.x * (1.0f - parallax.x),
                origin.y + eye.y * (1.0f - parallax.y)};
    }

    // Clamp the walk to the tiles the camera overlaps. This is the whole point: a
    // huge map costs a screenful of work, not a mapful.
    i32 x0 = 0, x1 = static_cast<i32>(m_width)  - 1;
    i32 y0 = 0, y1 = static_cast<i32>(m_height) - 1;
    if (visibleBounds != nullptr) {
        x0 = std::max(x0, floorDiv(visibleBounds->left()  - base.x, tileSize.x));
        x1 = std::min(x1, floorDiv(visibleBounds->right() - base.x, tileSize.x));
        // top() is min-y, so it maps to the *last* row; bottom() maps to the first.
        y0 = std::max(y0, floorDiv(base.y - visibleBounds->bottom(), tileSize.y));
        y1 = std::min(y1, floorDiv(base.y - visibleBounds->top(),    tileSize.y));
        if (x0 > x1 || y0 > y1) return;   // entirely off screen
    }

    const Vec4 color = tint;
    out.reserve(out.size() + static_cast<usize>(x1 - x0 + 1) * static_cast<usize>(y1 - y0 + 1));

    for (i32 ty = y0; ty <= y1; ++ty) {
        const usize row = static_cast<usize>(ty) * m_width;
        for (i32 tx = x0; tx <= x1; ++tx) {
            const TileId id = m_tiles[row + static_cast<usize>(tx)];
            if (id == kEmptyTile) continue;

            const Vec2 center{base.x + (static_cast<f32>(tx) + 0.5f) * tileSize.x,
                              base.y - (static_cast<f32>(ty) + 0.5f) * tileSize.y};

            out.push_back({
                .transform = Mat4::translation(center.x, center.y, 0.0f) *
                             Mat4::scaling(tileSize.x, tileSize.y, 1.0f),
                .color   = color,
                .uv      = tileset.frameUV(id - 1u),   // ids are 1-based; 0 is empty
                .texture = tileset.texture,
                .layer   = layer,
            });
        }
    }
}

// ---------------------------------------------------------------------- Tilemap

TileLayer& Tilemap::addLayer(TileLayer layer) {
    m_layers.push_back(std::move(layer));
    return m_layers.back();
}

TileLayer* Tilemap::layer(std::string_view name) {
    for (TileLayer& l : m_layers)
        if (l.name == name) return &l;
    return nullptr;
}

const TileLayer* Tilemap::layer(std::string_view name) const {
    return const_cast<Tilemap*>(this)->layer(name);
}

void Tilemap::setTileFlags(TileId id, u8 flags) {
    if (id >= m_flags.size()) m_flags.resize(id + 1u, TileNone);
    m_flags[id] = flags;
}

u8 Tilemap::tileFlags(TileId id) const noexcept {
    return id < m_flags.size() ? m_flags[id] : static_cast<u8>(TileNone);
}

bool Tilemap::queryFlags(Vec2 world, u8 flags) const noexcept {
    for (const TileLayer& l : m_layers) {
        i32 tx = 0, ty = 0;
        l.worldToTile(world, tx, ty);
        if ((tileFlags(l.tile(tx, ty)) & flags) != 0u) return true;
    }
    return false;
}

std::vector<Rect> Tilemap::solidBoxes(std::string_view layerName) const {
    std::vector<Rect> boxes;
    const TileLayer* l = layer(layerName);
    if (l == nullptr) return boxes;

    // An open box is a horizontal run that the row below might still extend
    // downward. A run that the next row does not repeat is closed and emitted.
    struct Open { i32 x0, x1, y0, y1; };
    std::vector<Open> open, carried;

    const auto emit = [&](const Open& o) {
        boxes.push_back({l->origin.x + static_cast<f32>(o.x0) * l->tileSize.x,
                         l->origin.y - static_cast<f32>(o.y1 + 1) * l->tileSize.y,
                         static_cast<f32>(o.x1 - o.x0 + 1) * l->tileSize.x,
                         static_cast<f32>(o.y1 - o.y0 + 1) * l->tileSize.y});
    };

    const auto isSolid = [&](i32 tx, i32 ty) {
        return (tileFlags(l->tile(tx, ty)) & TileSolid) != 0u;
    };

    const i32 w = static_cast<i32>(l->width());
    const i32 h = static_cast<i32>(l->height());

    for (i32 ty = 0; ty < h; ++ty) {
        carried.clear();
        for (i32 tx = 0; tx < w;) {
            if (!isSolid(tx, ty)) { ++tx; continue; }
            const i32 runStart = tx;
            while (tx < w && isSolid(tx, ty)) ++tx;
            const i32 runEnd = tx - 1;

            // Extend the box directly above iff it spans exactly the same columns —
            // a partial overlap would make the merged shape non-rectangular.
            auto it = std::find_if(open.begin(), open.end(), [&](const Open& o) {
                return o.x0 == runStart && o.x1 == runEnd && o.y1 == ty - 1;
            });
            if (it != open.end()) {
                it->y1 = ty;
                carried.push_back(*it);
                open.erase(it);
            } else {
                carried.push_back({runStart, runEnd, ty, ty});
            }
        }
        for (const Open& o : open) emit(o);   // not repeated on this row: done
        open.swap(carried);
    }
    for (const Open& o : open) emit(o);

    return boxes;
}

void Tilemap::extract(std::vector<RenderItem>& out, const Rect* visibleBounds) const {
    for (const TileLayer& l : m_layers) l.extract(out, visibleBounds);
}

}
