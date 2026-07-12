#pragma once
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

#include <cmath>

namespace vortex {

// PCG32: 16 bytes of state, no allocation, statistically solid, and fast enough to
// call per-particle. Copyable by value, so each system can own an independent
// stream and stay deterministic under job-system reordering — never share one
// Random across threads.
class Random {
public:
    constexpr Random() noexcept = default;

    explicit constexpr Random(u64 seed, u64 stream = 1u) noexcept { reseed(seed, stream); }

    // Distinct `stream` values yield independent sequences from the same seed.
    constexpr void reseed(u64 seed, u64 stream = 1u) noexcept {
        m_state = 0u;
        m_inc   = (stream << 1u) | 1u;
        nextU32();
        m_state += seed;
        nextU32();
    }

    constexpr u32 nextU32() noexcept {
        const u64 old = m_state;
        m_state = old * 6364136223846793005ULL + m_inc;
        const u32 xorshifted = static_cast<u32>(((old >> 18u) ^ old) >> 27u);
        const u32 rot        = static_cast<u32>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    constexpr u64 nextU64() noexcept {
        const u64 hi = nextU32();
        return (hi << 32) | nextU32();
    }

    // Uniform in [0, bound). Rejection-sampled, so free of modulo bias.
    constexpr u32 nextBounded(u32 bound) noexcept {
        if (bound == 0u) return 0u;
        const u32 threshold = (0u - bound) % bound;   // 2^32 mod bound
        for (;;) {
            const u32 r = nextU32();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform in [0, 1). 24 bits of mantissa, the most a f32 can hold.
    [[nodiscard]] constexpr f32 nextFloat() noexcept {
        return static_cast<f32>(nextU32() >> 8) * 0x1.0p-24f;
    }

    [[nodiscard]] constexpr f32 range(f32 minInclusive, f32 maxExclusive) noexcept {
        return minInclusive + (maxExclusive - minInclusive) * nextFloat();
    }

    [[nodiscard]] constexpr i32 range(i32 minInclusive, i32 maxInclusive) noexcept {
        if (maxInclusive <= minInclusive) return minInclusive;
        const u32 span = static_cast<u32>(maxInclusive - minInclusive) + 1u;
        return minInclusive + static_cast<i32>(nextBounded(span));
    }

    [[nodiscard]] constexpr bool nextBool(f32 probabilityTrue = 0.5f) noexcept {
        return nextFloat() < probabilityTrue;
    }

    [[nodiscard]] constexpr f32 nextSigned() noexcept { return range(-1.0f, 1.0f); }

    // ------------------------------------------------------------ geometry

    // Uniform over the disc, not over (angle, radius) — the sqrt corrects the
    // area weighting that a naive radius pick would introduce.
    [[nodiscard]] Vec2 insideUnitCircle() noexcept {
        const f32 theta = range(0.0f, kTwoPi);
        const f32 r     = std::sqrt(nextFloat());
        return {r * std::cos(theta), r * std::sin(theta)};
    }

    [[nodiscard]] Vec2 onUnitCircle() noexcept { return Vec2::fromAngle(range(0.0f, kTwoPi)); }

    [[nodiscard]] Vec2 direction() noexcept { return onUnitCircle(); }

    [[nodiscard]] Vec2 insideRect(Vec2 min, Vec2 max) noexcept {
        return {range(min.x, max.x), range(min.y, max.y)};
    }

    // Box-Muller. Useful for particle jitter and procedural spread.
    [[nodiscard]] f32 gaussian(f32 mean = 0.0f, f32 stdDev = 1.0f) noexcept {
        // nextFloat() can return 0, and log(0) is -inf; nudge off the boundary.
        const f32 u1 = 1.0f - nextFloat();
        const f32 u2 = nextFloat();
        return mean + stdDev * std::sqrt(-2.0f * std::log(u1)) * std::cos(kTwoPi * u2);
    }

    // ----------------------------------------------------------- sequences

    template <class T>
    [[nodiscard]] const T& pick(const T* items, usize count) noexcept {
        return items[nextBounded(static_cast<u32>(count))];
    }

    template <class T>
    void shuffle(T* items, usize count) noexcept {
        for (usize i = count; i > 1; --i) {
            const usize j = nextBounded(static_cast<u32>(i));
            T tmp      = items[i - 1];
            items[i - 1] = items[j];
            items[j]     = tmp;
        }
    }

private:
    u64 m_state = 0x853c49e6748fea9bULL;
    u64 m_inc   = 0xda3e39cb94b95bdbULL;
};

// Convenience stream for prototyping and non-deterministic effects. Thread-local,
// so it is safe to call from jobs, but the values a job sees are not reproducible.
// Anything that must replay identically should own an explicit Random.
[[nodiscard]] inline Random& defaultRandom() noexcept {
    static thread_local Random rng{0x9e3779b97f4a7c15ULL};
    return rng;
}

}
