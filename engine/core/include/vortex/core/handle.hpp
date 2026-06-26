#pragma once
#include "vortex/core/types.hpp"

namespace vortex {
    template <typename Tag>
    struct Handle {
        static constexpr u32 kInvalid = 0xFFFFFFFFu;
        u32 index      = kInvalid;
        u32 generation = 0;

        [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalid; }
        constexpr bool operator==(const Handle&) const noexcept = default;
    };
}
