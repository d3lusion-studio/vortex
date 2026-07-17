#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <array>
#include <unordered_map>
#include <vector>

namespace vortex::rhi {
class IGraphicsDevice;
class ICommandList;
}

namespace vortex::renderer {

struct Sprite {
    Vec2               position{0.0f, 0.0f};   // world-space anchor point
    Vec2               size{1.0f, 1.0f};       // world-space size
    f32                rotation = 0.0f;          // radians, about the anchor
    Vec4               color{1.0f, 1.0f, 1.0f, 1.0f};
    Rect               uv = kFullUV;
    rhi::TextureHandle texture;
    i32                layer = 0;
    SpriteSampler      sampler = SpriteSampler::LinearClamp;
    bool               flipX = false;
    bool               flipY = false;

    // Which point of the quad sits on `position`, in unit coordinates: (0,0) is the
    // bottom-left corner, (1,1) the top-right. Mirrors ecs::SpriteComp::anchor.
    Vec2 anchor{0.5f, 0.5f};

    [[nodiscard]] Vec2 anchorOffset() const noexcept {
        return {(0.5f - anchor.x) * size.x, (0.5f - anchor.y) * size.y};
    }
};

// Reverse the uv span on the requested axes. Sampling then walks the window
// backwards, which is a flip. See the note on RenderItem.
[[nodiscard]] constexpr Rect flippedUV(Rect uv, bool flipX, bool flipY) noexcept {
    if (flipX) { uv.x += uv.width;  uv.width  = -uv.width; }
    if (flipY) { uv.y += uv.height; uv.height = -uv.height; }
    return uv;
}

// 9-patch: how to stretch a sprite without stretching its border. The image is cut into
// nine cells by the four insets; the corners are drawn at their source size, the edges
// stretch along one axis and the middle stretches along both. This is what a resizable
// panel, button or dialogue box is made of — scaling such a sprite normally would smear
// its rounded corners.
struct NineSlice {
    // Insets from the edges of the source window, in SOURCE PIXELS.
    f32 left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;

    // Pixel size of the sprite's uv window — what the insets are measured against. For a
    // whole texture this is the texture's size; for an atlas region, TextureRegion::sizePixels.
    Vec2 sourcePixels{0.0f, 0.0f};

    [[nodiscard]] bool valid() const noexcept {
        return sourcePixels.x > 0.0f && sourcePixels.y > 0.0f;
    }
};

class SpriteBatch {
public:
    // `blend` is fixed at construction because it is baked into the pipeline: a batcher
    // draws one way. A scene that needs additive glow on top of alpha sprites wants two
    // batchers, drawn in order — which is also what keeps the sort inside each of them
    // meaningful, since additive draws do not care about order and alpha ones do.
    SpriteBatch(rhi::IGraphicsDevice& device, rhi::Format colorFormat, u32 maxSprites = 100000,
                rhi::Format depthFormat = rhi::Format::Undefined,
                rhi::BlendMode blend = rhi::BlendMode::Alpha);
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch&)            = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    void begin(const Mat4& viewProjection);
    void draw(const Sprite&);
    void drawSprite(rhi::TextureHandle texture, Vec2 position, Vec2 size,
                    Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, Rect uv = kFullUV, i32 layer = 0);
    // Draw `sprite` as a 9-patch: up to nine quads instead of one, sharing the sprite's
    // texture, colour, layer and sampler. `sprite.size` is the size the whole patch is
    // stretched to. Cells that would be empty are skipped, so a slice with no bottom
    // inset costs six quads, not nine. flipX/flipY are ignored — mirroring a 9-patch is
    // not a thing a 9-patch means.
    void drawNineSlice(const Sprite& sprite, const NineSlice&);

    // Submit an already-transformed item, e.g. produced by ECS render extraction.
    void submit(const RenderItem&);
    void submit(const RenderItem* items, usize count);
    void end(rhi::ICommandList& cmd);

    // Drop the bind groups cached for a texture. Call this before destroying the
    // texture: the cache is keyed by handle, and a handle whose slot is later
    // recycled would otherwise leave descriptors pointing at a dead image behind.
    void releaseTexture(rhi::TextureHandle);

    [[nodiscard]] u32 drawCallCount() const { return m_drawCalls; }
    [[nodiscard]] u32 spriteCount()   const { return static_cast<u32>(m_items.size()); }

private:
    // One record per sprite, matching sprite.vert's instance inputs. The 2D affine
    // transform is unrolled to its two basis columns plus the translation: a Mat4
    // would cost 64 bytes to say the same 24.
    struct Instance {
        Vec4 axes;        // m00, m10, m01, m11
        Vec2 translate;   // m03, m13
        Vec4 uv;          // x, y, width, height
        Vec4 color;
    };

    // One bind group per (texture, sampler) pair, created on first use. Grouping
    // the four samplers under one key keeps the lookup to a single hash of the
    // texture handle, which is what the inner run loop does per draw call.
    using SamplerBindGroups = std::array<rhi::BindGroupHandle, kSpriteSamplerCount>;

    rhi::BindGroupHandle bindGroupFor(rhi::TextureHandle, SpriteSampler);

    rhi::IGraphicsDevice& m_device;
    u32                   m_maxSprites;

    rhi::PipelineHandle m_pipeline;
    std::array<rhi::SamplerHandle, kSpriteSamplerCount> m_samplers;
    rhi::BufferHandle   m_instanceBuffers[rhi::kMaxFramesInFlight];   // one per frame in flight
    u32                 m_frame = 0;

    std::unordered_map<u64, SamplerBindGroups> m_bindGroupCache;

    Mat4                    m_viewProjection;
    std::vector<RenderItem> m_items;
    std::vector<Instance>   m_instances;
    u32                     m_drawCalls = 0;
};

}
