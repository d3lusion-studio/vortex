#include "vortex/asset/tiled.hpp"

#include "vortex/asset/asset_manager.hpp"
#include "vortex/core/json.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/color.hpp"
#include "vortex/platform/filesystem.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

#ifdef VORTEX_TILED_ZLIB
    #include <zlib.h>
#endif

namespace vortex::assets {

namespace {

constexpr const char* kCat = "Tiled";

// 0 for a file that is missing or unreadable, which reads as "unchanged" and so keeps
// a watcher quiet while an editor is mid-write.
[[nodiscard]] i64 fileMTime(const std::string& path) {
    std::error_code ec;
    const auto      time = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<i64>(time.time_since_epoch().count());
}

// ------------------------------------------------------------------- layer data

// Tiled writes layer data one of four ways, and which one you get depends on a
// dropdown in Map Properties that most people never touch. All four decode to the
// same array of global tile ids.
[[nodiscard]] bool decodeBase64(std::string_view text, std::vector<u8>& out) {
    static constexpr i8 kInvalid = -1;
    const auto sextet = [](char c) -> i8 {
        if (c >= 'A' && c <= 'Z') return static_cast<i8>(c - 'A');
        if (c >= 'a' && c <= 'z') return static_cast<i8>(c - 'a' + 26);
        if (c >= '0' && c <= '9') return static_cast<i8>(c - '0' + 52);
        if (c == '+') return 62;
        if (c == '/') return 63;
        return kInvalid;
    };

    u32 accumulator = 0;
    i32 bits        = 0;
    for (const char c : text) {
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        const i8 value = sextet(c);
        if (value == kInvalid) return false;
        accumulator = (accumulator << 6) | static_cast<u32>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<u8>((accumulator >> bits) & 0xFFu));
        }
    }
    return true;
}

#ifdef VORTEX_TILED_ZLIB
// windowBits of 15 + 32 lets zlib sniff the header, so this covers both of Tiled's
// compression choices ("zlib" and "gzip") without asking which one we were handed.
[[nodiscard]] bool inflateAll(const std::vector<u8>& in, usize expectedBytes,
                              std::vector<u8>& out) {
    out.assign(expectedBytes, 0u);

    z_stream stream{};
    if (inflateInit2(&stream, 15 + 32) != Z_OK) return false;

    stream.next_in   = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.data()));
    stream.avail_in  = static_cast<uInt>(in.size());
    stream.next_out  = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    const int result = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    // A short read means the file disagrees with width*height, which is corruption
    // rather than something to paper over.
    return (result == Z_STREAM_END || result == Z_OK) && stream.avail_out == 0;
}
#endif

[[nodiscard]] bool decodeLayerData(const json::Value& layer, usize cellCount,
                                   std::vector<renderer::TileId>& out) {
    out.clear();
    out.reserve(cellCount);

    const json::Value& data = layer["data"];

    // "csv" or absent: a plain JSON array of numbers. Tiled's default for new maps.
    if (data.isArray()) {
        for (const json::Value& cell : data.items()) out.push_back(cell.asU32());
        return out.size() == cellCount;
    }

    if (!data.isString()) {
        VORTEX_ERROR(kCat, "layer '%s' has no tile data", layer["name"].asString().c_str());
        return false;
    }

    const std::string encoding = layer["encoding"].asString();
    if (encoding != "base64") {
        VORTEX_ERROR(kCat, "layer '%s': unsupported encoding '%s'",
                     layer["name"].asString().c_str(), encoding.c_str());
        return false;
    }

    std::vector<u8> bytes;
    if (!decodeBase64(data.asString(), bytes)) {
        VORTEX_ERROR(kCat, "layer '%s': malformed base64", layer["name"].asString().c_str());
        return false;
    }

    const std::string compression = layer["compression"].asString();
    const usize       wanted      = cellCount * 4u;   // gids are 4-byte little-endian

    if (!compression.empty()) {
        if (compression == "zstd") {
            VORTEX_ERROR(kCat, "layer '%s': zstd compression is not supported. In Tiled, set "
                               "Map Properties -> Tile Layer Format to CSV, Base64 (uncompressed) "
                               "or Base64 (zlib).",
                         layer["name"].asString().c_str());
            return false;
        }
#ifdef VORTEX_TILED_ZLIB
        std::vector<u8> inflated;
        if (!inflateAll(bytes, wanted, inflated)) {
            VORTEX_ERROR(kCat, "layer '%s': %s stream did not inflate to %zu cells",
                         layer["name"].asString().c_str(), compression.c_str(), cellCount);
            return false;
        }
        bytes.swap(inflated);
#else
        VORTEX_ERROR(kCat, "layer '%s': the map is %s-compressed but this build has no zlib. "
                           "Rebuild with zlib available, or set Map Properties -> Tile Layer "
                           "Format to CSV in Tiled.",
                     layer["name"].asString().c_str(), compression.c_str());
        return false;
#endif
    }

    if (bytes.size() != wanted) {
        VORTEX_ERROR(kCat, "layer '%s': %zu bytes of tile data, expected %zu",
                     layer["name"].asString().c_str(), bytes.size(), wanted);
        return false;
    }

    for (usize i = 0; i < cellCount; ++i) {
        const usize b = i * 4u;
        out.push_back(static_cast<renderer::TileId>(bytes[b]) |
                      (static_cast<renderer::TileId>(bytes[b + 1u]) << 8) |
                      (static_cast<renderer::TileId>(bytes[b + 2u]) << 16) |
                      (static_cast<renderer::TileId>(bytes[b + 3u]) << 24));
    }
    return true;
}

