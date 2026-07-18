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

constexpr u32 kReplacement = 0xFFFDu;   // U+FFFD, shown when the bytes do not decode

}   // namespace

// --- UTF-8 --------------------------------------------------------------------

u32 decodeUtf8(std::string_view text, usize& index) {
    if (index >= text.size()) return 0;

    const auto byte = [&](usize i) { return static_cast<u8>(text[i]); };
    const u8   lead = byte(index);

    u32   cp    = 0;
    usize extra = 0;
    // `least` is the smallest codepoint this LENGTH is allowed to encode. Every codepoint
    // has exactly one legal encoding, and a longer one that decodes to the same value is an
    // "overlong": 0xC0 0x80 spells U+0000 in two bytes, 0xE0 0x80 0xAF spells '/' in three.
    // The bytes look well-formed, so a decoder that only checks the shape accepts them —
    // which is how a NUL or a '/' gets past a filter that inspected the bytes and then
    // reappears after decoding.
    u32 least = 0;
    if (lead < 0x80u)      { cp = lead;          extra = 0; least = 0x0000u; }
    else if (lead < 0xC0u) { ++index; return kReplacement; }   // a stray continuation byte
    else if (lead < 0xE0u) { cp = lead & 0x1Fu;  extra = 1; least = 0x0080u; }
    else if (lead < 0xF0u) { cp = lead & 0x0Fu;  extra = 2; least = 0x0800u; }
    else if (lead < 0xF8u) { cp = lead & 0x07u;  extra = 3; least = 0x10000u; }
    else                   { ++index; return kReplacement; }

    if (index + extra >= text.size() && extra > 0) {   // truncated at the end of the string
        index = text.size();
        return kReplacement;
    }

    for (usize i = 1; i <= extra; ++i) {
        const u8 cont = byte(index + i);
        if ((cont & 0xC0u) != 0x80u) {   // the sequence is broken; resync on the next byte
            ++index;
            return kReplacement;
        }
        cp = (cp << 6) | (cont & 0x3Fu);
    }
    index += extra + 1;

    // An overlong sequence, a surrogate half, or a value past the last codepoint all mean
    // the encoder was lying. Passing any of them on would only move the confusion into the
    // glyph lookup.
    if (cp < least) return kReplacement;
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return kReplacement;
    return cp;
}

usize utf8Length(std::string_view text) {
    usize n = 0;
    for (usize i = 0; i < text.size();) {
        decodeUtf8(text, i);
        ++n;
    }
    return n;
}

const std::vector<CodepointRange>& latinRanges() {
    static const std::vector<CodepointRange> ranges = {
        {0x0020, 95},    // Basic Latin, printable
        {0x00A0, 96},    // Latin-1 Supplement
        {0x0100, 128},   // Latin Extended-A
        {0x01A0, 17},    // Latin Extended-B: Ơ ơ Ư ư and their neighbours
        {0x2010, 24},    // General Punctuation: the dashes, quotes and the ellipsis
        {0x1EA0, 90},    // Latin Extended Additional: the Vietnamese tone marks
    };
    return ranges;
}

// --- Loading ------------------------------------------------------------------

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
                                        f32 pixelHeight,
                                        const std::vector<CodepointRange>& ranges) {
    const std::string path = defaultPath(fs);
    if (path.empty()) {
        VORTEX_WARN("Font", "No system font found. Set VORTEX_FONT_PATH to pick one.");
        return nullptr;
    }
    return loadFromFile(device, fs, path.c_str(), pixelHeight, ranges);
}

