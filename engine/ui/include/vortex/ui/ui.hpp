#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <string_view>

namespace vortex::rhi { class IGraphicsDevice; }
namespace vortex::text { class Font; }

namespace vortex::ui {

struct InputState {
    Vec2 mouse{0.0f, 0.0f};
    bool down     = false;   // button currently held
    bool pressed  = false;   // went down this frame
    bool released = false;   // went up this frame
};

// One skinned surface: a window onto a texture, plus the insets that keep its border
// from smearing when it is stretched. Leave `texture` unset and the widget falls back to
// the flat colour beside it.
struct Skin {
    rhi::TextureHandle  texture;
    Rect                uv = kFullUV;
    renderer::NineSlice slice;

    [[nodiscard]] bool valid() const noexcept { return texture.valid() && slice.valid(); }
};

// Colours are LINEAR light, like every colour the renderer takes. Authoring them through
// Color::fromRgb is what keeps them readable — the hex is the colour you would pick in a
// design tool, and fromRgb decodes it. Assigning a raw Vec4 here is legal but means you
// are stating linear values yourself, which will not look like the hex you had in mind.
struct Style {
    Vec4 panel   = Color::fromRgb(0x1F2230).withAlpha(0.92f);
    Vec4 button  = Color::fromRgb(0x33384D);
    Vec4 hovered = Color::fromRgb(0x47526F);
    Vec4 active  = Color::fromRgb(0x2A669E);
    Vec4 text    = Color::fromRgb(0xEBEFFA);
    f32  textScale = 1.0f;
    i32  baseLayer = 1000;   // UI draws on top of game sprites by default

    // Optional art. Set these and panel()/button() draw the pack's own 9-patch instead
    // of a flat rectangle; the colours above then tint it, so a disabled button is still
    // one assignment rather than a second texture.
    //
    // A widget with a skin ignores its flat colour only for the FILL — the tint still
    // applies, which is what makes hovered/active work without three more textures.
    Skin panelSkin;
    Skin buttonSkin;
    Skin buttonHoveredSkin;   // falls back to buttonSkin when unset
    Skin buttonActiveSkin;    // falls back to buttonHoveredSkin, then buttonSkin

    // Pixel art wants Nearest; the default flat skin has no texture to filter.
    renderer::SpriteSampler sampler = renderer::SpriteSampler::LinearClamp;
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
    void fillRect(Vec2 center, Vec2 size, Vec4 color, const Skin& skin);
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