// ---------------------------------------------------------------------- tilesets

struct Tileset {
    renderer::TileId      firstGid   = 1;
    u32                   tileCount  = 0;
    renderer::SpriteSheet sheet;
    std::string           name;

    [[nodiscard]] bool owns(renderer::TileId index) const noexcept {
        return index >= firstGid && index < firstGid + tileCount;
    }
};

// Tiled stores custom properties as a list of {name, type, value}. Flatten it to an
// object so callers can index by name, and so a missing key reads back as null.
[[nodiscard]] json::Value propertiesToObject(const json::Value& list) {
    json::Value out = json::Value::object();
    for (const json::Value& p : list.items()) {
        const std::string name = p["name"].asString();
        if (name.empty()) continue;
        const json::Value& value = p["value"];
        const std::string  type  = p["type"].asString();
        if (type == "bool")        out.set(name, value.asBool());
        else if (type == "int")    out.set(name, value.asI32());
        else if (type == "float")  out.set(name, value.asF32());
        else                       out.set(name, value.asString());   // string, file, color
    }
    return out;
}

// Bool properties on a tile become gameplay bits. Everything else on the tile is
// ignored on purpose: flags are a byte so the collision query stays one lookup.
void readTileFlags(const json::Value& tileset, const Tileset& entry, renderer::Tilemap& map) {
    for (const json::Value& tile : tileset["tiles"].items()) {
        u8 flags = renderer::TileNone;
        for (const json::Value& p : tile["properties"].items()) {
            if (!p["value"].asBool()) continue;
            const std::string name = p["name"].asString();
            if (name == "solid")       flags |= renderer::TileSolid;
            else if (name == "water")  flags |= renderer::TileWater;
            else if (name == "hazard") flags |= renderer::TileHazard;
        }
        if (flags == renderer::TileNone) continue;
        map.setTileFlags(entry.firstGid + tile["id"].asU32(), flags);
    }
}

[[nodiscard]] bool readTileset(const json::Value& source, renderer::TileId firstGid,
                               const std::filesystem::path& baseDir, AssetManager& assets,
                               u32 mapTileWidth, u32 mapTileHeight,
                               renderer::Tilemap& map, std::vector<std::string>& sources,
                               Tileset& out) {
    out.firstGid  = firstGid;
    out.name      = source["name"].asString();
    out.tileCount = source["tilecount"].asU32();

    const std::string image = source["image"].asString();
    if (image.empty()) {
        VORTEX_ERROR(kCat, "tileset '%s' has no image. Image-collection tilesets (one file per "
                           "tile) are not supported — export the tileset as a single sheet.",
                     out.name.c_str());
        return false;
    }

    const std::string path = (baseDir / image).lexically_normal().string();
    sources.push_back(path);

    const TextureHandle texture = assets.loadTexture(path.c_str());
    const TextureAsset* asset   = assets.get(texture);
    if (asset == nullptr) {
        VORTEX_ERROR(kCat, "tileset '%s': cannot load image '%s'", out.name.c_str(), path.c_str());
        return false;
    }

    const u32 tileWidth  = source["tilewidth"].asU32();
    const u32 tileHeight = source["tileheight"].asU32();
    if (tileWidth != mapTileWidth || tileHeight != mapTileHeight) {
        // Tiled anchors an oversized tile at the bottom of its cell and lets it
        // overhang. TileLayer draws every tile at exactly cell size, so rather than
        // silently squashing the art, say so.
        VORTEX_WARN(kCat, "tileset '%s' is %ux%u but the map grid is %ux%u; tiles will be "
                          "stretched to the cell instead of overhanging it",
                    out.name.c_str(), tileWidth, tileHeight, mapTileWidth, mapTileHeight);
    }

    out.sheet = {
        .texture       = asset->gpu,
        .textureWidth  = asset->width,
        .textureHeight = asset->height,
        .columns       = source["columns"].asU32(1),
        .rows          = 1,
        .margin        = source["margin"].asU32(),
        .spacing       = source["spacing"].asU32(),
    };
    if (out.sheet.columns == 0u) {
        VORTEX_ERROR(kCat, "tileset '%s' reports zero columns", out.name.c_str());
        return false;
    }
    // Tiled gives tilecount and columns; rows is the shape of the sheet they imply.
    out.sheet.rows = (out.tileCount + out.sheet.columns - 1u) / out.sheet.columns;

    readTileFlags(source, out, map);
    return true;
}

