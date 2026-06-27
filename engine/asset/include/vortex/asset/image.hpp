#pragma once
#include "vortex/core/types.hpp"

#include <cstddef>
#include <vector>

namespace vortex::assets {

// Decoded 8-bit RGBA image in CPU memory (import-time product).
struct Image {
    u32             width  = 0;
    u32             height = 0;
    std::vector<u8> pixels;   // RGBA8, width * height * 4 bytes

    [[nodiscard]] bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};


[[nodiscard]] Image decodeImage(const std::byte* data, usize size);

[[nodiscard]] bool writePng(const char* path, u32 width, u32 height, const void* rgba);

}
