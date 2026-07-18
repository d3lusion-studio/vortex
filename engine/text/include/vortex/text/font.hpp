#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; }
namespace vortex::pf  { class IFileSystem; }

namespace vortex::text {

// --- UTF-8 --------------------------------------------------------------------
//
// Text is bytes; glyphs are codepoints. Anything that walks a string one `char` at a time
// is walking BYTES, and every character outside ASCII is two to four of them — so it looks
// up 0xC3 and 0xB4 instead of 'ô' and finds neither. The whole string still renders; the
// accents just quietly are not in it.

// Decode the codepoint starting at `index`, and advance `index` past it.
//
// Returns U+FFFD for malformed input rather than throwing or stopping: a replacement
// character is visible on screen, which is how a mangled string gets reported instead of
// silently losing letters.
[[nodiscard]] u32 decodeUtf8(std::string_view text, usize& index);

// How many codepoints `text` holds. Not its size() — that is bytes.
[[nodiscard]] usize utf8Length(std::string_view text);

// --- Coverage -----------------------------------------------------------------

// A contiguous block of codepoints to bake, e.g. {0x20, 95} for printable ASCII.
struct CodepointRange {
    u32 first = 0;
    u32 count = 0;
};

// What a Latin-script game actually needs to print, which is more than ASCII:
//
//   Basic Latin            the alphabet, digits, punctuation
//   Latin-1 Supplement     à é ï ñ ö ü ÷ — most of Western Europe
//   Latin Extended-A       ā ć đ ę ł ő š ż — Central Europe, and Vietnamese's ă đ ĩ ũ
//   Latin Extended-B (bit) ơ ư — Vietnamese needs exactly these four
//   General Punctuation    – — ' ' " " … the quotes and dashes real text is written with
//   Latin Extended Add.    ạ ả ấ ầ ậ ắ ế ệ ọ ố ộ ớ ợ ụ ứ ự — Vietnamese tone marks
//
// About 640 requested, ~450 of which a face like DejaVu actually has. That is the
// difference between an engine that can write "Nông trại" and one that renders "Nng tri".
//
// It is not free: the atlas grows with the glyph count, and at 52px those 450 glyphs need
// 1024x1024 (4 MB) where 95 needed 256x256. Worth it by default — text that is quietly
// wrong is worse than text that costs a megabyte — but a font that only ever prints
// "Loading" can say so:
//
//     Font::loadDefault(device, fs, 52.0f, {{0x20, 95}});   // ASCII only
[[nodiscard]] const std::vector<CodepointRange>& latinRanges();

class Font {
public:
    struct Glyph {
        Rect uv;
        Vec2 size{0.0f, 0.0f};
        Vec2 offset{0.0f, 0.0f};
        f32  advance = 0.0f;
    };

    // `ranges` decides what this font can print. Baking more costs atlas area and load
    // time, not per-frame time — the cost of a glyph you never draw is the space it took.
    [[nodiscard]] static std::unique_ptr<Font> loadFromFile(
        rhi::IGraphicsDevice& device, pf::IFileSystem& fs, const char* path, f32 pixelHeight,
        const std::vector<CodepointRange>& ranges = latinRanges());

    // The first font this machine actually has, so a demo, a debug overlay or a HUD can
    // put text on screen without shipping a .ttf or hard-coding someone else's path.
    // Honours the VORTEX_FONT_PATH environment variable first.
    //
    // Null when nothing is found, which is a normal outcome on a bare container — check
    // it rather than assuming a font exists.
    [[nodiscard]] static std::unique_ptr<Font> loadDefault(
        rhi::IGraphicsDevice& device, pf::IFileSystem& fs, f32 pixelHeight,
        const std::vector<CodepointRange>& ranges = latinRanges());

    // The path loadDefault() would use, or empty. Split out so a caller can report which
    // font it got, or decide it would rather ship its own.
    [[nodiscard]] static std::string defaultPath(pf::IFileSystem& fs);

    ~Font();
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;

    // Null when the codepoint was not in the baked ranges, or the font has no such glyph.
    [[nodiscard]] const Glyph* glyph(u32 codepoint) const;

    // True if every codepoint in `text` has a glyph. For checking a translation against a
    // font before shipping, rather than discovering the holes on screen.
    [[nodiscard]] bool covers(std::string_view text) const;

    [[nodiscard]] rhi::TextureHandle atlas() const { return m_atlas; }
    [[nodiscard]] f32 pixelHeight() const { return m_pixelHeight; }
    [[nodiscard]] f32 ascent()      const { return m_ascent; }
    [[nodiscard]] f32 lineHeight()  const { return m_lineHeight; }

    // Width and height of `text` in pixels, honouring '\n'.
    [[nodiscard]] Vec2 measure(std::string_view text) const;

    // `text` broken into lines that each fit `maxWidth` (in the font's own pixels). Breaks
    // on spaces where it can, and inside a word only when one word is wider than the box.
    //
    // The lines BORROW from `text` — they are views into it, not copies. So `text` has to
    // outlive them, and passing a temporary is a use-after-free the compiler will not warn
    // about:
    //
    //     for (auto line : font.wrap(std::string(name), 200.0f))   // DANGLING
    //     const std::string s = name; for (auto line : font.wrap(s, 200.0f))   // fine
    [[nodiscard]] std::vector<std::string_view> wrap(std::string_view text, f32 maxWidth) const;

private:
    Font(rhi::IGraphicsDevice& device) : m_device(device) {}

    rhi::IGraphicsDevice& m_device;
    rhi::TextureHandle    m_atlas;

    // Sparse, because the ranges are: Vietnamese lives at U+1EA0 and an array indexed by
    // codepoint would be 7,900 entries to hold 640.
    std::unordered_map<u32, Glyph> m_glyphs;

    f32 m_pixelHeight = 0.0f;
    f32 m_ascent      = 0.0f;
    f32 m_lineHeight  = 0.0f;
};

}
