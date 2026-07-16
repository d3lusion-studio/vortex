#include "save.hpp"

#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace farm {

using namespace vortex;

namespace {
constexpr int kSaveVersion = 2;   // 2 added FarmTile::everRipe
}

bool saveGame(pf::IFileSystem& fs, const char* path, const GameState& state) {
    std::ostringstream out;
    out << "farmrpg " << kSaveVersion << '\n'
        << state.day << ' ' << static_cast<i32>(state.season) << ' ' << state.year << ' '
        << state.money << ' ' << state.player.energy << '\n'
        << state.player.position.x << ' ' << state.player.position.y << '\n';

    for (const Slot& slot : state.inventory.slots)
        out << slot.id << ' ' << slot.count << ' ';
    out << '\n';

    out << state.shipped.size() << '\n';
    for (const Slot& slot : state.shipped) out << slot.id << ' ' << slot.count << ' ';
    out << '\n';

    // Only the cells that were touched: a farm is mostly untouched grass, and writing
    // 1,496 identical rows would bury the interesting ones.
    for (i32 ty = 0; ty < kMapH; ++ty) {
        for (i32 tx = 0; tx < kMapW; ++tx) {
            const FarmTile& t = state.tile(tx, ty);
            if (!t.tilled && t.crop < 0) continue;
            out << tx << ' ' << ty << ' ' << (t.tilled ? 1 : 0) << ' ' << (t.watered ? 1 : 0)
                << ' ' << static_cast<i32>(t.crop) << ' ' << t.age << ' ' << (t.dead ? 1 : 0)
                << ' ' << (t.everRipe ? 1 : 0) << '\n';
        }
    }

    const std::string text = out.str();
    if (!fs.writeFile(path, text.data(), text.size())) {
        VORTEX_ERROR("Farm", "Could not write save to %s", path);
        return false;
    }
    VORTEX_INFO("Farm", "Saved to %s", path);
    return true;
}

bool loadGame(pf::IFileSystem& fs, const char* path, GameState& state) {
    if (!fs.exists(path)) return false;

    const std::vector<std::byte> bytes = fs.readFile(path);
    if (bytes.empty()) return false;

    std::istringstream in(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));

    std::string magic;
    int         version = 0;
    if (!(in >> magic >> version) || magic != "farmrpg" || version != kSaveVersion) {
        VORTEX_WARN("Farm", "Ignoring save %s: wrong format or version", path);
        return false;
    }

    // A save is a file on disk: it can be stale, hand-edited, or truncated, and every
    // number in it indexes a table. Validating on the way in is what keeps a bad save a
    // failed load rather than an out-of-bounds read a frame later.
    const auto cropOk = [](i32 crop) {
        return crop >= 0 && crop < static_cast<i32>(cropDefs().size());
    };
    const auto itemOk = [&](ItemId id) {
        if (id == kItemNone || isTool(id)) return true;
        if (isSeed(id)) return cropOk(cropOfSeed(id));
        if (isProduce(id)) return cropOk(cropOfProduce(id));
        return false;
    };

    GameState loaded;
    i32       season = 0;
    if (!(in >> loaded.day >> season >> loaded.year >> loaded.money >> loaded.player.energy))
        return false;
    if (season < 0 || season >= static_cast<i32>(Season::Count)) return false;
    loaded.season = static_cast<Season>(season);

    if (!(in >> loaded.player.position.x >> loaded.player.position.y)) return false;

    for (Slot& slot : loaded.inventory.slots) {
        if (!(in >> slot.id >> slot.count)) return false;
        if (!itemOk(slot.id) || slot.count < 0) return false;
    }

    usize shippedCount = 0;
    if (!(in >> shippedCount)) return false;
    if (shippedCount > kTotalSlots * 8) return false;   // a length field is an allocation
    loaded.shipped.resize(shippedCount);
    for (Slot& slot : loaded.shipped) {
        if (!(in >> slot.id >> slot.count)) return false;
        if (!itemOk(slot.id) || slot.count < 0) return false;
    }

    i32 tx = 0, ty = 0, tilled = 0, watered = 0, crop = 0, age = 0, dead = 0, ripe = 0;
    while (in >> tx >> ty >> tilled >> watered >> crop >> age >> dead >> ripe) {
        if (!GameState::inBounds(tx, ty)) continue;
        if (crop != -1 && !cropOk(crop)) return false;
        FarmTile& t = loaded.tile(tx, ty);
        t.tilled   = tilled != 0;
        t.watered  = watered != 0;
        t.crop     = static_cast<i8>(crop);
        t.age      = std::max(0, age);
        t.dead     = dead != 0;
        t.everRipe = ripe != 0;
    }

    // Only commit once the whole file parsed, so a truncated save cannot leave a
    // half-loaded farm behind.
    const ecs::Entity entity = state.player.entity;
    state                    = std::move(loaded);
    state.player.entity      = entity;

    VORTEX_INFO("Farm", "Loaded %s: %s %d, Year %d, %d G", path, seasonName(state.season),
                state.day, state.year, state.money);
    return true;
}

}
