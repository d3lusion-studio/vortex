#include "vortex/text/font.hpp"

#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <cstdlib>
#include <vector>

namespace vortex::text {

namespace {

// Where a desktop keeps its fonts. Ordered so the result is the same everywhere it can
// be: DejaVu first (it is what most Linux distributions ship and what the examples were
// written against), then the usual fallbacks, then the other platforms.
constexpr const char* kFontSearchPaths[] = {
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/Library/Fonts/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "C:/Windows/Fonts/segoeui.ttf",
};

}   // namespace

std::string Font::defaultPath(pf::IFileSystem& fs) {
    // The environment wins: it is how a container without any of the paths below, or a
    // developer who wants a specific face, says so without touching code.
    if (const char* env = std::getenv("VORTEX_FONT_PATH"))
        if (fs.exists(env)) return env;

    for (const char* path : kFontSearchPaths)
        if (fs.exists(path)) return path;
    return {};
}

std::unique_ptr<Font> Font::loadDefault(rhi::IGraphicsDevice& device, pf::IFileSystem& fs,
                                        f32 pixelHeight) {
    const std::string path = defaultPath(fs);
    if (path.empty()) {
        VORTEX_WARN("Font", "No system font found. Set VORTEX_FONT_PATH to pick one.");
        return nullptr;
    }
    return loadFromFile(device, fs, path.c_str(), pixelHeight);
}

std::unique_ptr<Font> Font::loadFromFile(rhi::IGraphicsDevice& device, pf::IFileSystem& fs,
                                         const char* path, f32 pixelHeight) {
    const std::vector<std::byte> ttf = fs.readFile(path);
    if (ttf.empty()) {
        VORTEX_ERROR("Font", "Failed to read '%s'", path);
        return nullptr;
    }

    const auto* data = reinterpret_cast<const unsigned char*>(ttf.data());

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        VORTEX_ERROR("Font", "Not a valid TrueType font: '%s'", path);
        return nullptr;
    }

    std::vector<stbtt_bakedchar> baked(kCharCount);
    std::vector<u8>              alpha;
    u32                          dim = 256;
    bool                         ok  = false;
    for (; dim <= 4096; dim *= 2) {
        alpha.assign(static_cast<usize>(dim) * dim, 0u);
        const int res = stbtt_BakeFontBitmap(data, 0, pixelHeight, alpha.data(),
                                             static_cast<int>(dim), static_cast<int>(dim),
                                             static_cast<int>(kFirstChar),
                                             static_cast<int>(kCharCount), baked.data());
        if (res > 0) { ok = true; break; }
    }
    if (!ok) {
        VORTEX_ERROR("Font", "Atlas overflow baking '%s' at %.0fpx", path, pixelHeight);
        return nullptr;
    }

    std::vector<u8> rgba(static_cast<usize>(dim) * dim * 4);
    for (usize i = 0; i < alpha.size(); ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = alpha[i];
    }

    std::unique_ptr<Font> font(new Font(device));
    font->m_atlas = device.createTexture(
        {.width = dim, .height = dim, .debugName = "font_atlas"}, rgba.data());
    font->m_pixelHeight = pixelHeight;

    const f32 invDim = 1.0f / static_cast<f32>(dim);
    for (u32 i = 0; i < kCharCount; ++i) {
        const stbtt_bakedchar& b = baked[i];
        Glyph& g   = font->m_glyphs[i];
        g.uv       = {static_cast<f32>(b.x0) * invDim, static_cast<f32>(b.y0) * invDim,
                      static_cast<f32>(b.x1 - b.x0) * invDim,
                      static_cast<f32>(b.y1 - b.y0) * invDim};
        g.size     = {static_cast<f32>(b.x1 - b.x0), static_cast<f32>(b.y1 - b.y0)};
        g.offset   = {b.xoff, b.yoff};
        g.advance  = b.xadvance;
    }

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const f32 scale     = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    font->m_ascent      = static_cast<f32>(ascent) * scale;
    font->m_lineHeight  = static_cast<f32>(ascent - descent + lineGap) * scale;

    VORTEX_INFO("Font", "Baked '%s' at %.0fpx into %ux%u atlas", path, pixelHeight, dim, dim);
    return font;
}

Font::~Font() {
    if (m_atlas.valid()) m_device.destroyTexture(m_atlas);
}

const Font::Glyph* Font::glyph(char c) const {
    const auto cp = static_cast<u32>(static_cast<unsigned char>(c));
    if (cp < kFirstChar || cp >= kFirstChar + kCharCount) return nullptr;
    return &m_glyphs[cp - kFirstChar];
}

Vec2 Font::measure(std::string_view text) const {
    f32 lineWidth = 0.0f, maxWidth = 0.0f;
    u32 lines = text.empty() ? 0u : 1u;
    for (char c : text) {
        if (c == '\n') {
            maxWidth  = lineWidth > maxWidth ? lineWidth : maxWidth;
            lineWidth = 0.0f;
            ++lines;
            continue;
        }
        if (const Glyph* g = glyph(c)) lineWidth += g->advance;
    }
    maxWidth = lineWidth > maxWidth ? lineWidth : maxWidth;
    return {maxWidth, static_cast<f32>(lines) * m_lineHeight};
}

}
