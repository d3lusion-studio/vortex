// Diagnostics, headless: measure once, read anywhere.
//
// The three moves this example proves:
//   - a CUSTOM diagnostic: `diag::add("physics.bodies", n)` at the measurement site is the
//     whole integration — the overlay, the log dump and this test all read the same series.
//   - ENABLE/DISABLE at runtime: a disabled series records nothing and costs nothing, and
//     resumes where it stopped.
//   - the LOG consumer: logEvery() prints every enabled series through the engine log on
//     its own schedule — the "FPS in the console" of a headless server build.
//
// Exits non-zero on failure.

#include "vortex/core/diagnostics.hpp"
#include "vortex/core/log.hpp"

#include <cmath>
#include <cstdio>

using namespace vortex;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

} // namespace

int main() {
    std::printf("-- a custom diagnostic: one add() call is the whole integration --\n");
    {
        diag::add("physics.bodies", 10.0);
        diag::add("physics.bodies", 20.0);
        diag::add("physics.bodies", 30.0);

        const diag::Diagnostic* d = diag::find("physics.bodies");
        check(d != nullptr, "the series exists because something measured it");
        check(d->value() == 30.0, "value() is the latest measurement");
        check(d->average() == 20.0, "average() covers the kept history");
        check(d->minimum() == 10.0 && d->maximum() == 30.0, "min and max track the extremes");
        check(d->count() == 3, "count() is measurements ever recorded");
        check(diag::find("never.measured") == nullptr,
              "find() does not invent series nobody measured");
    }

    std::printf("-- enable/disable: silencing a series, not deleting it --\n");
    {
        diag::Diagnostic& d = diag::get("physics.bodies");
        d.enabled = false;
        diag::add("physics.bodies", 9999.0);
        check(d.value() == 30.0 && d.count() == 3, "a disabled series records nothing");

        d.enabled = true;
        diag::add("physics.bodies", 40.0);
        check(d.value() == 40.0 && d.count() == 4, "re-enabled, it resumes where it stopped");
    }

    std::printf("-- history ring: bounded, ordered, plot-ready --\n");
    {
        diag::Diagnostic& d = diag::get("test.ring");
        for (int i = 0; i < 150; ++i) d.add(static_cast<f64>(i));

        std::vector<f32> h;
        d.history(h);
        check(h.size() == 120, "history keeps the window, not everything");
        check(h.front() == 30.0f && h.back() == 149.0f,
              "oldest-to-newest, and the oldest 30 fell off the ring");
        check(d.minimum() == 30.0 && d.maximum() == 149.0,
              "min/max describe the WINDOW — ancient spikes age out");
    }

    std::printf("-- frame(): the built-in series every overlay wants --\n");
    {
        for (int i = 0; i < 120; ++i) diag::frame(1.0f / 60.0f);
        const diag::Diagnostic* fps = diag::find("frame.fps");
        const diag::Diagnostic* ms  = diag::find("frame.ms");
        check(fps != nullptr && ms != nullptr, "frame.fps and frame.ms exist after frame()");
        check(std::fabs(ms->smoothed() - 16.667) < 0.1,
              "frame.ms settles on the actual frame time");
        check(std::fabs(fps->smoothed() - 60.0) < 1.0, "frame.fps settles on the actual rate");
    }

    std::printf("-- logEvery(): the console consumer (see the [Diag] dumps above) --\n");
    {
        // 2.5 more simulated seconds at a 1-second interval: roughly one dump per
        // simulated second. The clock is fed by frame(), so a headless server gets this
        // for free by calling both once per tick.
        for (int i = 0; i < 150; ++i) {
            diag::frame(1.0f / 60.0f);
            diag::logEvery(1.0f);
        }
        check(true, "logEvery ran (see the [Diag] lines above)");
    }

    std::printf("-- verdict --\n");
    std::printf(g_failures == 0 ? "  all checks passed\n" : "  %d check(s) FAILED\n",
                g_failures);
    return g_failures;
}
