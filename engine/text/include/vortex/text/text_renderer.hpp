#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/text/font.hpp"

#include <string_view>

namespace vortex::renderer { class SpriteBatch; }

namespace vortex::text {

// `text` is UTF-8. '\n' starts a new line; a codepoint the font has no glyph for is
// skipped.
void drawText(renderer::SpriteBatch& batch, const Font& font, std::string_view text,
              Vec2 topLeft, Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, f32 scale = 1.0f,
              i32 layer = 0);

// The same, broken to fit `maxWidth` (in the SAME units the text is drawn at, i.e. after
// `scale`). Breaks on spaces, and inside a word only when one word is wider than the box.
//
// This is what a dialogue box is: text and a width. Doing it at the call site means every
// game re-implements word wrapping, and most of them get the too-long-word case wrong.
void drawTextWrapped(renderer::SpriteBatch& batch, const Font& font, std::string_view text,
                     Vec2 topLeft, f32 maxWidth, Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f},
                     f32 scale = 1.0f, i32 layer = 0);

}
