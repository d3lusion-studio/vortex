#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vortex   { class CVar; }
namespace vortex::rhi      { class IGraphicsDevice; }
namespace vortex::renderer { class SpriteBatch; }
namespace vortex::text     { class Font; }

namespace vortex::debug {

class DebugDraw {
public:
    using Category = u32;
    static constexpr Category kDefault = 0;

    explicit DebugDraw(rhi::IGraphicsDevice& device);
    ~DebugDraw();
    DebugDraw(const DebugDraw&)            = delete;
    DebugDraw& operator=(const DebugDraw&) = delete;

    Category category(const char* name);
    void     setEnabled(Category cat, bool enabled);
    [[nodiscard]] bool enabled(Category cat) const;

    void begin();   // drop last frame's primitives

    void line(Vec2 a, Vec2 b, Vec4 color, f32 thickness = 1.5f, Category cat = kDefault);
    void box(Vec2 center, Vec2 size, Vec4 color, f32 thickness = 1.5f, Category cat = kDefault);
    void filledBox(Vec2 center, Vec2 size, Vec4 color, Category cat = kDefault);
    void circle(Vec2 center, f32 radius, Vec4 color, i32 segments = 24,
                f32 thickness = 1.5f, Category cat = kDefault);
    void text(Vec2 topLeft, std::string_view str, Vec4 color, Category cat = kDefault);

    // A ground grid covering `bounds`, lines snapped to multiples of `spacing`. Pass the
    // camera's visibleBounds() and it is infinite in every way that matters: wherever the
    // camera goes, the grid was already there, and only the visible lines are ever drawn.
    // Lines on multiples of `majorEvery` * spacing draw in `majorColor` — counting cells
    // is what a grid is FOR, and uniform lines make it wallpaper.
    void grid(Rect bounds, f32 spacing, Vec4 color, Vec4 majorColor, u32 majorEvery = 10,
              f32 thickness = 1.0f, Category cat = kDefault);

    void flush(renderer::SpriteBatch& batch, const text::Font* font = nullptr, i32 layer = 5000);

    [[nodiscard]] u32 primitiveCount() const {
        return static_cast<u32>(m_quads.size() + m_texts.size());
    }

private:
    struct Quad { Vec2 center; Vec2 size; f32 rotation; Vec4 color; Category cat; };
    struct Text { Vec2 topLeft; std::string str; Vec4 color; Category cat; };
    struct Cat  { std::string name; CVar* enabledVar; };

    rhi::IGraphicsDevice& m_device;
    rhi::TextureHandle    m_white{};
    std::vector<Cat>      m_categories;
    std::vector<Quad>     m_quads;
    std::vector<Text>     m_texts;
};

}
