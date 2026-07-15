#include "vortex/core/diagnostics.hpp"

#include "vortex/core/log.hpp"

#include <algorithm>
#include <memory>

namespace vortex::diag {

// ---------------------------------------------------------------------------
// Diagnostic
// ---------------------------------------------------------------------------

Diagnostic::Diagnostic(std::string name, usize historyLength)
    : m_name(std::move(name)), m_ring(std::max<usize>(historyLength, 1)) {}

void Diagnostic::add(f64 value) {
    if (!enabled) return;

    m_ring[m_head] = value;
    m_head = (m_head + 1) % m_ring.size();
    m_size = std::min(m_size + 1, m_ring.size());
    ++m_count;

    // First measurement seeds the EMA — starting from zero would make every series spend
    // its first second climbing out of a hole it was never in.
    m_smoothed = m_count == 1 ? value : m_smoothed + 0.1 * (value - m_smoothed);
}

f64 Diagnostic::value() const {
    if (m_size == 0) return 0.0;
    return m_ring[(m_head + m_ring.size() - 1) % m_ring.size()];
}

f64 Diagnostic::average() const {
    if (m_size == 0) return 0.0;
    f64 sum = 0.0;
    for (usize i = 0; i < m_size; ++i) sum += m_ring[i];
    return sum / static_cast<f64>(m_size);
}

f64 Diagnostic::minimum() const {
    if (m_size == 0) return 0.0;
    f64 m = m_ring[0];
    for (usize i = 1; i < m_size; ++i) m = std::min(m, m_ring[i]);
    return m;
}

f64 Diagnostic::maximum() const {
    if (m_size == 0) return 0.0;
    f64 m = m_ring[0];
    for (usize i = 1; i < m_size; ++i) m = std::max(m, m_ring[i]);
    return m;
}

void Diagnostic::history(std::vector<f32>& out) const {
    out.clear();
    out.reserve(m_size);
    // The ring's oldest entry is at m_head once the ring has wrapped, at 0 before.
    const usize start = m_size == m_ring.size() ? m_head : 0;
    for (usize i = 0; i < m_size; ++i)
        out.push_back(static_cast<f32>(m_ring[(start + i) % m_ring.size()]));
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

namespace {

struct Registry {
    // unique_ptr so a Diagnostic& handed out survives the vector growing.
    std::vector<std::unique_ptr<Diagnostic>> owned;
    std::vector<Diagnostic*>                 view;
    f64                                      clock       = 0.0;   // seconds, fed by frame()
    f64                                      lastLogTime = 0.0;
};

Registry& registry() {
    static Registry r;
    return r;
}

} // namespace

Diagnostic& get(std::string_view name) {
    if (Diagnostic* d = find(name)) return *d;
    Registry& r = registry();
    r.owned.push_back(std::make_unique<Diagnostic>(std::string(name)));
    r.view.push_back(r.owned.back().get());
    return *r.owned.back();
}

Diagnostic* find(std::string_view name) {
    for (Diagnostic* d : registry().view)
        if (d->name() == name) return d;
    return nullptr;
}

void add(std::string_view name, f64 value) { get(name).add(value); }

const std::vector<Diagnostic*>& all() { return registry().view; }

// ---------------------------------------------------------------------------
// Built-ins
// ---------------------------------------------------------------------------

void frame(f32 dtSeconds) {
    registry().clock += dtSeconds;

    Diagnostic& ms = get("frame.ms");
    ms.add(static_cast<f64>(dtSeconds) * 1000.0);

    // Derived from the smoothed frame time: 1/dt per frame is the number that reads 900,
    // 1400, 700 on consecutive frames and informs nobody.
    Diagnostic& fps = get("frame.fps");
    fps.add(ms.smoothed() > 0.0 ? 1000.0 / ms.smoothed() : 0.0);
}

void logEvery(f32 intervalSeconds) {
    Registry& r = registry();
    if (r.clock - r.lastLogTime < static_cast<f64>(intervalSeconds)) return;
    r.lastLogTime = r.clock;

    for (const Diagnostic* d : r.view) {
        if (!d->enabled || d->count() == 0) continue;
        VORTEX_INFO("Diag", "%-24s %10.3f  (avg %.3f, min %.3f, max %.3f)",
                    d->name().c_str(), d->smoothed(), d->average(), d->minimum(),
                    d->maximum());
    }
}

}
