#include "vortex/asset/image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

namespace vortex::assets {

Image decodeImage(const std::byte* data, usize size) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(data),
                                             static_cast<int>(size),
                                             &width, &height, &channels, 4);
    Image image;
    if (!decoded) return image;

    image.width  = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.pixels.assign(decoded, decoded + static_cast<usize>(width) * height * 4);
    stbi_image_free(decoded);
    return image;
}

bool writePng(const char* path, u32 width, u32 height, const void* rgba) {
    return stbi_write_png(path, static_cast<int>(width), static_cast<int>(height), 4, rgba,
                          static_cast<int>(width * 4)) != 0;
}

}
