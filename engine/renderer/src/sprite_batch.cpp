#include "vortex/renderer/sprite_batch.hpp"

#include "vortex/core/profiler.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "sprite_vert_spv.h"
#include "sprite_frag_spv.h"

#include <algorithm>
#include <cstring>

namespace vortex::renderer {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

u64 textureKey(rhi::TextureHandle h) {
    return (static_cast<u64>(h.generation) << 32) | h.index;
}

}

SpriteBatch::SpriteBatch(rhi::IGraphicsDevice& device, rhi::Format colorFormat, u32 maxSprites,
                         rhi::Format depthFormat)
    : m_device(device), m_maxSprites(maxSprites) {

    m_sampler = m_device.createSampler({.minFilter = rhi::Filter::Linear,
                                        .magFilter = rhi::Filter::Linear,
                                        .addressU  = rhi::AddressMode::ClampToEdge,
                                        .addressV  = rhi::AddressMode::ClampToEdge});

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = toBytes(sprite_vert_spv, sprite_vert_spv_size);
    pd.fragmentSpirv       = toBytes(sprite_frag_spv, sprite_frag_spv_size);
    pd.vertexLayout.stride = sizeof(Vertex);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex, pos)},
        {.location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex, uv)},
        {.location = 2, .format = rhi::VertexFormat::Float4, .offset = offsetof(Vertex, color)},
        {.location = 3, .format = rhi::VertexFormat::UInt1,  .offset = offsetof(Vertex, texIndex)},
    };
    pd.topology           = rhi::PrimitiveTopology::TriangleList;
    pd.cull               = rhi::CullMode::None;
    pd.colorFormat        = colorFormat;
    pd.alphaBlend         = true;
    pd.hasMaterialTexture = true;
    pd.pushConstantSize   = sizeof(Mat4);
    if (depthFormat != rhi::Format::Undefined) {
        pd.depthTest    = true;
        pd.depthWrite   = true;
        pd.depthCompare = rhi::CompareOp::LessEqual;
        pd.depthFormat  = depthFormat;
    }
    pd.debugName          = "sprite_pipeline";
    m_pipeline = m_device.createGraphicsPipeline(pd);

    std::vector<u32> indices(static_cast<usize>(maxSprites) * 6);
    for (u32 q = 0; q < maxSprites; ++q) {
        const u32 v = q * 4;
        u32* idx = &indices[static_cast<usize>(q) * 6];
        idx[0] = v + 0; idx[1] = v + 1; idx[2] = v + 2;
        idx[3] = v + 2; idx[4] = v + 3; idx[5] = v + 0;
    }
    m_indexBuffer = m_device.createBuffer(
        {.size = indices.size() * sizeof(u32), .usage = rhi::BufferUsage::Index,
         .domain = rhi::MemoryDomain::Device, .debugName = "sprite_indices"},
        indices.data());

    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_vertexBuffers[i] = m_device.createBuffer(
            {.size = static_cast<u64>(maxSprites) * 4 * sizeof(Vertex),
             .usage = rhi::BufferUsage::Vertex, .domain = rhi::MemoryDomain::Upload,
             .debugName = "sprite_vertices"});
    }

    m_vertices.reserve(static_cast<usize>(maxSprites) * 4);
}

SpriteBatch::~SpriteBatch() {
    m_device.waitIdle();
    for (auto& [key, bg] : m_bindGroupCache) m_device.destroyBindGroup(bg);
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) m_device.destroyBuffer(m_vertexBuffers[i]);
    m_device.destroyBuffer(m_indexBuffer);
    m_device.destroyPipeline(m_pipeline);
    m_device.destroySampler(m_sampler);
}

void SpriteBatch::begin(const Mat4& viewProjection) {
    m_viewProjection = viewProjection;
    m_items.clear();
    m_drawCalls = 0;
}

