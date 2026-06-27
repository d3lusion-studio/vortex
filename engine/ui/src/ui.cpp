#include "vortex/ui/ui.hpp"

#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/text/font.hpp"
#include "vortex/text/text_renderer.hpp"

#include <cmath>

namespace vortex::ui {

UI::UI(rhi::IGraphicsDevice& device) : m_device(device) {
    const u8 px[4] = {255, 255, 255, 255};
    m_white = m_device.createTexture({.width = 1, .height = 1, .debugName = "ui_white"}, px);
}

UI::~UI() {
    if (m_white.valid()) m_device.destroyTexture(m_white);
}

void UI::begin(renderer::SpriteBatch& batch, const text::Font& font, const InputState& input) {
    m_batch    = &batch;
    m_font     = &font;
    m_input    = input;
    m_widgetId = 0;
    m_column   = false;
}

void UI::end() {
    if (m_input.released) m_active = -1;
    m_batch = nullptr;
    m_font  = nullptr;
}

void UI::fillRect(Vec2 center, Vec2 size, Vec4 color) {
    m_batch->drawSprite(m_white, center, size, color, kFullUV, style.baseLayer);
}

void UI::drawCentered(std::string_view text, Vec2 center) {
    const Vec2 extent = m_font->measure(text);
    const Vec2 topLeft{center.x - extent.x * style.textScale * 0.5f,
                       center.y + extent.y * style.textScale * 0.5f};
    text::drawText(*m_batch, *m_font, text, topLeft, style.text, style.textScale,
                   style.baseLayer + 1);
}

bool UI::hot(Vec2 center, Vec2 size) const {
    return std::fabs(m_input.mouse.x - center.x) <= size.x * 0.5f &&
           std::fabs(m_input.mouse.y - center.y) <= size.y * 0.5f;
}

void UI::panel(Vec2 center, Vec2 size, Vec4 color) { fillRect(center, size, color); }

void UI::label(Vec2 center, std::string_view text, Vec4 color) {
    const Vec2 extent = m_font->measure(text);
    const Vec2 topLeft{center.x - extent.x * style.textScale * 0.5f,
                       center.y + extent.y * style.textScale * 0.5f};
    text::drawText(*m_batch, *m_font, text, topLeft, color, style.textScale, style.baseLayer + 1);
}

void UI::label(Vec2 center, std::string_view text) { label(center, text, style.text); }

bool UI::button(Vec2 center, Vec2 size, std::string_view label) {
    const i32  id      = m_widgetId++;
    const bool hovered = hot(center, size);

    if (m_input.pressed && hovered) m_active = id;
    const bool clicked = m_input.released && m_active == id && hovered;

    Vec4 color = style.button;
    if (hovered) color = (m_active == id && m_input.down) ? style.active : style.hovered;

    fillRect(center, size, color);
    drawCentered(label, center);
    return clicked;
}

void UI::beginColumn(Vec2 topCenter, Vec2 widgetSize, f32 spacing) {
    m_column   = true;
    m_slotSize = widgetSize;
    m_spacing  = spacing;
    // topCenter is the centre of the first slot.
    m_cursor   = topCenter;
}

Vec2 UI::nextSlot() {
    const Vec2 slot = m_cursor;
    m_cursor.y -= m_slotSize.y + m_spacing;   // world is +Y up: stack downward
    return slot;
}

bool UI::button(std::string_view label) {
    const Vec2 slot = nextSlot();
    return button(slot, m_slotSize, label);
}

void UI::label(std::string_view text) {
    const Vec2 slot = nextSlot();
    label(slot, text);
}

}