// ------------------------------------------------------------------------ colors

// Tiled writes "#rrggbb" or "#aarrggbb".
[[nodiscard]] Color parseTint(std::string_view text, f32 opacity) {
    Color tint = Color::white();
    if (text.size() == 7 || text.size() == 9) {
        const auto nibble = [](char c) -> u32 {
            if (c >= '0' && c <= '9') return static_cast<u32>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<u32>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<u32>(c - 'A' + 10);
            return 0u;
        };
        const auto byteAt = [&](usize i) {
            return static_cast<f32>(nibble(text[i]) * 16u + nibble(text[i + 1u])) / 255.0f;
        };
        const usize rgb = text.size() == 9 ? 3u : 1u;   // skip '#', and alpha if present
        tint.a = text.size() == 9 ? byteAt(1) : 1.0f;
        tint.r = byteAt(rgb);
        tint.g = byteAt(rgb + 2u);
        tint.b = byteAt(rgb + 4u);
    }
    tint.a *= opacity;
    return tint;
}

// ------------------------------------------------------------------------ layers

struct Import {
    const TiledImportOptions& options;
    const std::vector<Tileset>& tilesets;
    Vec2 tileSize{};
    u32  mapTileWidth  = 0;
    u32  mapTileHeight = 0;
    i32  nextLayer     = 0;
};

// One Tiled layer can draw from several tilesets; a TileLayer holds exactly one. So
// a mixed layer becomes one TileLayer per tileset it actually uses, all sharing a
// draw order. Tiles within a layer never overlap, so splitting them changes nothing
// on screen — and it keeps the hot loop in extract() reading a single sheet.
void addTileLayer(const json::Value& layer, const Import& ctx, renderer::Tilemap& map) {
    const u32   width  = layer["width"].asU32();
    const u32   height = layer["height"].asU32();
    const usize cells  = static_cast<usize>(width) * height;
    if (cells == 0u) return;

    std::vector<renderer::TileId> gids;
    if (!decodeLayerData(layer, cells, gids)) return;

    const std::string name = layer["name"].asString();
    const i32         draw = ctx.nextLayer;

    // Layer offsets are in pixels down-and-right; the world's y points up.
    const Vec2 origin{
        ctx.options.origin.x + layer["offsetx"].asF32() * ctx.options.unitsPerPixel,
        ctx.options.origin.y - layer["offsety"].asF32() * ctx.options.unitsPerPixel,
    };
    const Vec2 parallax{layer["parallaxx"].asF32(1.0f), layer["parallaxy"].asF32(1.0f)};
    const Color tint = parseTint(layer["tintcolor"].asString(), layer["opacity"].asF32(1.0f));

    for (const Tileset& set : ctx.tilesets) {
        // Only pay for a TileLayer if this tileset is actually in this layer.
        bool used = false;
        for (const renderer::TileId gid : gids) {
            if (gid != renderer::kEmptyTile && set.owns(renderer::tileIndex(gid))) {
                used = true;
                break;
            }
        }
        if (!used) continue;

        renderer::TileLayer out(width, height, ctx.tileSize);
        out.name        = ctx.tilesets.size() > 1 ? name + "." + set.name : name;
        out.tileset     = set.sheet;
        out.firstTileId = set.firstGid;
        out.origin      = origin;
        out.parallax    = parallax;
        out.tint        = tint;
        out.layer       = draw;
        out.visible     = layer["visible"].asBool(true);

        for (usize i = 0; i < cells; ++i) {
            const renderer::TileId gid = gids[i];
            if (gid == renderer::kEmptyTile) continue;
            if (!set.owns(renderer::tileIndex(gid))) continue;   // another tileset's cell
            out.setTile(static_cast<i32>(i % width), static_cast<i32>(i / width), gid);
        }
        map.addLayer(std::move(out));
    }
}

