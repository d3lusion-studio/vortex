#pragma once
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <memory>

namespace vortex::pf { class IWindow; }

namespace vortex::rhi {

class ISwapchain;
class ICommandList;

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;

    [[nodiscard]] virtual BufferHandle createBuffer(const BufferDesc&,
                                                    const void* initialData = nullptr) = 0;
    virtual void destroyBuffer(BufferHandle) = 0;

    virtual void updateBuffer(BufferHandle, const void* data, u64 size, u64 offset = 0) = 0;

    [[nodiscard]] virtual TextureHandle createTexture(const TextureDesc&,
                                                      const void* pixels = nullptr) = 0;
    virtual void destroyTexture(TextureHandle) = 0;

    // Overwrite a rectangle of a 2D colour texture. `pixels` is tightly packed —
    // width * bytesPerPixel(format) per row, no padding. The texture must carry
    // TextureUsage::CopyDst (implicit when it was created with initial pixels).
    //
    // The write lands before any subsequent draw reads the texture, so a caller
    // that rewrites pixels every frame does not have to synchronise by hand. It is
    // a full pipeline stall on Vulkan, though: this is for CPU-authored images and
    // atlas growth, not for streaming a video.
    virtual void updateTexture(TextureHandle, const void* pixels,
                               u32 x, u32 y, u32 width, u32 height) = 0;

    [[nodiscard]] virtual SamplerHandle createSampler(const SamplerDesc&) = 0;
    virtual void destroySampler(SamplerHandle) = 0;

    [[nodiscard]] virtual BindGroupHandle createBindGroup(const BindGroupDesc&) = 0;
    virtual void destroyBindGroup(BindGroupHandle) = 0;

    [[nodiscard]] virtual PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) = 0;
    virtual void destroyPipeline(PipelineHandle) = 0;

    [[nodiscard]] virtual std::unique_ptr<ISwapchain> createSwapchain(const SwapchainDesc&,
                                                                      pf::IWindow&) = 0;

    [[nodiscard]] virtual FrameContext beginFrame(ISwapchain&) = 0;
    virtual void endFrame() = 0;

    virtual void waitIdle() = 0;

    [[nodiscard]] virtual ICommandList* acquireSecondaryCommandList() = 0;

    virtual void executeSecondary(ICommandList& primary,
                                  ICommandList* const* lists, u32 count) = 0;

    [[nodiscard]] virtual f64 gpuFrameTimeMs() const = 0;
};

[[nodiscard]] std::unique_ptr<IGraphicsDevice> createDevice(GraphicsAPI api, pf::IWindow& window);

[[nodiscard]] GraphicsAPI defaultGraphicsAPI();
[[nodiscard]] std::unique_ptr<IGraphicsDevice> createDevice(pf::IWindow& window);

}
