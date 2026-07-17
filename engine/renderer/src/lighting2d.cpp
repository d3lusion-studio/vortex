#include "vortex/renderer/lighting2d.hpp"

#include "vortex/core/log.hpp"
#include "vortex/renderer/sprite_batch.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <cmath>
#include <vector>

namespace vortex::renderer {

namespace {

// The falloff, baked once into a texture instead of evaluated in a shader.
//
// One radial gradient serves every light: a light is that gradient stretched to its radius
// and tinted its colour, which is why adding a light costs one quad and no state change.
// The shape is smoothstep on the squared distance — a soft core that does not read as a
// hard disc, and a tail that reaches zero exactly at the edge so a light never ends on a
// visible circle.
constexpr u32 kGradientSize = 128;

[[nodiscard]] rhi::TextureHandle makeGradient(rhi::IGraphicsDevice& device) {
    std::vector<u8> px(static_cast<usize>(kGradientSize) * kGradientSize * 4);

    const f32 centre = static_cast<f32>(kGradientSize - 1) * 0.5f;
    for (u32 y = 0; y < kGradientSize; ++y) {
        for (u32 x = 0; x < kGradientSize; ++x) {
            const f32 dx = (static_cast<f32>(x) - centre) / centre;
            const f32 dy = (static_cast<f32>(y) - centre) / centre;
            const f32 d  = std::sqrt(dx * dx + dy * dy);

            const f32 t = d >= 1.0f ? 0.0f : (1.0f - d);
            const f32 v = t * t * (3.0f - 2.0f * t);   // smoothstep
            const auto b = static_cast<u8>(v * 255.0f + 0.5f);

            u8* p = &px[(static_cast<usize>(y) * kGradientSize + x) * 4];
            // White, with the falloff in every channel: additive blending ignores alpha,
            // so the RGB IS the intensity. Putting it only in alpha would add nothing.
            p[0] = p[1] = p[2] = b;
            p[3] = b;
        }
    }

    return device.createTexture({.width = kGradientSize, .height = kGradientSize,
                                 .debugName = "light2d_gradient"},
                                px.data());
}

}   // namespace

struct Lighting2D::Impl {
    rhi::IGraphicsDevice& device;
    rhi::Format           targetFormat;

    rhi::TextureHandle gradient;
    rhi::TextureHandle buffer;          // the light buffer
    rhi::TextureHandle white;           // for the ambient fill and the composite quad
    u32                bufferWidth  = 0;
    u32                bufferHeight = 0;
    f32                scale        = 0.5f;

    // Additive: lights add to the ambient fill and to each other.
    std::unique_ptr<SpriteBatch> lightBatch;
    // Multiply: the buffer darkens the world it is drawn over.
    std::unique_ptr<SpriteBatch> compositeBatch;

    std::vector<Light2D> lights;
    u32                  maxLights = 0;

