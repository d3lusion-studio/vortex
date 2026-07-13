#pragma once
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/renderer/sprite_atlas.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vortex::renderer {

// Tile ids are 1-based on the wire so that 0 can mean "empty" without costing a
// separate occupancy mask. kEmptyTile is what an unset cell reads back as.
//
// The top three bits carry orientation rather than identity, matching Tiled's GID
// encoding so an imported map needs no re-encoding pass. Everything that means
// "which tile is this" — flags, the tileset index — must go through tileIndex();
// only extract() looks at the orientation bits.
using TileId = u32;
inline constexpr TileId kEmptyTile = 0u;

inline constexpr TileId kFlipHorizontal = 0x80000000u;
inline constexpr TileId kFlipVertical   = 0x40000000u;
inline constexpr TileId kFlipDiagonal   = 0x20000000u;   // transpose; with H or V this is a 90° rotation
inline constexpr TileId kFlipMask       = kFlipHorizontal | kFlipVertical | kFlipDiagonal;
inline constexpr TileId kTileIndexMask  = ~kFlipMask;    // 29 bits: half a billion distinct tiles

[[nodiscard]] constexpr TileId tileIndex(TileId id) noexcept { return id & kTileIndexMask; }
[[nodiscard]] constexpr TileId tileFlipBits(TileId id) noexcept { return id & kFlipMask; }

// Per-tile gameplay bits, indexed by tile id. Kept out of the draw path entirely —
// rendering never reads them, and collision never reads the UVs.
enum TileFlags : u8 {
    TileNone  = 0,
    TileSolid = 1u << 0,   // blocks movement; feeds collider generation
    TileWater = 1u << 1,
    TileHazard = 1u << 2,
};

// A grid of tile ids drawn from one tileset page. Layers stack by `layer`, and
// `parallax` scales how far the layer moves against the camera — 1.0 tracks the
// world exactly, less than 1 drifts behind for a background.
class TileLayer {
public:
    TileLayer() = default;
    TileLayer(u32 widthInTiles, u32 heightInTiles, Vec2 tileSize);

    void resize(u32 widthInTiles, u32 heightInTiles);

    // Out-of-range reads return kEmptyTile; out-of-range writes are dropped. Both
    // are silent, because gameplay routinely probes past the edge of a map.
    [[nodiscard]] TileId tile(i32 tx, i32 ty) const noexcept;
    void                 setTile(i32 tx, i32 ty, TileId id) noexcept;
    void                 fill(TileId id) noexcept;

    // Tile coordinate covering a world point, and the world-space centre of a tile.
    [[nodiscard]] Vec2 tileCenter(i32 tx, i32 ty) const noexcept;
    void               worldToTile(Vec2 world, i32& tx, i32& ty) const noexcept;

    [[nodiscard]] Rect worldBounds() const noexcept;

    // Append the visible tiles as draw items. With `visibleBounds`, only the tile
    // range the camera actually overlaps is walked — a 4096x4096 map costs the same
    // to draw as the screenful you can see.
    void extract(std::vector<RenderItem>& out, const Rect* visibleBounds = nullptr) const;

    [[nodiscard]] u32   width()  const noexcept { return m_width; }
    [[nodiscard]] u32   height() const noexcept { return m_height; }
    [[nodiscard]] usize tileCount() const noexcept { return m_tiles.size(); }
    [[nodiscard]] const std::vector<TileId>& tiles() const noexcept { return m_tiles; }

    std::string name;
    SpriteSheet tileset;                 // page + grid the ids index into

    // The tile id that means "frame 0 of this layer's tileset". Ids stay globally
    // unique across a map — which is what lets Tilemap key flags by id alone — while
    // each layer subtracts its own base to land on a frame. A map drawn from a single
    // tileset leaves this at 1 and never thinks about it.
    TileId      firstTileId = 1;

    Vec2        tileSize{32.0f, 32.0f};  // world units per tile
    Vec2        origin{0.0f, 0.0f};      // world position of the top-left corner
    Vec2        parallax{1.0f, 1.0f};
    Color       tint    = Color::white();
    i32         layer   = 0;
    bool        visible = true;

private:
    u32                 m_width  = 0;
    u32                 m_height = 0;
    std::vector<TileId> m_tiles;   // row-major, m_width * m_height
};

// A stack of layers plus the tile flags they share. This is the level.
class Tilemap {
public:
    TileLayer&       addLayer(TileLayer layer);
    [[nodiscard]] TileLayer*       layer(std::string_view name);
    [[nodiscard]] const TileLayer* layer(std::string_view name) const;

    [[nodiscard]] std::vector<TileLayer>&       layers() noexcept { return m_layers; }
    [[nodiscard]] const std::vector<TileLayer>& layers() const noexcept { return m_layers; }

    // Flags are per tile *id*, so every instance of a tile shares them. Ids beyond
    // what has been set read back as TileNone. Orientation bits are masked off, so a
    // flipped tile carries the same flags as the tile it was flipped from. Flagging
    // kEmptyTile is dropped: it would make every empty cell in the map read as solid.
    void                 setTileFlags(TileId id, u8 flags);
    [[nodiscard]] u8     tileFlags(TileId id) const noexcept;

    // Dense, indexed by tile id, and only as long as the highest id ever flagged.
    [[nodiscard]] const std::vector<u8>& flags() const noexcept { return m_flags; }

    void clear();

    // Does any layer put a tile carrying `flags` over this world point? This is the
    // query gameplay actually asks ("am I standing on something solid?").
    [[nodiscard]] bool queryFlags(Vec2 world, u8 flags) const noexcept;
    [[nodiscard]] bool solid(Vec2 world) const noexcept { return queryFlags(world, TileSolid); }

    // Merge runs of solid tiles in `layerName` into as few axis-aligned boxes as
    // possible, for handing to the physics world as static geometry. Greedy: rows
    // first, then vertical merge of identical spans, which turns a typical
    // platformer layer into tens of boxes rather than thousands.
    [[nodiscard]] std::vector<Rect> solidBoxes(std::string_view layerName) const;

    void extract(std::vector<RenderItem>& out, const Rect* visibleBounds = nullptr) const;

private:
    std::vector<TileLayer> m_layers;
    std::vector<u8>        m_flags;   // indexed by tile id
};

}
