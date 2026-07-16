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

// Alpha-composite `src` over `dst`, in place — the "over" operator, on straight
// (non-premultiplied) RGBA.
//
// This is what a paper-doll character is made of: a pack that ships skin, clothes, hair
// and a held tool as separate sheets has to be flattened before upload, or every frame
// costs four draw calls and four textures instead of one. Import-time work: it runs on
// the CPU, once.
//
// A no-op (and a warning) unless the two images are the same size — layers of a doll
// always are, and a mismatch means the wrong file was loaded.
void compositeOver(Image& dst, const Image& src);

[[nodiscard]] bool writePng(const char* path, u32 width, u32 height, const void* rgba);

}
