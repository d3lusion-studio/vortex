#pragma once
#include "vortex/core/types.hpp"

#include <chrono>
#include <vector>

#if defined(VORTEX_TRACY_ENABLED)
    #include <tracy/Tracy.hpp>
#endif

// Lightweight CPU scope profiler. Named scopes are timed via RAII and aggregated
// per frame (summed time + call count per name). It is thread-safe, so scopes
// recorded inside job-system workers fold into the same frame. Designed so a
// Tracy backend could slot in later behind the same macros.
namespace vortex::profiler {

struct Entry {
    const char* name  = nullptr;
    f64         ms    = 0.0;   // total wall time this frame
    u32         calls = 0;
};

void beginFrame();
void endFrame();
void record(const char* name, f64 milliseconds);

// Snapshot of the most recently completed frame, sorted by descending time.
[[nodiscard]] const std::vector<Entry>& lastFrame();

class ScopedZone {
public:
    explicit ScopedZone(const char* name)
        : m_name(name), m_start(std::chrono::steady_clock::now()) {}
    ~ScopedZone() {
        const std::chrono::duration<f64, std::milli> dt =
            std::chrono::steady_clock::now() - m_start;
        record(m_name, dt.count());
    }
    ScopedZone(const ScopedZone&)            = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

private:
    const char*                                    m_name;
    std::chrono::steady_clock::time_point          m_start;
};

}

#define VORTEX_PROFILE_CONCAT_(a, b) a##b
#define VORTEX_PROFILE_CONCAT(a, b)  VORTEX_PROFILE_CONCAT_(a, b)

#if defined(VORTEX_TRACY_ENABLED)
    // Emit both our built-in zone and a Tracy zone from one macro.
    #define VORTEX_PROFILE_ZONE(name)                                              \
        ZoneScopedN(name);                                                         \
        ::vortex::profiler::ScopedZone VORTEX_PROFILE_CONCAT(_vortex_zone_, __LINE__)(name)
    #define VORTEX_PROFILE_FRAME_MARK() FrameMark
#else
    #define VORTEX_PROFILE_ZONE(name) \
        ::vortex::profiler::ScopedZone VORTEX_PROFILE_CONCAT(_vortex_zone_, __LINE__)(name)
    #define VORTEX_PROFILE_FRAME_MARK() ((void)0)
#endif