void addObjects(const json::Value& layer, const Import& ctx) {
    const f32 scale = ctx.options.unitsPerPixel;

    for (const json::Value& o : layer["objects"].items()) {
        TiledObject out;
        out.name            = o["name"].asString();
        out.type            = o["type"].asString();
        out.rotationDegrees = o["rotation"].asF32();
        out.gid             = o["gid"].asU32();

        const f32 w = o["width"].asF32()  * scale;
        const f32 h = o["height"].asF32() * scale;

        // Tiled's y grows downward, and a *tile* object is anchored at its bottom-left
        // corner while every other shape is anchored at its top-left. Both land here as
        // a min-corner Rect in a y-up world.
        const f32 x = ctx.options.origin.x + o["x"].asF32() * scale;
        const f32 top = ctx.options.origin.y - o["y"].asF32() * scale;
        out.bounds = {x, out.gid != 0u ? top : top - h, w, h};

        const json::Value properties = propertiesToObject(o["properties"]);
        out.properties = &properties;

        ctx.options.onObject(out);
    }
}

void addLayers(const json::Value& layers, Import& ctx, renderer::Tilemap& map) {
    for (const json::Value& layer : layers.items()) {
        const std::string type = layer["type"].asString();
        if (type == "tilelayer") {
            addTileLayer(layer, ctx, map);
            ++ctx.nextLayer;
        } else if (type == "objectgroup") {
            if (ctx.options.onObject) addObjects(layer, ctx);
        } else if (type == "group") {
            addLayers(layer["layers"], ctx, map);   // groups are organisational only
        }
    }
}

}

// -------------------------------------------------------------------------- load

