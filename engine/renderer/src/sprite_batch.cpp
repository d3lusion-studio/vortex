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

rhi::SamplerDesc samplerDescFor(SpriteSampler s) {
    const bool nearest = s == SpriteSampler::NearestClamp || s == SpriteSampler::NearestRepeat;
    const bool repeat  = s == SpriteSampler::LinearRepeat || s == SpriteSampler::NearestRepeat;
    const rhi::Filter      filter  = nearest ? rhi::Filter::Nearest : rhi::Filter::Linear;
    const rhi::AddressMode address = repeat ? rhi::AddressMode::Repeat
                                            : rhi::AddressMode::ClampToEdge;
    return {.minFilter = filter, .magFilter = filter, .addressU = address, .addressV = address};
}

}

SpriteBatch::SpriteBatch(rhi::IGraphicsDevice& device, rhi::Format colorFormat, u32 maxSprites,
                         rhi::Format depthFormat, rhi::BlendMode blend)
    : m_device(device), m_maxSprites(maxSprites) {

    // Four samplers, made up front. They are tiny, immutable objects and every one
    // of them is a plausible ask, so there is nothing to gain from creating them lazily.
    for (u32 i = 0; i < kSpriteSamplerCount; ++i)
        m_samplers[i] = m_device.createSampler(samplerDescFor(static_cast<SpriteSampler>(i)));

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = toBytes(sprite_vert_spv, sprite_vert_spv_size);
    pd.fragmentSpirv       = toBytes(sprite_frag_spv, sprite_frag_spv_size);
    pd.vertexWgsl          = sprite_vert_spv_wgsl;
    pd.fragmentWgsl        = sprite_frag_spv_wgsl;
    pd.vertexLayout.stride = sizeof(Instance);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float4, .offset = offsetof(Instance, axes)},
        {.location = 1, .format = rhi::VertexFormat::Float2, .offset = offsetof(Instance, translate)},
        {.location = 2, .format = rhi::VertexFormat::Float4, .offset = offsetof(Instance, uv)},
        {.location = 3, .format = rhi::VertexFormat::Float4, .offset = offsetof(Instance, color)},
    };
    pd.vertexLayout.perInstance = true;
    // Four corners as a strip, so the quad needs no index buffer at all. Instances
    // do not connect to one another, so one strip per sprite is exactly right.
    pd.topology           = rhi::PrimitiveTopology::TriangleStrip;
    pd.cull               = rhi::CullMode::None;
    pd.colorFormat        = colorFormat;
    pd.blendMode          = blend;
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

    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        m_instanceBuffers[i] = m_device.createBuffer(
            {.size = static_cast<u64>(maxSprites) * sizeof(Instance),
             .usage = rhi::BufferUsage::Vertex, .domain = rhi::MemoryDomain::Upload,
             .debugName = "sprite_instances"});
    }

    m_instances.reserve(maxSprites);
}

SpriteBatch::~SpriteBatch() {
    m_device.waitIdle();
    for (auto& [key, groups] : m_bindGroupCache)
        for (rhi::BindGroupHandle bg : groups)
            if (bg.valid()) m_device.destroyBindGroup(bg);
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) m_device.destroyBuffer(m_instanceBuffers[i]);
    m_device.destroyPipeline(m_pipeline);
    for (rhi::SamplerHandle s : m_samplers) m_device.destroySampler(s);
}

void SpriteBatch::begin(const Mat4& viewProjection) {
    m_viewProjection = viewProjection;
    m_items.clear();
    m_drawCalls = 0;
}

void SpriteBatch::draw(const Sprite& sprite) {
    // Sizing and the anchor shift fold into one matrix, applied inside the rotation
    // so the sprite turns about its anchor rather than about its centre.
    const Vec2 offset = sprite.anchorOffset();
    Mat4 local;
    local.at(0, 0) = sprite.size.x;
    local.at(1, 1) = sprite.size.y;
    local.at(0, 3) = offset.x;
    local.at(1, 3) = offset.y;

    const Mat4 transform = Mat4::translation(sprite.position.x, sprite.position.y, 0.0f) *
                           Mat4::rotationZ(sprite.rotation) * local;
    m_items.push_back({.transform = transform,
                       .color     = sprite.color,
                       .uv        = flippedUV(sprite.uv, sprite.flipX, sprite.flipY),
                       .texture   = sprite.texture,
                       .layer     = sprite.layer,
                       .sampler   = sprite.sampler});
}

