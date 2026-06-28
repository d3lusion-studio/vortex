#include "vortex/debug/debug_draw.hpp"

#include "vortex/core/console.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"
#include "vortex/text/font.hpp"
#include "vortex/text/text_renderer.hpp"

#include <cmath>

namespace vortex::debug {

DebugDraw::DebugDraw(rhi::IGraphicsDevice& device) : m_device(device) {
    const u8 px[4] = {255, 255, 255, 255};
    m_white = m_device.createTexture({.width = 1, .height = 1, .debugName = "debug_white"}, px);
    category("default");
}

DebugDraw::~DebugDraw() {
    if (m_white.valid()) m_device.destroyTexture(m_white);
}

DebugDraw::Category DebugDraw::category(const char* name) {
    const std::string cvarName = "debug." + std::string(name);
    for (u32 i = 0; i < m_categories.size(); ++i)
        if (m_categories[i].name == cvarName) return i;

    m_categories.push_back({cvarName, nullptr});
    Cat& cat = m_categories.back();
    cat.enabledVar = Console::global().registerBool(
        cat.name.c_str(), true, "toggle a debug-draw category");
    return static_cast<Category>(m_categories.size() - 1);
}

void DebugDraw::setEnabled(Category cat, bool enabled) {
    if (cat < m_categories.size()) m_categories[cat].enabledVar->set(enabled);
}

bool DebugDraw::enabled(Category cat) const {
    return cat < m_categories.size() && m_categories[cat].enabledVar->asBool();
}

void DebugDraw::begin() {
    m_quads.clear();
    m_texts.clear();
}

void DebugDraw::line(Vec2 a, Vec2 b, Vec4 color, f32 thickness, Category cat) {
    const Vec2 d   = b - a;
    const f32  len = length(d);
    if (len <= 1e-6f) return;
    const Vec2 center{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    const f32  angle = std::atan2(d.y, d.x);
    m_quads.push_back({center, {len, thickness}, angle, color, cat});
}

void DebugDraw::box(Vec2 center, Vec2 size, Vec4 color, f32 thickness, Category cat) {
    const f32 hx = size.x * 0.5f, hy = size.y * 0.5f;
    const Vec2 tl{center.x - hx, center.y + hy};
    const Vec2 tr{center.x + hx, center.y + hy};
    const Vec2 br{center.x + hx, center.y - hy};
    const Vec2 bl{center.x - hx, center.y - hy};
    line(tl, tr, color, thickness, cat);
    line(tr, br, color, thickness, cat);
    line(br, bl, color, thickness, cat);
    line(bl, tl, color, thickness, cat);
}

void DebugDraw::filledBox(Vec2 center, Vec2 size, Vec4 color, Category cat) {
    m_quads.push_back({center, size, 0.0f, color, cat});
}

void DebugDraw::circle(Vec2 center, f32 radius, Vec4 color, i32 segments,
                       f32 thickness, Category cat) {
    if (segments < 3) segments = 3;
    const f32 step = 6.2831853f / static_cast<f32>(segments);
    Vec2 prev{center.x + radius, center.y};
    for (i32 i = 1; i <= segments; ++i) {
        const f32 a = step * static_cast<f32>(i);
        const Vec2 cur{center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
        line(prev, cur, color, thickness, cat);
        prev = cur;
    }
}

void DebugDraw::text(Vec2 topLeft, std::string_view str, Vec4 color, Category cat) {
    m_texts.push_back({topLeft, std::string(str), color, cat});
}

void DebugDraw::flush(renderer::SpriteBatch& batch, const text::Font* font, i32 layer) {
    for (const Quad& q : m_quads) {
        if (!enabled(q.cat)) continue;
        renderer::Sprite s;
        s.position = q.center;
        s.size     = q.size;
        s.rotation = q.rotation;
        s.color    = q.color;
        s.texture  = m_white;
        s.layer    = layer;
        batch.draw(s);
    }
    if (font) {
        for (const Text& t : m_texts) {
            if (!enabled(t.cat)) continue;
            text::drawText(batch, *font, t.str, t.topLeft, t.color, 1.0f, layer + 1);
        }
    }
}

}
