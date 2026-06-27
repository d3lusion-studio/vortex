#include "vortex/text/text_renderer.hpp"

#include "vortex/renderer/sprite_batch.hpp"

namespace vortex::text {

void drawText(renderer::SpriteBatch& batch, const Font& font, std::string_view text,
              Vec2 topLeft, Vec4 color, f32 scale, i32 layer) {
    const f32 startX   = topLeft.x;
    f32       penX     = startX;
    // Baseline sits one ascent below the requested top edge (world is +Y up).
    f32       baseline = topLeft.y - font.ascent() * scale;

    for (char c : text) {
        if (c == '\n') {
            penX     = startX;
            baseline -= font.lineHeight() * scale;
            continue;
        }

        const Font::Glyph* g = font.glyph(c);
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

}
