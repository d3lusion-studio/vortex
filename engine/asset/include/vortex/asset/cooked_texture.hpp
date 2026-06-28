#pragma once
#include "vortex/asset/image.hpp"
#include "vortex/core/types.hpp"

#include <cstddef>
#include <vector>

namespace vortex::assets {

#pragma pack(push, 1)
struct CookedTextureHeader {
    char magic[4] = {'V', 'T', 'E', 'X'};
    u32  version  = 1;
    u32  width    = 0;
    u32  height   = 0;
    u32  channels = 4;     // always RGBA8 for now
    u32  dataSize = 0;     // width * height * channels
};
#pragma pack(pop)

constexpr u32 kCookedTextureVersion = 1;

[[nodiscard]] std::vector<std::byte> encodeCookedTexture(const Image& image);

[[nodiscard]] Image decodeCookedTexture(const std::byte* data, usize size);

}
