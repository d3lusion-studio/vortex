#pragma once
#include "vortex/core/types.hpp"

namespace vortex {

struct StringId {
    u64 value = 0;
    constexpr bool operator==(const StringId&) const noexcept = default;
};

[[nodiscard]] constexpr u64 fnv1a64(const char* str) noexcept {
    u64 hash = 1469598103934665603ull;          // FNV offset basis
    while (*str) {
        hash ^= static_cast<u64>(static_cast<unsigned char>(*str++));
        hash *= 1099511628211ull;               // FNV prime
    }
    return hash;
}

[[nodiscard]] constexpr StringId stringId(const char* str) noexcept {
    return StringId{fnv1a64(str)};
}

[[nodiscard]] constexpr StringId operator""_sid(const char* str, usize) noexcept {
    return StringId{fnv1a64(str)};
}

}
