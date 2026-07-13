#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/tilemap.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace vortex::json { class Value; }
namespace vortex::pf   { class IFileSystem; }

namespace vortex::assets {

class AssetManager;

// Importer for Tiled maps saved as JSON (`.tmj`). This is the engine's authoring
// path for tilemaps: you draw the level in Tiled, the game loads the file. Building
// a map in code stays supported and is the right answer for procedural levels — the
// two paths meet at renderer::Tilemap.
//
// Deliberately a subset of the format, not a mirror of it:
//   - orthogonal, finite maps (the shape a 2D platformer or top-down game uses)
//   - tile layers and object layers; group layers are walked through
//   - tilesets embedded in the map, or external ones saved as JSON (`.tsj`)
//   - layer data as a plain array (CSV), or base64, optionally zlib/gzip compressed
//
// What is missing fails loudly with a message naming the fix, rather than importing
// something subtly wrong. `.tsx` (XML) tilesets are the one likely to bite: Tiled
// writes them by default, and the fix is to save the tileset as `.tsj` instead.

// One entry from an object layer, handed to TiledImportOptions::onObject. Spawn
// points, trigger zones, doors — the things that turn a grid of tiles into a level.
struct TiledObject {
    std::string name;
    std::string type;                    // Tiled's "class" (called "type" pre-1.9)
    Rect        bounds;                  // world space, min-corner + size
    f32         rotationDegrees = 0.0f;  // clockwise, as Tiled reports it
    renderer::TileId gid = 0;            // tile objects only; 0 for a plain shape

    // The object's custom properties, as an object of name -> value, or null if it
    // has none. Read with props["speed"].asF32(1.0f) and the like: an absent key
    // yields the fallback, so a level that has not set a field still loads.
    const json::Value* properties = nullptr;
};

struct TiledImportOptions {
    // World position of the map's top-left corner. Tiled's y axis points down and
    // the engine's points up, so the map is laid out downward from here.
    Vec2 origin{0.0f, 0.0f};

    // World units per source pixel. A 16px tile at 2.0 covers 32 world units, which
    // is the usual "art is 16px, the world is not" scale factor.
    f32 unitsPerPixel = 1.0f;

    // Draw layer of the first tile layer; each subsequent one sits above it. Pick a
    // base that leaves room above for the sprites you want drawn over the map.
    i32 baseLayer = 0;

    // Tile custom properties, ticked as bools in the tileset editor, that become
    // TileFlags. Anything else on a tile is ignored: gameplay bits are a byte, and
    // this keeps the collision query a single array lookup.
    //   "solid" -> TileSolid, "water" -> TileWater, "hazard" -> TileHazard

    // Called once at the start of every import, before any object. A hot-reload runs
    // the whole import again, objects included, so this is where the things you built
    // from the last one — spawned entities, pickups, colliders — get cleared.
    std::function<void()> onBeginImport;

    // Called once per object in every object layer, in file order. Left empty, object
    // layers are skipped.
    std::function<void(const TiledObject&)> onObject;
};

// Replaces the contents of `out`. Returns false and logs on a malformed or
// unsupported file, leaving `out` cleared rather than half-built.
//
// Textures named by the tilesets are resolved relative to the map file and loaded
// through `assets`, so they share the manager's cache.
//
// `outSources`, if given, receives every file the import read — the map, any external
// tilesets, and the tileset images. That is exactly the set TiledWatcher watches.
bool loadTiledMap(renderer::Tilemap& out, AssetManager& assets, pf::IFileSystem& fs,
                  const char* path, const TiledImportOptions& options = {},
                  std::vector<std::string>* outSources = nullptr);

// Re-imports the map whenever any file it was built from changes on disk. Save in
// Tiled, and the running game has the new level — which is the answer to what a
// code-only engine gives up by not shipping an editor.
//
// A re-import is a full one, deliberately. A tileset image that hot-reloads on its
// own comes back as a *different* GPU texture, so a map holding the old one would go
// on drawing a texture that no longer exists; re-importing re-resolves it. Maps are
// tens of kilobytes, and this runs when a human hits Ctrl+S.
class TiledWatcher {
public:
    TiledWatcher(std::string path, TiledImportOptions options);

    // Import once, and start watching. Same return as loadTiledMap.
    bool load(renderer::Tilemap& out, AssetManager& assets, pf::IFileSystem& fs);

    // Call once a frame. True when the map was re-imported, at which point anything
    // derived from it — spawned entities, physics colliders, merged solid boxes — is
    // stale and must be rebuilt. A file that fails to parse is reported and skipped
    // until it changes again, so a half-saved file does not wipe the level.
    bool poll(renderer::Tilemap& out, AssetManager& assets, pf::IFileSystem& fs);

private:
    std::string        m_path;
    TiledImportOptions m_options;

    // Source path -> last seen mtime. Rebuilt on every successful import, so a map
    // that stops using a tileset stops watching its image.
    std::vector<std::pair<std::string, i64>> m_watched;
};

}
