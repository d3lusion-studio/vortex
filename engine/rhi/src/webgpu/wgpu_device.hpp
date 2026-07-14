#pragma once
#include "wgpu_command_list.hpp"
#include "wgpu_common.hpp"
#include "wgpu_resources.hpp"

#include "vortex/rhi/device.hpp"

#include <atomic>
#include <memory>

namespace vortex::pf { class IWindow; enum class NativeHandleKind; }

namespace vortex::rhi::wgpu {

class WebGPUSwapchain;

inline constexpr u32 kMaxSecondaries = 64;

WGPUSurface createWGPUSurface(WGPUInstance, pf::NativeHandleKind, void* display, void* window);

class WebGPUDevice final : public IGraphicsDevice {
public:
    explicit WebGPUDevice(pf::IWindow& window);
    ~WebGPUDevice() override;

    WebGPUDevice(const WebGPUDevice&)            = delete;
    WebGPUDevice& operator=(const WebGPUDevice&) = delete;

    [[nodiscard]] bool valid() const { return m_device != nullptr; }

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc&, const void* initialData) override;
    void destroyBuffer(BufferHandle) override;
    void updateBuffer(BufferHandle, const void* data, u64 size, u64 offset) override;

    [[nodiscard]] TextureHandle createTexture(const TextureDesc&, const void* pixels) override;
    void destroyTexture(TextureHandle) override;
    void updateTexture(TextureHandle, const void* pixels,
                       u32 x, u32 y, u32 width, u32 height) override;
    // Readback needs an async buffer map, which has no place to block in the browser's
    // frame loop. Unimplemented until something in the WebGPU path actually needs it.
    void readTexture(TextureHandle, void*) override {}

    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc&) override;
    void destroySampler(SamplerHandle) override;

    [[nodiscard]] BindGroupHandle createBindGroup(const BindGroupDesc&) override;
    void destroyBindGroup(BindGroupHandle) override;

    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&) override;
    void destroyPipeline(PipelineHandle) override;

    [[nodiscard]] std::unique_ptr<ISwapchain> createSwapchain(const SwapchainDesc&, pf::IWindow&) override;

    [[nodiscard]] FrameContext beginFrame(ISwapchain&) override;
    void endFrame() override;

    void waitIdle() override;

    [[nodiscard]] ICommandList* acquireSecondaryCommandList() override;
    void executeSecondary(ICommandList& primary, ICommandList* const* lists, u32 count) override;

    [[nodiscard]] f64 gpuFrameTimeMs() const override { return 0.0; }

    // Internal accessors used by the command list / swapchain.
    [[nodiscard]] WGPUDevice  device()  const { return m_device; }
    [[nodiscard]] WGPUQueue   queue()   const { return m_queue; }
    [[nodiscard]] WGPUInstance instance() const { return m_instance; }
    [[nodiscard]] WGPUAdapter adapter() const { return m_adapter; }
    [[nodiscard]] WGPUCommandEncoder frameEncoder() const { return m_frameEncoder; }

    [[nodiscard]] WebGPUBuffer*    getBuffer(BufferHandle h)       { return m_buffers.get(h); }
    [[nodiscard]] WebGPUTexture*   getTexture(TextureHandle h)     { return m_textures.get(h); }
    [[nodiscard]] WebGPUSampler*   getSampler(SamplerHandle h)     { return m_samplers.get(h); }
    [[nodiscard]] WebGPUBindGroup* getBindGroup(BindGroupHandle h) { return m_bindGroups.get(h); }
    [[nodiscard]] WebGPUPipeline*  getPipeline(PipelineHandle h)   { return m_pipelines.get(h); }

    [[nodiscard]] TextureHandle registerTexture(const WebGPUTexture&) ;
    void unregisterTexture(TextureHandle);

    /// Push-constant emulation. Copies `size` bytes into this frame's uniform ring buffer and
    /// returns the dynamic offset the command list must bind at kPushConstantGroup. Returns
    /// UINT32_MAX if the ring is exhausted (the draw is then skipped rather than reading garbage).
    [[nodiscard]] u32 writePushConstants(const void* data, u32 size);
    [[nodiscard]] WGPUBindGroup       pushBindGroup() const { return m_pushBindGroup; }
    [[nodiscard]] WGPUBindGroupLayout pushBGL()       const { return m_pushBGL; }
    [[nodiscard]] WGPUBindGroupLayout emptyBGL()      const { return m_emptyBGL; }

private:
    void createPushConstantRing();
    WGPUInstance m_instance = nullptr;
    WGPUSurface  m_surface  = nullptr;
    WGPUAdapter  m_adapter  = nullptr;
    WGPUDevice   m_device   = nullptr;
    WGPUQueue    m_queue    = nullptr;

    WGPUBindGroupLayout m_materialBGL = nullptr;   // binding 0: texture, binding 1: sampler
    WGPUBindGroupLayout m_uniformBGL  = nullptr;   // binding 0: uniform buffer (vtx+frag)

    // Push-constant emulation, bound at kPushConstantGroup with a dynamic offset.
    WGPUBindGroupLayout m_pushBGL       = nullptr;  // one dynamic-offset uniform buffer
    WGPUBindGroup       m_pushBindGroup = nullptr;
    WGPUBuffer          m_pushRing      = nullptr;
    u64                 m_pushRingSize  = 0;
    u64                 m_pushRingHead  = 0;        // reset every frame
    u32                 m_pushStride    = 256;      // minUniformBufferOffsetAlignment

    // An empty layout used to fill the gaps: a pipeline layout is indexed by @group, so a shader
    // that uses group 0 and group 3 still needs *something* at 1 and 2.
    WGPUBindGroupLayout m_emptyBGL = nullptr;

    Pool<WebGPUBuffer,    BufferTag>    m_buffers;
    Pool<WebGPUTexture,   TextureTag>   m_textures;
    Pool<WebGPUSampler,   SamplerTag>   m_samplers;
    Pool<WebGPUBindGroup, BindGroupTag> m_bindGroups;
    Pool<WebGPUPipeline,  PipelineTag>  m_pipelines;

    WebGPUCommandList m_primary;

    // Per-frame transient state.
    WebGPUSwapchain*  m_frameSwapchain = nullptr;
    WGPUCommandEncoder m_frameEncoder  = nullptr;
    WGPUTexture       m_frameSurfaceTex = nullptr;
    TextureHandle     m_frameBackbuffer{};
    bool              m_frameActive    = false;

    // Secondary (deferred) command lists, recycled each frame.
    std::unique_ptr<WebGPUCommandList> m_secondaries[kMaxSecondaries];
    std::atomic<u32>                   m_secondaryNext{0};
};

}
