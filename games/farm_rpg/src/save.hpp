// Persistence. A plain-text save, because a farm is small enough that legibility is
// worth more than bytes: you can diff two saves and see what a day did.
#pragma once

#include "farm.hpp"

namespace vortex::pf { class IFileSystem; }

namespace farm {

[[nodiscard]] bool saveGame(vortex::pf::IFileSystem& fs, const char* path, const GameState& state);

// Fills `state` from `path`. False if the file is missing or from another version,
// leaving `state` untouched so a bad save starts a new game rather than a broken one.
[[nodiscard]] bool loadGame(vortex::pf::IFileSystem& fs, const char* path, GameState& state);

}
