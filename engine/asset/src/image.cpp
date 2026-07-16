#include "vortex/asset/image.hpp"

#include "vortex/core/log.hpp"

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


void compositeOver(Image& dst, const Image& src) {
    if (!dst.valid() || !src.valid() || src.width != dst.width || src.height != dst.height) {
        VORTEX_WARN("Image", "compositeOver: size mismatch (%ux%u onto %ux%u); skipped",
                    src.width, src.height, dst.width, dst.height);
        return;
    }

    // Straight alpha, not premultiplied. Pixel art here is hard-edged — almost every
    // texel is a=0 or a=255 — so the two fast paths carry the whole image and the blend
    // below is only for the rare soft edge. Premultiplying instead would round the dark
    // outlines these packs are drawn with into the colours underneath.
    for (usize i = 0; i < dst.pixels.size(); i += 4) {
        const u32 a = src.pixels[i + 3];
        if (a == 0) continue;
        if (a == 255) {
            dst.pixels[i + 0] = src.pixels[i + 0];
            dst.pixels[i + 1] = src.pixels[i + 1];
            dst.pixels[i + 2] = src.pixels[i + 2];
            dst.pixels[i + 3] = 255;
            continue;
        }
        for (u32 c = 0; c < 3; ++c) {
            const u32 s = src.pixels[i + c];
            const u32 d = dst.pixels[i + c];
            dst.pixels[i + c] = static_cast<u8>((s * a + d * (255 - a)) / 255);
        }
        dst.pixels[i + 3] = static_cast<u8>(a + dst.pixels[i + 3] * (255 - a) / 255);
    }
}

}
