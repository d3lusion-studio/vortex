#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <unordered_map>
#include <vector>

namespace vortex::rhi {
class IGraphicsDevice;
class ICommandList;
}

namespace vortex::renderer {

struct Sprite {
    Vec2               position{0.0f, 0.0f};   // world-space centre
    Vec2               size{1.0f, 1.0f};       // world-space size
    f32                rotation = 0.0f;          // radians
    Vec4               color{1.0f, 1.0f, 1.0f, 1.0f};
    Rect               uv = kFullUV;
    rhi::TextureHandle texture;
    i32                layer = 0;
};

class SpriteBatch {
public:
    SpriteBatch(rhi::IGraphicsDevice& device, rhi::Format colorFormat, u32 maxSprites = 100000);
    ~SpriteBatch();

    SpriteBatch(const SpriteBatch&)            = delete;
    SpriteBatch& operator=(const SpriteBatch&) = delete;

    void begin(const Mat4& viewProjection);
    void draw(const Sprite&);
    void drawSprite(rhi::TextureHandle texture, Vec2 position, Vec2 size,
                    Vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}, Rect uv = kFullUV, i32 layer = 0);
    // Submit an already-transformed item, e.g. produced by ECS render extraction.
    void submit(const RenderItem&);
    void submit(const RenderItem* items, usize count);
    void end(rhi::ICommandList& cmd);

    [[nodiscard]] u32 drawCallCount() const { return m_drawCalls; }
    [[nodiscard]] u32 spriteCount()   const { return static_cast<u32>(m_items.size()); }

private:
    struct Vertex {
        Vec2 pos;
        Vec2 uv;
        Vec4 color;
    };

    rhi::BindGroupHandle bindGroupFor(rhi::TextureHandle);

    rhi::IGraphicsDevice& m_device;
    u32                   m_maxSprites;

    rhi::PipelineHandle m_pipeline;
    rhi::SamplerHandle  m_sampler;
    rhi::BufferHandle   m_indexBuffer;
    rhi::BufferHandle   m_vertexBuffers[rhi::kMaxFramesInFlight];   // one per frame in flight
    u32                 m_frame = 0;

    std::unordered_map<u64, rhi::BindGroupHandle> m_bindGroupCache;

    Mat4                    m_viewProjection;
    std::vector<RenderItem> m_items;
    std::vector<Vertex>     m_vertices;
    u32                     m_drawCalls = 0;
};

}
