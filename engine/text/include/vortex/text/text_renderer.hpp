#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/text/font.hpp"

#include <string_view>

namespace vortex::renderer { class SpriteBatch; }

namespace vortex::text {

void drawText(renderer::SpriteBatch& batch, const Font& font, std::string_view text,
              Vec2 topLeft, Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, f32 scale = 1.0f,
              i32 layer = 0);

}
