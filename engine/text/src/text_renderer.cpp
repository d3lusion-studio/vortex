#include "vortex/text/text_renderer.hpp"

#include "vortex/renderer/sprite_batch.hpp"

namespace vortex::text {

void drawText(renderer::SpriteBatch& batch, const Font& font, std::string_view text,
              Vec2 topLeft, Vec4 color, f32 scale, i32 layer) {
    const f32 startX   = topLeft.x;
    f32       penX     = startX;
    // Baseline sits one ascent below the requested top edge (world is +Y up).
    f32       baseline = topLeft.y - font.ascent() * scale;

    // Codepoints, not bytes. `for (char c : text)` walks the UTF-8 ENCODING — for "ô" it
    // looks up 0xC3 and 0xB4, finds neither, and drops both. The string still renders; it
    // has just silently lost every accent in it.
    for (usize i = 0; i < text.size();) {
        const u32 cp = decodeUtf8(text, i);

        if (cp == '\n') {
            penX     = startX;
            baseline -= font.lineHeight() * scale;
            continue;
        }

        const Font::Glyph* g = font.glyph(cp);
        if (!g) continue;

        const f32 w = g->size.x * scale;
        const f32 h = g->size.y * scale;
        if (w > 0.0f && h > 0.0f) {
            // offset.y (stb yoff) is the y-down distance from baseline to the glyph
            // top; negate it to get the world-up top edge.
            const f32 worldTop = baseline - g->offset.y * scale;
            const Vec2 center{penX + g->offset.x * scale + w * 0.5f, worldTop - h * 0.5f};
            batch.drawSprite(font.atlas(), center, {w, h}, color, g->uv, layer);
        }
        penX += g->advance * scale;
    }
}

void drawTextWrapped(renderer::SpriteBatch& batch, const Font& font, std::string_view text,
                     Vec2 topLeft, f32 maxWidth, Vec4 color, f32 scale, i32 layer) {
    // Nothing to draw, and — more to the point — nothing to divide by. Text faded in by
    // animating the scale passes 0 on its first frame, and maxWidth/0 is an infinite box
    // that puts the whole string on one line and runs it off the panel.
    if (scale <= 0.0f) return;

    // wrap() measures in the font's own unscaled pixels, so the box is divided by the scale
    // rather than the widths multiplied by it — otherwise a half-size font would wrap as
    // though it were still full size.
    const std::vector<std::string_view> lines = font.wrap(text, maxWidth / scale);

    Vec2 pen = topLeft;
    for (const std::string_view& line : lines) {
        drawText(batch, font, line, pen, color, scale, layer);
        pen.y -= font.lineHeight() * scale;
    }
}

}
