#include "vortex/asset/cooked_texture.hpp"

#include <cstring>

namespace vortex::assets {

std::vector<std::byte> encodeCookedTexture(const Image& image) {
    std::vector<std::byte> out;
    if (!image.valid()) return out;

    CookedTextureHeader header;
    header.version  = kCookedTextureVersion;
    header.width    = image.width;
    header.height   = image.height;
    header.channels = 4;
    header.dataSize = static_cast<u32>(image.pixels.size());

    out.resize(sizeof(header) + image.pixels.size());
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + sizeof(header), image.pixels.data(), image.pixels.size());
    return out;
}

Image decodeCookedTexture(const std::byte* data, usize size) {
    Image image;
    if (!data || size < sizeof(CookedTextureHeader)) return image;

    CookedTextureHeader header;
    std::memcpy(&header, data, sizeof(header));

    const CookedTextureHeader ref;
    if (std::memcmp(header.magic, ref.magic, sizeof(ref.magic)) != 0) return image;
    if (header.version != kCookedTextureVersion || header.channels != 4) return image;

    const usize expected = static_cast<usize>(header.width) * header.height * 4;
    if (header.dataSize != expected || size < sizeof(header) + expected) return image;

    image.width  = header.width;
    image.height = header.height;
    image.pixels.resize(expected);
    std::memcpy(image.pixels.data(), data + sizeof(header), expected);
    return image;
}

}
