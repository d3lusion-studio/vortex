#pragma once
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <memory>
#include <string_view>

namespace vortex::rhi { class IGraphicsDevice; }
namespace vortex::pf  { class IFileSystem; }

namespace vortex::text {

class Font {
public:
    struct Glyph {
        Rect uv;
        Vec2 size{0.0f, 0.0f};
        Vec2 offset{0.0f, 0.0f};
        f32  advance = 0.0f;
    };

    [[nodiscard]] static std::unique_ptr<Font> loadFromFile(
        rhi::IGraphicsDevice& device, pf::IFileSystem& fs, const char* path, f32 pixelHeight);

    ~Font();
    Font(const Font&)            = delete;
    Font& operator=(const Font&) = delete;

    [[nodiscard]] const Glyph* glyph(char c) const;
    [[nodiscard]] rhi::TextureHandle atlas() const { return m_atlas; }
    [[nodiscard]] f32 pixelHeight() const { return m_pixelHeight; }
    [[nodiscard]] f32 ascent()      const { return m_ascent; }
    [[nodiscard]] f32 lineHeight()  const { return m_lineHeight; }

    [[nodiscard]] Vec2 measure(std::string_view text) const;

private:
    Font(rhi::IGraphicsDevice& device) : m_device(device) {}

    static constexpr u32 kFirstChar = 32;
    static constexpr u32 kCharCount = 95;

    rhi::IGraphicsDevice& m_device;
    rhi::TextureHandle    m_atlas;
    Glyph                 m_glyphs[kCharCount]{};
    f32                   m_pixelHeight = 0.0f;
    f32                   m_ascent      = 0.0f;
    f32                   m_lineHeight  = 0.0f;
};

}