std::unique_ptr<Font> Font::loadFromFile(rhi::IGraphicsDevice& device, pf::IFileSystem& fs,
                                         const char* path, f32 pixelHeight,
                                         const std::vector<CodepointRange>& ranges) {
    const std::vector<std::byte> ttf = fs.readFile(path);
    if (ttf.empty()) {
        VORTEX_ERROR("Font", "Failed to read '%s'", path);
        return nullptr;
    }
    if (ranges.empty()) {
        VORTEX_ERROR("Font", "No codepoint ranges requested for '%s'", path);
        return nullptr;
    }

    const auto* data = reinterpret_cast<const unsigned char*>(ttf.data());

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        VORTEX_ERROR("Font", "Not a valid TrueType font: '%s'", path);
        return nullptr;
    }

    // PackFontRanges, not BakeFontBitmap: baking takes ONE contiguous range, which is
    // exactly why this used to be ASCII-only. Vietnamese lives at U+1EA0, seven thousand
    // codepoints from the alphabet, and no single range reaches both.
    std::vector<std::vector<stbtt_packedchar>> packed(ranges.size());
    std::vector<stbtt_pack_range>              packRanges(ranges.size());

    std::vector<u8> alpha;
    u32             dim = 256;
    bool            ok  = false;
    for (; dim <= 4096; dim *= 2) {
        alpha.assign(static_cast<usize>(dim) * dim, 0u);

        for (usize r = 0; r < ranges.size(); ++r) {
            packed[r].assign(ranges[r].count, stbtt_packedchar{});
            packRanges[r] = stbtt_pack_range{
                .font_size                        = pixelHeight,
                .first_unicode_codepoint_in_range = static_cast<int>(ranges[r].first),
                .array_of_unicode_codepoints      = nullptr,
                .num_chars                        = static_cast<int>(ranges[r].count),
                .chardata_for_range               = packed[r].data(),
                .h_oversample                     = 0,
                .v_oversample                     = 0};
        }

        stbtt_pack_context pc;
        if (!stbtt_PackBegin(&pc, alpha.data(), static_cast<int>(dim), static_cast<int>(dim), 0,
                             1, nullptr))
            continue;
        // No oversampling: this text is drawn at the size it was baked at, and oversampling
        // would only soften a pixel-art HUD.
        stbtt_PackSetOversampling(&pc, 1, 1);
        const int res = stbtt_PackFontRanges(&pc, data, 0, packRanges.data(),
                                             static_cast<int>(packRanges.size()));
        stbtt_PackEnd(&pc);

        if (res > 0) { ok = true; break; }
    }
    if (!ok) {
        VORTEX_ERROR("Font", "Atlas overflow baking '%s' at %.0fpx", path,
                     static_cast<f64>(pixelHeight));
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
    for (usize r = 0; r < ranges.size(); ++r) {
        for (u32 i = 0; i < ranges[r].count; ++i) {
            const stbtt_packedchar& b  = packed[r][i];
            const u32               cp = ranges[r].first + i;

            // A codepoint the FACE does not have packs as an empty box with no advance.
            // Storing it would claim coverage this font does not have, and covers() would
            // then vouch for a translation it cannot print.
            if (b.x1 <= b.x0 && b.y1 <= b.y0 && b.xadvance == 0.0f) continue;

            Glyph g;
            g.uv      = {static_cast<f32>(b.x0) * invDim, static_cast<f32>(b.y0) * invDim,
                         static_cast<f32>(b.x1 - b.x0) * invDim,
                         static_cast<f32>(b.y1 - b.y0) * invDim};
            g.size    = {static_cast<f32>(b.x1 - b.x0), static_cast<f32>(b.y1 - b.y0)};
            g.offset  = {b.xoff, b.yoff};
            g.advance = b.xadvance;
            font->m_glyphs.emplace(cp, g);
        }
    }

    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const f32 scale     = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    font->m_ascent      = static_cast<f32>(ascent) * scale;
    font->m_lineHeight  = static_cast<f32>(ascent - descent + lineGap) * scale;

    VORTEX_INFO("Font", "Baked '%s' at %.0fpx: %zu glyphs into a %ux%u atlas", path,
                static_cast<f64>(pixelHeight), font->m_glyphs.size(), dim, dim);
    return font;
}

Font::~Font() {
    if (m_atlas.valid()) m_device.destroyTexture(m_atlas);
}

const Font::Glyph* Font::glyph(u32 codepoint) const {
    const auto it = m_glyphs.find(codepoint);
    return it == m_glyphs.end() ? nullptr : &it->second;
}

bool Font::covers(std::string_view text) const {
    for (usize i = 0; i < text.size();) {
        const u32 cp = decodeUtf8(text, i);
        if (cp == '\n' || cp == '\r' || cp == '\t') continue;
        if (glyph(cp) == nullptr) return false;
    }
    return true;
}

Vec2 Font::measure(std::string_view text) const {
    f32 lineWidth = 0.0f, maxWidth = 0.0f;
    u32 lines = text.empty() ? 0u : 1u;

    for (usize i = 0; i < text.size();) {
        const u32 cp = decodeUtf8(text, i);
        if (cp == '\n') {
            maxWidth  = lineWidth > maxWidth ? lineWidth : maxWidth;
            lineWidth = 0.0f;
            ++lines;
            continue;
        }
        if (const Glyph* g = glyph(cp)) lineWidth += g->advance;
    }
    maxWidth = lineWidth > maxWidth ? lineWidth : maxWidth;

    return {maxWidth, static_cast<f32>(lines) * m_lineHeight};
}

std::vector<std::string_view> Font::wrap(std::string_view text, f32 maxWidth) const {
    constexpr usize kNone = static_cast<usize>(-1);

    std::vector<std::string_view> lines;
    if (text.empty()) return lines;

    usize lineStart = 0;       // byte offset the current line starts at
    usize lastSpace = kNone;   // byte offset of the last space this line could break on
    f32   width     = 0.0f;

    for (usize i = 0; i < text.size();) {
        const usize charStart = i;
        const u32   cp        = decodeUtf8(text, i);

        if (cp == '\n') {
            lines.push_back(text.substr(lineStart, charStart - lineStart));
            lineStart = i;
            lastSpace = kNone;
            width     = 0.0f;
            continue;
        }

        if (cp == ' ') lastSpace = charStart;

        const Glyph* g       = glyph(cp);
        const f32    advance = g ? g->advance : 0.0f;

        if (width + advance > maxWidth && charStart > lineStart) {
            if (lastSpace != kNone) {
                // Break at the space and swallow it: a wrapped line should not begin with
                // the whitespace it broke on.
                lines.push_back(text.substr(lineStart, lastSpace - lineStart));
                lineStart = lastSpace + 1;
            } else {
                // One word wider than the whole box. Break inside it rather than let it run
                // off the edge — a long name or a URL is the usual culprit.
                lines.push_back(text.substr(lineStart, charStart - lineStart));
                lineStart = charStart;
            }
            i         = lineStart;
            lastSpace = kNone;
            width     = 0.0f;
            continue;
        }

        width += advance;
    }

    if (lineStart < text.size()) lines.push_back(text.substr(lineStart));
    return lines;
}

}