    explicit Impl(rhi::IGraphicsDevice& d, rhi::Format fmt) : device(d), targetFormat(fmt) {}
};

Lighting2D::Lighting2D(rhi::IGraphicsDevice& device, rhi::Format targetFormat, u32 maxLights)
    : m_impl(std::make_unique<Impl>(device, targetFormat)) {
    Impl& s = *m_impl;
    s.maxLights = maxLights;
    s.gradient  = makeGradient(device);

    const u8 whitePx[4] = {255, 255, 255, 255};
    s.white = device.createTexture({.width = 1, .height = 1, .debugName = "light2d_white"},
                                   whitePx);

    // The light buffer is RGBA8: light is a colour in [0, 1] here, because the composite
    // multiplies and multiplying by more than 1 is what HDR bloom is for, not this.
    constexpr rhi::Format kBufferFormat = rhi::Format::R8G8B8A8_UNORM;
    s.lightBatch = std::make_unique<SpriteBatch>(device, kBufferFormat, maxLights + 1,
                                                 rhi::Format::Undefined,
                                                 rhi::BlendMode::Additive);
    s.compositeBatch = std::make_unique<SpriteBatch>(device, targetFormat, 4,
                                                     rhi::Format::Undefined,
                                                     rhi::BlendMode::Multiply);
}

Lighting2D::~Lighting2D() {
    Impl& s = *m_impl;
    if (s.lightBatch) {
        s.lightBatch->releaseTexture(s.gradient);
        s.lightBatch->releaseTexture(s.white);
    }
    if (s.compositeBatch && s.buffer.valid()) s.compositeBatch->releaseTexture(s.buffer);

    if (s.buffer.valid())   s.device.destroyTexture(s.buffer);
    if (s.gradient.valid()) s.device.destroyTexture(s.gradient);
    if (s.white.valid())    s.device.destroyTexture(s.white);
}

void Lighting2D::begin() { m_impl->lights.clear(); }

void Lighting2D::add(const Light2D& light) {
    Impl& s = *m_impl;
    if (s.lights.size() >= s.maxLights) return;   // silently capped, like the sprite batcher
    s.lights.push_back(light);
}

usize Lighting2D::count() const { return m_impl->lights.size(); }

void Lighting2D::setResolutionScale(f32 scale) {
    m_impl->scale = scale > 0.0f ? scale : 1.0f;
}

void Lighting2D::ensureBuffer(u32 width, u32 height) {
    Impl& s = *m_impl;
    if (s.buffer.valid() && s.bufferWidth == width && s.bufferHeight == height) return;

    if (s.buffer.valid()) {
        // The batcher caches bind groups by texture handle, and handles get recycled. A
        // stale descriptor pointing at a destroyed image is a GPU crash that shows up
        // somewhere else entirely, so drop it before the texture goes.
        s.compositeBatch->releaseTexture(s.buffer);
        s.device.destroyTexture(s.buffer);
    }

    s.buffer = s.device.createTexture(
        {.width  = width,
         .height = height,
         .format = rhi::Format::R8G8B8A8_UNORM,
         .usage  = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::Sampled,
         .debugName = "light2d_buffer"});
    s.bufferWidth  = width;
    s.bufferHeight = height;
}

bool Lighting2D::isNoop() const {
    const Impl& s = *m_impl;
    // Ambient white with nothing shining is a multiply by 1 over every pixel. Rendering it
    // is a buffer, a clear and a full-screen pass to change nothing.
    return s.lights.empty() && ambient.r >= 1.0f && ambient.g >= 1.0f && ambient.b >= 1.0f;
}

void Lighting2D::buildBuffer(rhi::ICommandList& cmd, const Mat4& viewProjection, u32 targetWidth,
                             u32 targetHeight) {
    Impl& s = *m_impl;
    if (targetWidth == 0 || targetHeight == 0) return;
    if (isNoop()) return;

    const auto w = static_cast<u32>(static_cast<f32>(targetWidth) * s.scale);
    const auto h = static_cast<u32>(static_cast<f32>(targetHeight) * s.scale);
    ensureBuffer(w > 0 ? w : 1, h > 0 ? h : 1);

    // Clearing to the ambient colour IS the darkness: everything the lights do not reach
    // keeps this value, and the composite multiplies the world by it.
    rhi::RenderPassDesc pass;
    pass.color.target = s.buffer;
    pass.color.loadOp = rhi::LoadOp::Clear;
    pass.color.setClear(ambient);
    pass.width  = s.bufferWidth;
    pass.height = s.bufferHeight;

    cmd.beginRenderPass(pass);
    cmd.setViewport({.x = 0.0f, .y = 0.0f,
                     .width  = static_cast<f32>(s.bufferWidth),
                     .height = static_cast<f32>(s.bufferHeight)});
    cmd.setScissor(0, 0, s.bufferWidth, s.bufferHeight);

    // The lights are drawn in WORLD space with the world's matrix — the buffer covers
    // exactly the same view, just at fewer pixels, so the two line up by construction and
    // nothing has to be projected by hand.
    s.lightBatch->begin(viewProjection);
    for (const Light2D& light : s.lights) {
        const Color tint{light.color.r * light.intensity, light.color.g * light.intensity,
                         light.color.b * light.intensity, 1.0f};
        s.lightBatch->draw({.position = light.position,
                            .size     = {light.radius * 2.0f, light.radius * 2.0f},
                            .color    = tint,
                            .texture  = s.gradient,
                            .sampler  = SpriteSampler::LinearClamp});
    }
    s.lightBatch->end(cmd);
    cmd.endRenderPass();

    // The buffer was just a render target; composite() is about to SAMPLE it. endRenderPass
    // does not move it — it only ends the rendering — so without this the shader reads an
    // image still in the colour-attachment layout. That is undefined, and the kind of
    // undefined that works on the GPU you wrote it on: this ran correctly for a whole
    // session on Intel with the validation layers unavailable to object.
    //
    // The RenderGraph does this for its own resources; Lighting2D owns this one, so it does
    // it itself.
    cmd.transition(s.buffer, rhi::ResourceState::ShaderRead);
}

void Lighting2D::composite(rhi::ICommandList& cmd, u32 targetWidth, u32 targetHeight) {
    Impl& s = *m_impl;
    if (!s.buffer.valid() || targetWidth == 0 || targetHeight == 0) return;
    // Skipped for the same reason buildBuffer skipped: this frame's buffer was never
    // filled, so compositing it would multiply the world by whatever was in it last.
    if (isNoop()) return;

    // A full-screen quad in its own pixel space, so this needs no camera: the buffer is
    // the view, stretched back up to the target.
    const f32 w = static_cast<f32>(targetWidth);
    const f32 h = static_cast<f32>(targetHeight);
    const Mat4 screen = Mat4::ortho(-w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, -1.0f, 1.0f);

    s.compositeBatch->begin(screen);
    s.compositeBatch->draw({.position = {0.0f, 0.0f},
                            .size     = {w, h},
                            .texture  = s.buffer,
                            // Linear, and this is the point of the low-res buffer: the
                            // upscale is what turns four times less fill into a softer
                            // falloff rather than a blockier one.
                            .sampler  = SpriteSampler::LinearClamp});
    s.compositeBatch->end(cmd);
}

}
