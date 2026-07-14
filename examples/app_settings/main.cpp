// Settings that survive the process.
//
// The flow, and the reason it is in this order:
//
//   1. Read the Settings file YOURSELF, before the App exists.
//   2. Fill AppConfig from it — window size, post-processing, whatever the player chose.
//   3. Construct the App with that config, and hand it the same settings name so it keeps the
//      file up to date and writes it back at shutdown.
//
// Step 1 has to come first because the window is created in the App's constructor: a resolution
// read after that is a resolution that applies next launch. An App that read its own config file
// and used it to size its own window would be a circle, and the version that resolves it silently
// always resolves it wrong. So the circle is cut here, in the open.
//
// Run it twice. The second run starts where the first left off — that is the whole feature.

#include "vortex/app/app.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/settings.hpp"

#include <cstdlib>

using namespace vortex;

namespace {
constexpr const char* kAppName = "vortex-settings-demo";
}

int main() {
    initLogFromEnv();

    // --- 1. Read, before anything is created ---------------------------------
    Settings prefs;
    const std::string path = Settings::defaultPath(kAppName);
    const bool existed = !path.empty() && prefs.load(path.c_str());

    VORTEX_INFO("Settings", "file: %s", path.empty() ? "(no home directory)" : path.c_str());
    VORTEX_INFO("Settings", "%s", existed ? "loaded from a previous run" : "first run — defaults");

    // Every read carries its own default. That is not a convenience: the file is written by an
    // older version of the game, hand-edited, or absent, and the code has to work in all three.
    const i32         launches = prefs.getInt("stats.launches", 0) + 1;
    const i32         width    = prefs.getInt("window.width", 1024);
    const i32         height   = prefs.getInt("window.height", 640);
    const f32         volume   = prefs.getFloat("audio.master", 0.8f);
    const bool        bloom    = prefs.getBool("render.bloom", false);
    const std::string player   = prefs.getString("player.name", "Guest");

    VORTEX_INFO("Settings", "launch #%d", launches);
    VORTEX_INFO("Settings", "  window   %dx%d", width, height);
    VORTEX_INFO("Settings", "  volume   %.2f", volume);
    VORTEX_INFO("Settings", "  bloom    %s", bloom ? "on" : "off");
    VORTEX_INFO("Settings", "  player   %s", player.c_str());

    // --- 2. The config is built FROM the settings ---------------------------
    app::AppConfig config;
    config.title        = "Vortex — Settings";
    config.width        = width;
    config.height       = height;
    config.postProcess  = bloom;
    config.settingsName = kAppName;   // App keeps the file and writes it back on the way out

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    config.maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;

    app::App app(config);

    // --- 3. Change something, so the next run can prove it stuck -------------
    app.settings().set("stats.launches", launches);
    app.settings().set("window.width", width);
    app.settings().set("window.height", height);
    app.settings().set("audio.master", volume);
    app.settings().set("render.bloom", bloom);
    app.settings().set("player.name", player);

    // A run that ends cleanly records where it got to. This is written after the shutdown hooks,
    // so a hook that records "the player quit on level 4" lands in the file that gets saved.
    app.onShutdown([](app::App& a) {
        a.settings().set("stats.lastExit", "clean");
        VORTEX_INFO("Settings", "writing settings on the way out");
    });

    return app.run();
}