bool loadTiledMap(renderer::Tilemap& out, AssetManager& assets, pf::IFileSystem& fs,
                  const char* path, const TiledImportOptions& options,
                  std::vector<std::string>* outSources) {
    out.clear();

    // Collected as we go, and handed back only on success: a failed import leaves the
    // caller watching whatever it was watching before.
    std::vector<std::string> sources{path};

    const std::vector<std::byte> bytes = fs.readFile(path);
    if (bytes.empty()) {
        VORTEX_ERROR(kCat, "cannot read '%s'", path);
        return false;
    }

    std::string error;
    const json::Value doc = json::parse(
        std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &error);
    if (!doc.isObject()) {
        VORTEX_ERROR(kCat, "'%s' is not valid JSON: %s. If this is a .tmx file, save it as .tmj "
                           "(File -> Save As, and pick the JSON format).",
                     path, error.c_str());
        return false;
    }

    if (const std::string orientation = doc["orientation"].asString();
        orientation != "orthogonal") {
        VORTEX_ERROR(kCat, "'%s': orientation '%s' is not supported, only orthogonal",
                     path, orientation.c_str());
        return false;
    }
    if (doc["infinite"].asBool()) {
        VORTEX_ERROR(kCat, "'%s' is an infinite map. Uncheck Infinite in Map Properties.", path);
        return false;
    }

    const u32 tileWidth  = doc["tilewidth"].asU32();
    const u32 tileHeight = doc["tileheight"].asU32();
    if (tileWidth == 0u || tileHeight == 0u) {
        VORTEX_ERROR(kCat, "'%s' has a zero tile size", path);
        return false;
    }

    const std::filesystem::path baseDir = std::filesystem::path(path).parent_path();

    std::vector<Tileset> tilesets;
    for (const json::Value& entry : doc["tilesets"].items()) {
        const renderer::TileId firstGid = entry["firstgid"].asU32(1);
        const std::string      source   = entry["source"].asString();

        Tileset set;
        bool    ok = false;

        if (source.empty()) {
            ok = readTileset(entry, firstGid, baseDir, assets, tileWidth, tileHeight, out,
                             sources, set);
        } else {
            // An external tileset. Tiled saves these as XML (.tsx) unless you ask for
            // JSON, and pulling in an XML parser to read one file is not a trade worth
            // making — say what to click instead.
            const std::filesystem::path resolved = (baseDir / source).lexically_normal();
            const std::string           ext      = resolved.extension().string();
            if (ext != ".tsj" && ext != ".json") {
                VORTEX_ERROR(kCat, "'%s' references tileset '%s'. Only JSON tilesets are "
                                   "supported: open it in Tiled and save as .tsj, or tick "
                                   "'Embed tileset' in the map.",
                             path, source.c_str());
                return false;
            }

            sources.push_back(resolved.string());

            const std::vector<std::byte> raw = fs.readFile(resolved.string().c_str());
            if (raw.empty()) {
                VORTEX_ERROR(kCat, "cannot read tileset '%s'", resolved.string().c_str());
                return false;
            }
            const json::Value tileset = json::parse(
                std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), &error);
            if (!tileset.isObject()) {
                VORTEX_ERROR(kCat, "tileset '%s' is not valid JSON: %s",
                             resolved.string().c_str(), error.c_str());
                return false;
            }
            // Image paths inside an external tileset are relative to the tileset.
            ok = readTileset(tileset, firstGid, resolved.parent_path(), assets,
                             tileWidth, tileHeight, out, sources, set);
        }

        if (!ok) {
            out.clear();
            return false;
        }
        tilesets.push_back(std::move(set));
    }

    if (tilesets.empty()) {
        VORTEX_ERROR(kCat, "'%s' has no tilesets", path);
        return false;
    }

    // Past the point where the file could still be rejected, so a failed import never
    // makes the caller throw away a level it still has.
    if (options.onBeginImport) options.onBeginImport();

    Import ctx{
        .options       = options,
        .tilesets      = tilesets,
        .tileSize      = {static_cast<f32>(tileWidth)  * options.unitsPerPixel,
                          static_cast<f32>(tileHeight) * options.unitsPerPixel},
        .mapTileWidth  = tileWidth,
        .mapTileHeight = tileHeight,
        .nextLayer     = options.baseLayer,
    };
    addLayers(doc["layers"], ctx, out);

    if (outSources != nullptr) *outSources = std::move(sources);

    VORTEX_INFO(kCat, "'%s': %ux%u tiles, %zu tileset(s), %zu layer(s)",
                path, doc["width"].asU32(), doc["height"].asU32(), tilesets.size(),
                out.layers().size());
    return true;
}

// ----------------------------------------------------------------------- watching

TiledWatcher::TiledWatcher(std::string path, TiledImportOptions options)
    : m_path(std::move(path)), m_options(std::move(options)) {}

bool TiledWatcher::load(renderer::Tilemap& out, AssetManager& assets, pf::IFileSystem& fs) {
    std::vector<std::string> sources;
    if (!loadTiledMap(out, assets, fs, m_path.c_str(), m_options, &sources)) return false;

    m_watched.clear();
    m_watched.reserve(sources.size());
    for (std::string& source : sources) {
        const i64 mtime = fileMTime(source);
        m_watched.emplace_back(std::move(source), mtime);
    }
    return true;
}

bool TiledWatcher::poll(renderer::Tilemap& out, AssetManager& assets, pf::IFileSystem& fs) {
    bool dirty = false;
    for (auto& [source, mtime] : m_watched) {
        const i64 now = fileMTime(source);
        if (now == 0 || now == mtime) continue;
        mtime = now;    // taken even if the re-import fails, so a broken file is not retried
        dirty = true;   // every frame until someone fixes it
    }
    if (!dirty) return false;

    // load() rebuilds the watch list, so a failed import must not be allowed to clear
    // it — reload into a scratch map and only publish once it parsed.
    renderer::Tilemap fresh;
    std::vector<std::string> sources;
    if (!loadTiledMap(fresh, assets, fs, m_path.c_str(), m_options, &sources)) {
        VORTEX_WARN(kCat, "'%s' changed but did not import; keeping the loaded map",
                    m_path.c_str());
        return false;
    }

    out = std::move(fresh);

    m_watched.clear();
    m_watched.reserve(sources.size());
    for (std::string& source : sources) {
        const i64 mtime = fileMTime(source);
        m_watched.emplace_back(std::move(source), mtime);
    }

    VORTEX_INFO(kCat, "reloaded '%s'", m_path.c_str());
    return true;
}

}
