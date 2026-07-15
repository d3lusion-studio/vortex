#pragma once
#include "vortex/core/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vortex::diag {

// One measured quantity over time: frame time, entity count, drawcalls, whatever a game
// decides is worth watching. This is the layer between "the number exists somewhere in the
// code" and "the number is on screen / in the log" — measurement is recorded here once, and
// every consumer (overlay, console logger, a test asserting a budget) reads the same series
// instead of each keeping its own bookkeeping.
class Diagnostic {
public:
    explicit Diagnostic(std::string name, usize historyLength = 120);

    // Record one measurement. Ignored while disabled — the point of disabling is making a
    // noisy series stop costing anything, including its history churn.
    void add(f64 value);

    [[nodiscard]] const std::string& name() const { return m_name; }

    [[nodiscard]] f64 value() const;      // most recent measurement
    [[nodiscard]] f64 average() const;    // over the kept history
    [[nodiscard]] f64 minimum() const;
    [[nodiscard]] f64 maximum() const;

    // Exponential moving average — the number an overlay should PRINT. The raw value
    // flickers too fast to read, the windowed average lags spikes; this tracks closely and
    // stays legible.
    [[nodiscard]] f64 smoothed() const { return m_smoothed; }

    [[nodiscard]] usize count() const { return m_count; }   // measurements recorded, ever

    // Oldest-to-newest copy of the kept history, for plotting. A copy, not a view: the
    // storage is a ring, so the data is not contiguous in time order.
    void history(std::vector<f32>& out) const;

    // A disabled diagnostic keeps its history but records nothing new — re-enable and the
    // series resumes where it stopped.
    bool enabled = true;

private:
    std::string      m_name;
    std::vector<f64> m_ring;
    usize            m_head     = 0;    // next write position
    usize            m_size     = 0;    // valid entries in the ring
    usize            m_count    = 0;
    f64              m_smoothed = 0.0;
};

// --- Registry ------------------------------------------------------------------------------
//
// Global by design, like the console's cvars: a diagnostic is only useful if the code that
// measures and the code that displays can find the same series by name without being
// plumbed together. Names are cheap ("physics.bodies", "render.drawcalls"); the first use
// creates the series.

// The series with this name, created on first use.
[[nodiscard]] Diagnostic& get(std::string_view name);

// The series with this name, or null. For consumers that should not create what nobody
// measures.
[[nodiscard]] Diagnostic* find(std::string_view name);

// Record in one call — `diag::add("render.drawcalls", n)` is the whole integration.
void add(std::string_view name, f64 value);

// Every registered series, in registration order. For overlays and log dumps.
[[nodiscard]] const std::vector<Diagnostic*>& all();

// --- Built-ins -----------------------------------------------------------------------------

// Feed once per frame with the frame's dt; maintains "frame.ms" and "frame.fps". FPS is
// derived from the SMOOTHED frame time, not per-frame 1/dt — the latter is the number that
// jitters by 300 at high frame rates and tells nobody anything.
void frame(f32 dtSeconds);

// Log every enabled series through the engine log, at most once per `intervalSeconds`.
// Call it every frame and forget about it; it keeps its own clock (fed by frame()).
void logEvery(f32 intervalSeconds);

}
