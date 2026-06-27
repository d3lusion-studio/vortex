#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <string_view>

namespace vortex::rhi { class IGraphicsDevice; }
namespace vortex::renderer { class SpriteBatch; }
namespace vortex::text { class Font; }

namespace vortex::ui {

struct InputState {
    Vec2 mouse{0.0f, 0.0f};
    bool down     = false;   // button currently held
    bool pressed  = false;   // went down this frame
    bool released = false;   // went up this frame
};

struct Style {
    Vec4 panel   {0.12f, 0.13f, 0.18f, 0.92f};
    Vec4 button  {0.20f, 0.22f, 0.30f, 1.0f};
    Vec4 hovered {0.28f, 0.32f, 0.44f, 1.0f};
    Vec4 active  {0.16f, 0.40f, 0.62f, 1.0f};
    Vec4 text    {0.92f, 0.94f, 0.98f, 1.0f};
    f32  textScale = 1.0f;
    i32  baseLayer = 1000;   // UI draws on top of game sprites by default
};

// Retained-nothing immediate-mode UI. Widgets are identified by call order, so
// call the same widgets in the same sequence every frame. All widgets are
// positioned by CENTRE + size, matching SpriteBatch::drawSprite.
class UI {
public:
    UI(rhi::IGraphicsDevice& device);
    ~UI();
    UI(const UI&)            = delete;
    UI& operator=(const UI&) = delete;

    void begin(renderer::SpriteBatch& batch, const text::Font& font, const InputState& input);
    void end();

    // --- Explicit-position widgets ---
    void panel(Vec2 center, Vec2 size, Vec4 color);
    void panel(Vec2 center, Vec2 size) { panel(center, size, style.panel); }
    void label(Vec2 center, std::string_view text);
    void label(Vec2 center, std::string_view text, Vec4 color);
    [[nodiscard]] bool button(Vec2 center, Vec2 size, std::string_view label);

    // --- Vertical-stack layout (top to bottom) ---
    void beginColumn(Vec2 topCenter, Vec2 widgetSize, f32 spacing);
    [[nodiscard]] bool button(std::string_view label);
    void label(std::string_view text);

    Style style;

private:
    void fillRect(Vec2 center, Vec2 size, Vec4 color);
    void drawCentered(std::string_view text, Vec2 center);
    [[nodiscard]] bool hot(Vec2 center, Vec2 size) const;
    [[nodiscard]] Vec2 nextSlot();

    rhi::IGraphicsDevice&    m_device;
    rhi::TextureHandle       m_white{};
    renderer::SpriteBatch*   m_batch = nullptr;
    const text::Font*        m_font  = nullptr;
    InputState               m_input;
    i32                      m_widgetId = 0;
    i32                      m_active   = -1;   // widget pressed and not yet released

    // Column layout cursor.
    bool m_column = false;
    Vec2 m_cursor{0.0f, 0.0f};
    Vec2 m_slotSize{0.0f, 0.0f};
    f32  m_spacing = 0.0f;
};

}
