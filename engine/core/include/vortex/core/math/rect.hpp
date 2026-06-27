#pragma once
#include "vortex/core/types.hpp"

namespace vortex {

struct Rect {
    f32 x = 0.0f, y = 0.0f;
    f32 width = 0.0f, height = 0.0f;

    [[nodiscard]] constexpr f32 left()   const noexcept { return x; }
    [[nodiscard]] constexpr f32 right()  const noexcept { return x + width; }
    [[nodiscard]] constexpr f32 top()    const noexcept { return y; }
    [[nodiscard]] constexpr f32 bottom() const noexcept { return y + height; }

    [[nodiscard]] constexpr bool contains(f32 px, f32 py) const noexcept {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    constexpr bool operator==(const Rect&) const noexcept = default;
};

inline constexpr Rect kFullUV{0.0f, 0.0f, 1.0f, 1.0f};

}
