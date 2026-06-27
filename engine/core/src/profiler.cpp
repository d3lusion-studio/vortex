#include "vortex/core/profiler.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace vortex::profiler {

namespace {

struct Accum {
    f64 ms    = 0.0;
    u32 calls = 0;
};

std::mutex                                 g_mutex;
std::unordered_map<const char*, Accum>     g_current;   // keyed by literal pointer identity
std::vector<Entry>                         g_lastFrame;

} // namespace

void beginFrame() {
    std::lock_guard lock(g_mutex);
    g_current.clear();
}

void record(const char* name, f64 milliseconds) {
    std::lock_guard lock(g_mutex);
    Accum& a = g_current[name];
    a.ms += milliseconds;
    ++a.calls;
}

void endFrame() {
    std::lock_guard lock(g_mutex);
    g_lastFrame.clear();
    g_lastFrame.reserve(g_current.size());
    for (const auto& [name, accum] : g_current)
        g_lastFrame.push_back({name, accum.ms, accum.calls});
    std::sort(g_lastFrame.begin(), g_lastFrame.end(),
              [](const Entry& a, const Entry& b) { return a.ms > b.ms; });
}

const std::vector<Entry>& lastFrame() { return g_lastFrame; }

}