void SpriteBatch::draw(const Sprite& sprite) {
    const Mat4 transform = Mat4::translation(sprite.position.x, sprite.position.y, 0.0f) *
                           Mat4::rotationZ(sprite.rotation) *
                           Mat4::scaling(sprite.size.x, sprite.size.y, 1.0f);
    m_items.push_back({.transform = transform, .color = sprite.color, .uv = sprite.uv,
                       .texture = sprite.texture, .layer = sprite.layer});
}

void SpriteBatch::drawSprite(rhi::TextureHandle texture, Vec2 position, Vec2 size,
                             Vec4 color, Rect uv, i32 layer) {
    const Mat4 transform = Mat4::translation(position.x, position.y, 0.0f) *
                           Mat4::scaling(size.x, size.y, 1.0f);
    m_items.push_back({.transform = transform, .color = color, .uv = uv,
                       .texture = texture, .layer = layer});
}

void SpriteBatch::submit(const RenderItem& item) {
    m_items.push_back(item);
}

void SpriteBatch::submit(const RenderItem* items, usize count) {
    m_items.insert(m_items.end(), items, items + count);
}

rhi::BindGroupHandle SpriteBatch::bindGroupFor(rhi::TextureHandle texture) {
    const u64 key = textureKey(texture);
    auto it = m_bindGroupCache.find(key);
    if (it != m_bindGroupCache.end()) return it->second;
    rhi::BindGroupHandle bg = m_device.createBindGroup({.texture = texture, .sampler = m_sampler});
    m_bindGroupCache.emplace(key, bg);
    return bg;
}

void SpriteBatch::end(rhi::ICommandList& cmd) {
    if (m_items.empty()) return;

    {
        VORTEX_PROFILE_ZONE("batch.sort");
        const auto order = [](const RenderItem& a, const RenderItem& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            return textureKey(a.texture) < textureKey(b.texture);
        };
        if (!std::is_sorted(m_items.begin(), m_items.end(), order))
            std::stable_sort(m_items.begin(), m_items.end(), order);
    }

    const u32 spriteCount = std::min(static_cast<u32>(m_items.size()), m_maxSprites);

    static constexpr Vec2 kCorners[4] = {
        {-0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f}};

    {
        VORTEX_PROFILE_ZONE("batch.vertices");
        m_vertices.resize(static_cast<usize>(spriteCount) * 4);
        for (u32 i = 0; i < spriteCount; ++i) {
            const RenderItem& it = m_items[i];
            const f32 uMin = it.uv.x, vMin = it.uv.y;
            const f32 uMax = it.uv.x + it.uv.width, vMax = it.uv.y + it.uv.height;
            const Vec2 uvs[4] = {{uMin, vMin}, {uMax, vMin}, {uMax, vMax}, {uMin, vMax}};
            Vertex* v = &m_vertices[static_cast<usize>(i) * 4];
            for (int c = 0; c < 4; ++c) {
                const Vec4 p = it.transform * Vec4{kCorners[c].x, kCorners[c].y, 0.0f, 1.0f};
                v[c] = {{p.x, p.y}, uvs[c], it.color};
            }
        }
    }

    rhi::BufferHandle vbo = m_vertexBuffers[m_frame];
    {
        VORTEX_PROFILE_ZONE("batch.upload");
        m_device.updateBuffer(vbo, m_vertices.data(), m_vertices.size() * sizeof(Vertex));
    }

    cmd.setPipeline(m_pipeline);
    cmd.pushConstants(&m_viewProjection, sizeof(Mat4));
    cmd.setVertexBuffer(0, vbo);
    cmd.setIndexBuffer(m_indexBuffer, rhi::IndexType::U32);

    u32 runStart = 0;
    while (runStart < spriteCount) {
        const rhi::TextureHandle tex = m_items[runStart].texture;
        u32 runEnd = runStart + 1;
        while (runEnd < spriteCount && m_items[runEnd].texture == tex) ++runEnd;

        cmd.setBindGroup(0, bindGroupFor(tex));
        cmd.drawIndexed((runEnd - runStart) * 6, 1, runStart * 6, 0, 0);
        ++m_drawCalls;
        runStart = runEnd;
    }

    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
}

}