void SpriteBatch::drawNineSlice(const Sprite& sprite, const NineSlice& slice) {
    if (!slice.valid()) { draw(sprite); return; }

    // Column and row extents, in destination units. The corners want their source size,
    // so the insets carry over 1:1 and the middle takes whatever is left.
    f32 l = slice.left, r = slice.right, t = slice.top, b = slice.bottom;

    // A patch stretched smaller than its own borders would give the middle a negative
    // size and fold the corners through each other. Shrink the borders to fit instead —
    // the corners lose detail, but the patch stays a patch.
    if (const f32 sum = l + r; sum > sprite.size.x && sum > 0.0f) {
        const f32 k = sprite.size.x / sum;
        l *= k; r *= k;
    }
    if (const f32 sum = t + b; sum > sprite.size.y && sum > 0.0f) {
        const f32 k = sprite.size.y / sum;
        t *= k; b *= k;
    }

    const f32 midW = sprite.size.x - l - r;
    const f32 midH = sprite.size.y - t - b;

    // The same three-way split, once in destination units and once in uv.
    const f32 colW[3] = {l, midW, r};
    const f32 rowH[3] = {t, midH, b};

    const f32 uScale = sprite.uv.width  / slice.sourcePixels.x;
    const f32 vScale = sprite.uv.height / slice.sourcePixels.y;
    const f32 colU[3] = {slice.left * uScale,
                         sprite.uv.width - (slice.left + slice.right) * uScale,
                         slice.right * uScale};
    const f32 rowV[3] = {slice.top * vScale,
                         sprite.uv.height - (slice.top + slice.bottom) * vScale,
                         slice.bottom * vScale};

    // The sprite's own frame: position, rotation, and the shift that puts its anchor on
    // the position. Cells are then laid out inside it, in the quad's local space.
    const Vec2 offset = sprite.anchorOffset();
    const Mat4 frame  = Mat4::translation(sprite.position.x, sprite.position.y, 0.0f) *
                        Mat4::rotationZ(sprite.rotation) *
                        Mat4::translation(offset.x, offset.y, 0.0f);

    // Walk from the top-left corner of the quad. Local +y is up, so rows advance downwards.
    f32 y = sprite.size.y * 0.5f;
    f32 v = sprite.uv.y;
    for (int row = 0; row < 3; ++row) {
        f32 x = -sprite.size.x * 0.5f;
        f32 u = sprite.uv.x;
        for (int col = 0; col < 3; ++col) {
            if (colW[col] > 0.0f && rowH[row] > 0.0f) {
                Mat4 local;
                local.at(0, 0) = colW[col];
                local.at(1, 1) = rowH[row];
                local.at(0, 3) = x + colW[col] * 0.5f;
                local.at(1, 3) = y - rowH[row] * 0.5f;

                m_items.push_back({.transform = frame * local,
                                   .color   = sprite.color,
                                   .uv      = {u, v, colU[col], rowV[row]},
                                   .texture = sprite.texture,
                                   .layer   = sprite.layer,
                                   .sampler = sprite.sampler});
            }
            x += colW[col];
            u += colU[col];
        }
        y -= rowH[row];
        v += rowV[row];
    }
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

rhi::BindGroupHandle SpriteBatch::bindGroupFor(rhi::TextureHandle texture, SpriteSampler sampler) {
    SamplerBindGroups& groups = m_bindGroupCache[textureKey(texture)];
    rhi::BindGroupHandle& bg  = groups[static_cast<usize>(sampler)];
    if (!bg.valid())
        bg = m_device.createBindGroup({.texture = texture,
                                       .sampler = m_samplers[static_cast<usize>(sampler)]});
    return bg;
}

void SpriteBatch::releaseTexture(rhi::TextureHandle texture) {
    auto it = m_bindGroupCache.find(textureKey(texture));
    if (it == m_bindGroupCache.end()) return;
    for (rhi::BindGroupHandle bg : it->second)
        if (bg.valid()) m_device.destroyBindGroup(bg);
    m_bindGroupCache.erase(it);
}

void SpriteBatch::end(rhi::ICommandList& cmd) {
    if (m_items.empty()) return;

    {
        VORTEX_PROFILE_ZONE("batch.sort");
        // Layer decides what covers what, so it leads. Texture and sampler only
        // decide how few draw calls the layer costs, and grouping on both is what
        // lets a run share one bind group.
        const auto order = [](const RenderItem& a, const RenderItem& b) {
            if (a.layer != b.layer) return a.layer < b.layer;
            const u64 ka = textureKey(a.texture), kb = textureKey(b.texture);
            if (ka != kb) return ka < kb;
            return a.sampler < b.sampler;
        };
        if (!std::is_sorted(m_items.begin(), m_items.end(), order))
            std::stable_sort(m_items.begin(), m_items.end(), order);
    }

    const u32 spriteCount = std::min(static_cast<u32>(m_items.size()), m_maxSprites);

    {
        // Copying the transform's two basis columns is the whole of the per-sprite CPU
        // work now — the corners are the vertex shader's problem.
        VORTEX_PROFILE_ZONE("batch.instances");
        m_instances.resize(spriteCount);
        for (u32 i = 0; i < spriteCount; ++i) {
            const RenderItem& it = m_items[i];
            m_instances[i] = {
                .axes      = {it.transform.at(0, 0), it.transform.at(1, 0),
                              it.transform.at(0, 1), it.transform.at(1, 1)},
                .translate = {it.transform.at(0, 3), it.transform.at(1, 3)},
                .uv        = {it.uv.x, it.uv.y, it.uv.width, it.uv.height},
                .color     = it.color,
            };
        }
    }

    rhi::BufferHandle vbo = m_instanceBuffers[m_frame];
    {
        VORTEX_PROFILE_ZONE("batch.upload");
        m_device.updateBuffer(vbo, m_instances.data(), m_instances.size() * sizeof(Instance));
    }

    cmd.setPipeline(m_pipeline);
    cmd.pushConstants(&m_viewProjection, sizeof(Mat4));
    cmd.setVertexBuffer(0, vbo);

    u32 runStart = 0;
    while (runStart < spriteCount) {
        const rhi::TextureHandle tex = m_items[runStart].texture;
        const SpriteSampler      smp = m_items[runStart].sampler;
        u32 runEnd = runStart + 1;
        while (runEnd < spriteCount &&
               m_items[runEnd].texture == tex && m_items[runEnd].sampler == smp) ++runEnd;

        // Four corners, one strip, N instances — the run's sprites in a single call.
        cmd.setBindGroup(0, bindGroupFor(tex, smp));
        cmd.draw(4, runEnd - runStart, 0, runStart);
        ++m_drawCalls;
        runStart = runEnd;
    }

    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
}

}
