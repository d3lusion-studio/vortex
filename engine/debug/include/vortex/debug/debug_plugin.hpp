// The debug overlay, as one line in a game's main().
//
//   app.addPlugin<debug::OverlayPlugin>();   // F1 in game
//
// Everything it shows already existed — ImGuiLayer, EntityInspector, PerfOverlay, the
// console's CVars — and none of it was reachable from a game built on App: each was a
// plain class that a raw render loop had to construct, feed input to, and record itself.
// So the engine had a plugin system with nothing to plug in, and an App-based game had no
// way to inspect itself. This is that wiring, done once.
//
// Lives in `debug` rather than `app` so a shipped build simply does not link it: the
// dependency points debug -> app, and App knows nothing about any of this.
#pragma once

#include "vortex/app/plugin.hpp"
#include "vortex/core/types.hpp"
#include "vortex/platform/input.hpp"

#include <memory>

namespace vortex::debug {

class OverlayPlugin : public app::IPlugin {
public:
    // `toggle` shows and hides the whole overlay. It starts hidden: a debug UI that is up
    // by default is one you turn off before every screenshot.
    explicit OverlayPlugin(pf::Key toggle = pf::Key::F1);
    ~OverlayPlugin() override;

    [[nodiscard]] const char* name() const override { return "debug.overlay"; }
    void build(app::App& app) override;

    // True while ImGui wants the pointer — a game that reads the mouse for gameplay should
    // check this, or a click on a debug window also swings the sword behind it.
    [[nodiscard]] bool capturingMouse() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
