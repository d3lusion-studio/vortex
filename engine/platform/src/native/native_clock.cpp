#include "vortex/platform/clock.hpp"
#include <chrono>

namespace vortex::pf {

class StdClock final : public IClock {
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Seconds   = std::chrono::duration<f64>;

public:
    StdClock() : m_start(Clock::now()), m_last(m_start) {}

    f64 time() const override {
        return Seconds(Clock::now() - m_start).count();
    }

    f64 deltaTime() const override { return m_delta; }

    void tick() override {
        auto now  = Clock::now();
        m_delta   = Seconds(now - m_last).count();
        m_last    = now;
    }

private:
    TimePoint m_start;
    TimePoint m_last;
    f64       m_delta = 0.0;
};

std::unique_ptr<IClock> createClock() {
    return std::make_unique<StdClock>();
}

}
