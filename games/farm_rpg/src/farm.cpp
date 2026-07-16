#include "farm.hpp"

#include <cstdio>

namespace farm {

const char* seasonName(Season s) {
    switch (s) {
        case Season::Spring: return "Spring";
        case Season::Summer: return "Summer";
        case Season::Fall:   return "Fall";
        case Season::Winter: return "Winter";
        default:             return "?";
    }
}

Vec2 dirToVec(Dir d) {
    switch (d) {
        case Dir::Up:    return { 0.0f,  1.0f};
        case Dir::Down:  return { 0.0f, -1.0f};
        case Dir::Left:  return {-1.0f,  0.0f};
        case Dir::Right: return { 1.0f,  0.0f};
        default:         return {};
    }
}

// The playable roster. The pack ships no winter crops, which is why winter here is
// what it is in Stardew: a season you live off what you stored.
const std::vector<CropDef>& cropDefs() {
    static const std::vector<CropDef> defs = {
        // name          path                        season          grow  seed  sell  regrow
        {"Parsnip",     "Spring/Parsnip.png",       Season::Spring,   4,   20,   35,   0},
        {"Potato",      "Spring/Potato.png",        Season::Spring,   6,   50,   80,   0},
        {"Cauliflower", "Spring/Cauliflower.png",   Season::Spring,  12,   80,  175,   0},
        {"Strawberry",  "Spring/Strawberry.png",    Season::Spring,   8,  100,  120,   4},
        {"Tomato",      "Summer/Tomato.png",        Season::Summer,  11,   50,   60,   4},
        {"Melon",       "Summer/Melon.png",         Season::Summer,  12,   80,  250,   0},
        {"Hot Pepper",  "Summer/Hot Pepper.png",    Season::Summer,   5,   40,   40,   3},
        {"Corn",        "Fall/Corn.png",            Season::Fall,    14,  150,  250,   4},
        {"Pumpkin",     "Fall/Pumpkin.png",         Season::Fall,    13,  100,  320,   0},
        {"Eggplant",    "Fall/Eggplant.png",        Season::Fall,     5,   20,   60,   5},
    };
    return defs;
}

i32 Inventory::add(ItemId id, i32 count) {
    if (id == kItemNone || count <= 0) return 0;

    // Tools are unique: a second hoe is not a stack of two, it is a bug.
    if (isTool(id) && countOf(id) > 0) return count;

    for (Slot& s : slots) {
        if (s.id == id && s.count > 0) {
            s.count += count;
            return 0;
        }
    }
    for (Slot& s : slots) {
        if (s.empty()) {
            s.id    = id;
            s.count = count;
            return 0;
        }
    }
    return count;   // bag full
}

bool Inventory::remove(ItemId id, i32 count) {
    if (countOf(id) < count) return false;
    for (Slot& s : slots) {
        if (s.id != id) continue;
        const i32 take = (s.count < count) ? s.count : count;
        s.count -= take;
        count   -= take;
        if (s.count <= 0) s = Slot{};
        if (count == 0) return true;
    }
    return count == 0;
}

i32 Inventory::countOf(ItemId id) const {
    i32 n = 0;
    for (const Slot& s : slots)
        if (s.id == id) n += s.count;
    return n;
}

std::string GameState::clockText() const {
    const f32 h     = hour();
    i32       hh    = static_cast<i32>(h);
    const i32 mm    = static_cast<i32>((h - static_cast<f32>(hh)) * 60.0f) / 10 * 10;
    const bool pm   = (hh % 24) >= 12;
    i32       shown = hh % 24;
    if (shown == 0) shown = 12;
    else if (shown > 12) shown -= 12;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d %s", shown, mm, pm ? "pm" : "am");
    return buf;
}

}
