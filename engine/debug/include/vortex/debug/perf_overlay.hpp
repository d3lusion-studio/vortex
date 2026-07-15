#pragma once
#include "vortex/core/types.hpp"

#include <vector>

namespace vortex::debug {

// The always-on corner readout: FPS, a frame-time graph, and whatever else has been
// recorded through diag:: — plus the profiler's per-zone times when open.
//
// It draws FROM the diagnostics registry rather than keeping its own counters, so "put a
// number on the overlay" is `diag::add("render.drawcalls", n)` at the measurement site and
// nothing here. The overlay is a viewer, not a bookkeeper.
//
// Call draw() between ImGuiLayer::newFrame() and render(), every frame it should show.
// diag::frame(dt) must be fed once per frame by the app for the FPS section to have
// anything to say.
class PerfOverlay {
public:
    // Corner to pin to: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.
    i32  corner  = 1;
    bool visible = true;

    void draw();

private:
    std::vector<f32> m_history;   // reused scratch for plot data
};

}
