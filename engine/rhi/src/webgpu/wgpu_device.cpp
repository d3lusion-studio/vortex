#include "wgpu_device.hpp"
#include "wgpu_command_list.hpp"

#include "vortex/core/assert.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstring>
#include <vector>

namespace vortex::rhi::wgpu {

namespace {

void logCallback(WGPULogLevel level, const char* msg, void*) {
    switch (level) {
        case WGPULogLevel_Error: VORTEX_ERROR("wgpu", "%s", msg); break;
        case WGPULogLevel_Warn:  VORTEX_WARN("wgpu", "%s", msg);  break;
        default:                 VORTEX_INFO("wgpu", "%s", msg);  break;
    }
}

void errorCallback(WGPUErrorType type, const char* msg, void*) {
    VORTEX_ERROR("wgpu", "uncaptured error (type %d): %s", static_cast<int>(type), msg);
}

struct AdapterResult { WGPUAdapter adapter = nullptr; bool done = false; };
void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* msg, void* ud) {
    auto* r = static_cast<AdapterResult*>(ud);
    if (status == WGPURequestAdapterStatus_Success) r->adapter = adapter;
    else VORTEX_ERROR("RHI", "wgpu requestAdapter failed: %s", msg ? msg : "?");
    r->done = true;
}

struct DeviceResult { WGPUDevice device = nullptr; bool done = false; };
void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, const char* msg, void* ud) {
    auto* r = static_cast<DeviceResult*>(ud);
    if (status == WGPURequestDeviceStatus_Success) r->device = device;
    else VORTEX_ERROR("RHI", "wgpu requestDevice failed: %s", msg ? msg : "?");
    r->done = true;
}

constexpr u32 kMaxPushConstantSize = 128;

}

class WebGPUSwapchain final : public ISwapchain {
public:
    WebGPUSwapchain(WebGPUDevice& device, WGPUSurface surface, const SwapchainDesc& desc,
                    WGPUTextureFormat format)
        : m_device(device), m_surface(surface), m_format(format),
          m_width(desc.width), m_height(desc.height),
          m_present(toWGPUPresentMode(desc.present)) {
        configure();
    }

    [[nodiscard]] Format format() const override { return fromWGPUFormat(m_format); }
    void getExtent(u32& w, u32& h) const override { w = m_width; h = m_height; }

    void requestResize(u32 w, u32 h) override {
        if (w == 0 || h == 0) return;
        if (w == m_width && h == m_height) return;
        m_width = w; m_height = h;
        m_needsConfigure = true;
    }

    void configure() {
        WGPUSurfaceConfiguration cfg{};
        cfg.device      = m_device.device();
        cfg.format      = m_format;
        cfg.usage       = WGPUTextureUsage_RenderAttachment;
        cfg.alphaMode   = WGPUCompositeAlphaMode_Auto;
        cfg.width       = m_width;
        cfg.height      = m_height;
        cfg.presentMode = m_present;
        wgpuSurfaceConfigure(m_surface, &cfg);
        m_needsConfigure = false;
    }

    [[nodiscard]] bool needsConfigure() const { return m_needsConfigure; }
    void markNeedsConfigure() { m_needsConfigure = true; }
    [[nodiscard]] WGPUSurface surface() const { return m_surface; }
    [[nodiscard]] WGPUTextureFormat wgpuFormat() const { return m_format; }
    [[nodiscard]] u32 width() const { return m_width; }
    [[nodiscard]] u32 height() const { return m_height; }

private:
    WebGPUDevice&     m_device;
    WGPUSurface       m_surface;
    WGPUTextureFormat m_format;
    u32               m_width;
    u32               m_height;
    WGPUPresentMode   m_present;
    bool              m_needsConfigure = false;
};

// ===========================================================================
// Device construction
// ===========================================================================

WebGPUDevice::WebGPUDevice(pf::IWindow& window) {
    wgpuSetLogLevel(WGPULogLevel_Warn);
    wgpuSetLogCallback(logCallback, nullptr);

    WGPUInstanceExtras extras{};
    extras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    extras.backends    = WGPUInstanceBackend_Primary;   // Vulkan on Linux
    WGPUInstanceDescriptor instDesc{};
    instDesc.nextInChain = &extras.chain;
    m_instance = wgpuCreateInstance(&instDesc);
    if (!m_instance) { VORTEX_ERROR("RHI", "wgpuCreateInstance failed"); return; }

    m_surface = createWGPUSurface(m_instance, window.nativeHandleKind(),
                                  window.nativeDisplayHandle(), window.nativeWindowHandle());
    if (!m_surface) return;

    AdapterResult ar{};
    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = m_surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(m_instance, &opts, onAdapter, &ar);
    VORTEX_ASSERT(ar.done, "wgpu requestAdapter is expected to be synchronous in wgpu-native");
    m_adapter = ar.adapter;
    if (!m_adapter) return;

    // Require the push-constant native feature + a generous push-constant size,
    // since the engine's sprite/post pipelines use push constants.
    WGPUNativeLimits nativeLimits{};
    nativeLimits.maxPushConstantSize = kMaxPushConstantSize;
    WGPURequiredLimitsExtras limitsExtras{};
    limitsExtras.chain.sType = static_cast<WGPUSType>(WGPUSType_RequiredLimitsExtras);
    limitsExtras.limits      = nativeLimits;

    // Require exactly what the adapter supports. Zero-initialised alignment
    // limits would be rejected ("0 is better than allowed"), since alignment
    // limits are inverted — requesting the adapter's values is always valid.
    WGPUSupportedLimits supported{};
    wgpuAdapterGetLimits(m_adapter, &supported);
    WGPURequiredLimits requiredLimits{};
    requiredLimits.nextInChain = &limitsExtras.chain;
    requiredLimits.limits      = supported.limits;

    const WGPUFeatureName features[] = {
        static_cast<WGPUFeatureName>(WGPUNativeFeature_PushConstants)};

    DeviceResult dr{};
    WGPUDeviceDescriptor devDesc{};
    devDesc.requiredFeatureCount = 1;
    devDesc.requiredFeatures     = features;
    devDesc.requiredLimits       = &requiredLimits;
    wgpuAdapterRequestDevice(m_adapter, &devDesc, onDevice, &dr);
    VORTEX_ASSERT(dr.done, "wgpu requestDevice is expected to be synchronous in wgpu-native");
    m_device = dr.device;
    if (!m_device) return;

    wgpuDeviceSetUncapturedErrorCallback(m_device, errorCallback, nullptr);
    m_queue = wgpuDeviceGetQueue(m_device);

    // Shared material bind-group layout: texture (0) + sampler (1), fragment stage.
    WGPUBindGroupLayoutEntry entries[2]{};
    entries[0].binding               = 0;
    entries[0].visibility            = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType    = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].binding               = 1;
    entries[1].visibility            = WGPUShaderStage_Fragment;
    entries[1].sampler.type          = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 2;
    bglDesc.entries    = entries;
    m_materialBGL = wgpuDeviceCreateBindGroupLayout(m_device, &bglDesc);

    // Uniform bind-group layout: one uniform buffer (0), vertex+fragment stages.
    WGPUBindGroupLayoutEntry uboEntry{};
    uboEntry.binding      = 0;
    uboEntry.visibility   = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    uboEntry.buffer.type  = WGPUBufferBindingType_Uniform;
    WGPUBindGroupLayoutDescriptor uboBglDesc{};
    uboBglDesc.entryCount = 1;
    uboBglDesc.entries    = &uboEntry;
    m_uniformBGL = wgpuDeviceCreateBindGroupLayout(m_device, &uboBglDesc);

    VORTEX_INFO("RHI", "WebGPU device initialised (wgpu-native)");
}

WebGPUDevice::~WebGPUDevice() {
    if (m_device) wgpuDevicePoll(m_device, true, nullptr);

    m_pipelines.forEachAlive([](WebGPUPipeline& p) {
        if (p.pipeline) wgpuRenderPipelineRelease(p.pipeline);
        if (p.layout)   wgpuPipelineLayoutRelease(p.layout);
    });
    m_bindGroups.forEachAlive([](WebGPUBindGroup& g) {
        if (g.group) wgpuBindGroupRelease(g.group);
    });
    m_samplers.forEachAlive([](WebGPUSampler& s) {
        if (s.sampler) wgpuSamplerRelease(s.sampler);
    });
    m_textures.forEachAlive([](WebGPUTexture& t) {
        if (t.view) wgpuTextureViewRelease(t.view);
        if (t.texture && t.ownsTexture) wgpuTextureDestroy(t.texture);
        if (t.texture && t.ownsTexture) wgpuTextureRelease(t.texture);
    });
    m_buffers.forEachAlive([](WebGPUBuffer& b) {
        if (b.buffer) { wgpuBufferDestroy(b.buffer); wgpuBufferRelease(b.buffer); }
    });

    if (m_materialBGL) wgpuBindGroupLayoutRelease(m_materialBGL);
    if (m_uniformBGL)  wgpuBindGroupLayoutRelease(m_uniformBGL);
    if (m_queue)    wgpuQueueRelease(m_queue);
    if (m_device)   wgpuDeviceRelease(m_device);
    if (m_adapter)  wgpuAdapterRelease(m_adapter);
    if (m_surface)  wgpuSurfaceRelease(m_surface);
    if (m_instance) wgpuInstanceRelease(m_instance);
}

// ===========================================================================
// Buffers
// ===========================================================================

BufferHandle WebGPUDevice::createBuffer(const BufferDesc& desc, const void* initialData) {
    WGPUBufferUsageFlags usage = WGPUBufferUsage_CopyDst;   // allow updateBuffer()
    if (hasFlag(desc.usage, BufferUsage::Vertex))  usage |= WGPUBufferUsage_Vertex;
    if (hasFlag(desc.usage, BufferUsage::Index))   usage |= WGPUBufferUsage_Index;
    if (hasFlag(desc.usage, BufferUsage::Uniform)) usage |= WGPUBufferUsage_Uniform;
    if (hasFlag(desc.usage, BufferUsage::Storage)) usage |= WGPUBufferUsage_Storage;
    if (hasFlag(desc.usage, BufferUsage::Staging)) usage |= WGPUBufferUsage_CopySrc;

    const u64 size = (desc.size + 3) & ~u64{3};   // WebGPU requires 4-byte sizes

    WGPUBufferDescriptor bd{};
    bd.label            = desc.debugName;
    bd.usage            = usage;
    bd.size             = size;
    bd.mappedAtCreation = initialData != nullptr;

    WebGPUBuffer buffer{};
    buffer.size   = size;
    buffer.buffer = wgpuDeviceCreateBuffer(m_device, &bd);

    if (initialData) {
        void* mapped = wgpuBufferGetMappedRange(buffer.buffer, 0, size);
        std::memcpy(mapped, initialData, desc.size);
        wgpuBufferUnmap(buffer.buffer);
    }
    return m_buffers.create(buffer);
}

void WebGPUDevice::destroyBuffer(BufferHandle h) {
    if (WebGPUBuffer* b = m_buffers.get(h)) {
        if (b->buffer) { wgpuBufferDestroy(b->buffer); wgpuBufferRelease(b->buffer); }
        m_buffers.destroy(h);
    }
}

void WebGPUDevice::updateBuffer(BufferHandle h, const void* data, u64 size, u64 offset) {
    WebGPUBuffer* b = m_buffers.get(h);
    if (!b) return;
    VORTEX_ASSERT(offset + size <= b->size, "updateBuffer write exceeds buffer size");
    const u64 writeSize = (size + 3) & ~u64{3};
    wgpuQueueWriteBuffer(m_queue, b->buffer, offset, data, writeSize);
}

// ===========================================================================
// Textures
// ===========================================================================

TextureHandle WebGPUDevice::createTexture(const TextureDesc& desc, const void* pixels) {
    const bool depth = isDepthFormat(desc.format) || hasFlag(desc.usage, TextureUsage::DepthStencil);

    WGPUTextureUsageFlags usage = 0;
    if (depth) {
        usage = WGPUTextureUsage_RenderAttachment;
        if (hasFlag(desc.usage, TextureUsage::Sampled)) usage |= WGPUTextureUsage_TextureBinding;
    } else {
        usage = WGPUTextureUsage_TextureBinding;
        if (pixels) usage |= WGPUTextureUsage_CopyDst;
        if (hasFlag(desc.usage, TextureUsage::RenderTarget))
            usage |= WGPUTextureUsage_RenderAttachment;
    }

    WebGPUTexture tex{};
    tex.format  = toWGPUFormat(desc.format);
    tex.width   = desc.width;
    tex.height  = desc.height;
    tex.isDepth = depth;

    WGPUTextureDescriptor td{};
    td.label         = desc.debugName;
    td.usage         = usage;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {desc.width, desc.height, 1};
    td.format        = tex.format;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    tex.texture      = wgpuDeviceCreateTexture(m_device, &td);

    if (pixels) {
        WGPUImageCopyTexture dst{};
        dst.texture = tex.texture;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTextureDataLayout layout{};
        layout.bytesPerRow  = desc.width * 4;
        layout.rowsPerImage = desc.height;
        WGPUExtent3D ext{desc.width, desc.height, 1};
        wgpuQueueWriteTexture(m_queue, &dst, pixels,
                              static_cast<usize>(desc.width) * desc.height * 4, &layout, &ext);
    }

    tex.view = wgpuTextureCreateView(tex.texture, nullptr);
    return m_textures.create(tex);
}

void WebGPUDevice::destroyTexture(TextureHandle h) {
    if (WebGPUTexture* t = m_textures.get(h)) {
        if (t->view) wgpuTextureViewRelease(t->view);
        if (t->texture && t->ownsTexture) { wgpuTextureDestroy(t->texture); wgpuTextureRelease(t->texture); }
        m_textures.destroy(h);
    }
}

TextureHandle WebGPUDevice::registerTexture(const WebGPUTexture& tex) { return m_textures.create(tex); }
void WebGPUDevice::unregisterTexture(TextureHandle h) { m_textures.destroy(h); }

// ===========================================================================
// Samplers
// ===========================================================================

SamplerHandle WebGPUDevice::createSampler(const SamplerDesc& desc) {
    WGPUSamplerDescriptor sd{};
    sd.addressModeU = toWGPUAddress(desc.addressU);
    sd.addressModeV = toWGPUAddress(desc.addressV);
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter    = toWGPUFilter(desc.magFilter);
    sd.minFilter    = toWGPUFilter(desc.minFilter);
    sd.mipmapFilter = toWGPUMipFilter(desc.minFilter);
    sd.lodMinClamp  = 0.0f;
    sd.lodMaxClamp  = 32.0f;
    sd.maxAnisotropy = 1;

    WebGPUSampler s{};
    s.sampler = wgpuDeviceCreateSampler(m_device, &sd);
    return m_samplers.create(s);
}

void WebGPUDevice::destroySampler(SamplerHandle h) {
    if (WebGPUSampler* s = m_samplers.get(h)) {
        if (s->sampler) wgpuSamplerRelease(s->sampler);
        m_samplers.destroy(h);
    }
}

// ===========================================================================
// Bind groups
// ===========================================================================

BindGroupHandle WebGPUDevice::createBindGroup(const BindGroupDesc& desc) {
    // Uniform-buffer bind group: single UBO at binding 0.
    if (desc.uniformBuffer.valid()) {
        WebGPUBuffer* buf = m_buffers.get(desc.uniformBuffer);
        VORTEX_ASSERT(buf, "createBindGroup with invalid uniform buffer handle");
        if (!buf) return {};

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer  = buf->buffer;
        entry.offset  = 0;
        entry.size    = desc.uniformSize > 0 ? desc.uniformSize : buf->size;

        WGPUBindGroupDescriptor bgd{};
        bgd.layout     = m_uniformBGL;
        bgd.entryCount = 1;
        bgd.entries    = &entry;

        WebGPUBindGroup group{};
        group.group = wgpuDeviceCreateBindGroup(m_device, &bgd);
        return m_bindGroups.create(group);
    }

    WebGPUTexture* tex = m_textures.get(desc.texture);
    WebGPUSampler* smp = m_samplers.get(desc.sampler);
    VORTEX_ASSERT(tex && smp, "createBindGroup with invalid texture or sampler handle");
    if (!tex || !smp) return {};

    WGPUBindGroupEntry entries[2]{};
    entries[0].binding     = 0;
    entries[0].textureView = tex->view;
    entries[1].binding     = 1;
    entries[1].sampler     = smp->sampler;

    WGPUBindGroupDescriptor bgd{};
    bgd.layout     = m_materialBGL;
    bgd.entryCount = 2;
    bgd.entries    = entries;

    WebGPUBindGroup group{};
    group.group = wgpuDeviceCreateBindGroup(m_device, &bgd);
    return m_bindGroups.create(group);
}

void WebGPUDevice::destroyBindGroup(BindGroupHandle h) {
    if (WebGPUBindGroup* g = m_bindGroups.get(h)) {
        if (g->group) wgpuBindGroupRelease(g->group);
        m_bindGroups.destroy(h);
    }
}

// ===========================================================================
// Pipelines
// ===========================================================================

PipelineHandle WebGPUDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    auto makeModule = [&](const std::vector<std::byte>& spirv) -> WGPUShaderModule {
        WGPUShaderModuleSPIRVDescriptor sp{};
        sp.chain.sType = WGPUSType_ShaderModuleSPIRVDescriptor;
        sp.codeSize    = static_cast<u32>(spirv.size() / 4);
        sp.code        = reinterpret_cast<const u32*>(spirv.data());
        WGPUShaderModuleDescriptor smd{};
        smd.nextInChain = &sp.chain;
        return wgpuDeviceCreateShaderModule(m_device, &smd);
    };
    WGPUShaderModule vs = makeModule(desc.vertexSpirv);
    WGPUShaderModule fs = makeModule(desc.fragmentSpirv);

    // Pipeline layout: optional material bind group + optional push constants.
    WGPUPushConstantRange pcRange{};
    pcRange.stages = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    pcRange.start  = 0;
    pcRange.end    = desc.pushConstantSize;
    WGPUPipelineLayoutExtras layoutExtras{};
    layoutExtras.chain.sType            = static_cast<WGPUSType>(WGPUSType_PipelineLayoutExtras);
    layoutExtras.pushConstantRangeCount = 1;
    layoutExtras.pushConstantRanges     = &pcRange;

    // Bind group layouts in order: material set (if any) then uniform set (if any).
    WGPUBindGroupLayout bgls[2];
    u32 bglCount = 0;
    if (desc.hasMaterialTexture) bgls[bglCount++] = m_materialBGL;
    if (desc.hasUniformBuffer)   bgls[bglCount++] = m_uniformBGL;
    WGPUPipelineLayoutDescriptor pld{};
    if (desc.pushConstantSize > 0) pld.nextInChain = &layoutExtras.chain;
    if (bglCount > 0) {
        pld.bindGroupLayoutCount = bglCount;
        pld.bindGroupLayouts     = bgls;
    }
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(m_device, &pld);

    // Vertex layout.
    std::vector<WGPUVertexAttribute> attrs;
    attrs.reserve(desc.vertexLayout.attributes.size());
    for (const VertexAttribute& a : desc.vertexLayout.attributes)
        attrs.push_back({toWGPUVertexFormat(a.format), a.offset, a.location});

    WGPUVertexBufferLayout vbl{};
    vbl.arrayStride    = desc.vertexLayout.stride;
    vbl.stepMode       = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = attrs.size();
    vbl.attributes     = attrs.data();

    const bool hasVertices = desc.vertexLayout.stride > 0;

    WGPURenderPipelineDescriptor pd{};
    pd.label          = desc.debugName;
    pd.layout         = layout;
    pd.vertex.module      = vs;
    pd.vertex.entryPoint  = "main";
    pd.vertex.bufferCount = hasVertices ? 1u : 0u;
    pd.vertex.buffers     = hasVertices ? &vbl : nullptr;

    pd.primitive.topology  = toWGPUTopology(desc.topology);
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode  = toWGPUCull(desc.cull);

    pd.multisample.count = desc.sampleCount > 0 ? desc.sampleCount : 1;
    pd.multisample.mask  = 0xFFFFFFFFu;

    // Depth-stencil.
    WGPUDepthStencilState ds{};
    if (desc.depthFormat != Format::Undefined) {
        ds.format            = toWGPUFormat(desc.depthFormat);
        ds.depthWriteEnabled = desc.depthWrite;
        ds.depthCompare      = desc.depthTest ? toWGPUCompare(desc.depthCompare)
                                              : WGPUCompareFunction_Always;
        ds.stencilFront = {WGPUCompareFunction_Always, WGPUStencilOperation_Keep,
                           WGPUStencilOperation_Keep, WGPUStencilOperation_Keep};
        ds.stencilBack  = ds.stencilFront;
        pd.depthStencil = &ds;
    }

    // Fragment + colour target.
    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};

    WGPUColorTargetState colorTarget{};
    colorTarget.format    = toWGPUFormat(desc.colorFormat);
    colorTarget.writeMask = WGPUColorWriteMask_All;
    colorTarget.blend     = desc.alphaBlend ? &blend : nullptr;

    WGPUFragmentState fragment{};
    fragment.module      = fs;
    fragment.entryPoint  = "main";
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;
    pd.fragment = &fragment;

    WebGPUPipeline pipeline{};
    pipeline.layout           = layout;
    pipeline.pushConstantSize = desc.pushConstantSize;
    pipeline.pipeline         = wgpuDeviceCreateRenderPipeline(m_device, &pd);

    wgpuShaderModuleRelease(vs);
    wgpuShaderModuleRelease(fs);
    return m_pipelines.create(pipeline);
}

void WebGPUDevice::destroyPipeline(PipelineHandle h) {
    if (WebGPUPipeline* p = m_pipelines.get(h)) {
        if (p->pipeline) wgpuRenderPipelineRelease(p->pipeline);
        if (p->layout)   wgpuPipelineLayoutRelease(p->layout);
        m_pipelines.destroy(h);
    }
}

// ===========================================================================
// Swapchain + frame loop
// ===========================================================================

std::unique_ptr<ISwapchain> WebGPUDevice::createSwapchain(const SwapchainDesc& desc, pf::IWindow&) {
    // Pick a surface format: prefer BGRA8Unorm (matches the Vulkan backend's
    // typical swapchain format) for image parity, else the surface's preferred.
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps);
    WGPUTextureFormat chosen = caps.formatCount > 0 ? caps.formats[0]
                                                    : WGPUTextureFormat_BGRA8Unorm;
    for (usize i = 0; i < caps.formatCount; ++i)
        if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm) { chosen = WGPUTextureFormat_BGRA8Unorm; break; }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    return std::make_unique<WebGPUSwapchain>(*this, m_surface, desc, chosen);
}

FrameContext WebGPUDevice::beginFrame(ISwapchain& scBase) {
    auto& sc = static_cast<WebGPUSwapchain&>(scBase);
    FrameContext fc{};

    if (sc.needsConfigure()) sc.configure();

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(sc.surface(), &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        if (st.texture) wgpuTextureRelease(st.texture);
        sc.markNeedsConfigure();
        return fc;
    }

    WebGPUTexture backbuffer{};
    backbuffer.texture     = st.texture;
    backbuffer.view        = wgpuTextureCreateView(st.texture, nullptr);
    backbuffer.format      = sc.wgpuFormat();
    backbuffer.width       = sc.width();
    backbuffer.height      = sc.height();
    backbuffer.ownsTexture = false;   // owned by the surface
    m_frameSurfaceTex = st.texture;
    m_frameBackbuffer = registerTexture(backbuffer);

    m_frameEncoder = wgpuDeviceCreateCommandEncoder(m_device, nullptr);
    m_primary.bindPrimary(this);
    m_frameSwapchain = &sc;
    m_frameActive    = true;
    m_secondaryNext.store(0, std::memory_order_relaxed);

    fc.cmd        = &m_primary;
    fc.backbuffer = m_frameBackbuffer;
    fc.width      = sc.width();
    fc.height     = sc.height();
    fc.valid      = true;
    return fc;
}

void WebGPUDevice::endFrame() {
    if (!m_frameActive) return;

    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(m_frameEncoder, nullptr);
    wgpuQueueSubmit(m_queue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(m_frameEncoder);
    m_frameEncoder = nullptr;

    wgpuSurfacePresent(m_frameSwapchain->surface());

    // Release the transient backbuffer view + surface texture for this frame.
    if (WebGPUTexture* t = m_textures.get(m_frameBackbuffer)) {
        if (t->view) wgpuTextureViewRelease(t->view);
    }
    unregisterTexture(m_frameBackbuffer);
    if (m_frameSurfaceTex) { wgpuTextureRelease(m_frameSurfaceTex); m_frameSurfaceTex = nullptr; }

    wgpuDevicePoll(m_device, false, nullptr);
    m_frameActive = false;
}

void WebGPUDevice::waitIdle() {
    if (m_device) wgpuDevicePoll(m_device, true, nullptr);
}

// ===========================================================================
// Secondary command lists (deferred record + replay)
// ===========================================================================

ICommandList* WebGPUDevice::acquireSecondaryCommandList() {
    const u32 idx = m_secondaryNext.fetch_add(1, std::memory_order_relaxed);
    VORTEX_ASSERT(idx < kMaxSecondaries, "exceeded kMaxSecondaries secondary command lists");
    if (idx >= kMaxSecondaries) return nullptr;

    if (!m_secondaries[idx]) m_secondaries[idx] = std::make_unique<WebGPUCommandList>();
    m_secondaries[idx]->bindSecondary(this);
    return m_secondaries[idx].get();
}

void WebGPUDevice::executeSecondary(ICommandList& primary, ICommandList* const* lists, u32 count) {
    auto* prim = static_cast<WebGPUCommandList*>(&primary);
    WGPURenderPassEncoder enc = prim->encoder();
    if (!enc) return;
    for (u32 i = 0; i < count; ++i)
        static_cast<WebGPUCommandList*>(lists[i])->replayOnto(enc);
}

// ===========================================================================
// Factory
// ===========================================================================

std::unique_ptr<IGraphicsDevice> createWebGPUDevice(pf::IWindow& window) {
    auto dev = std::make_unique<WebGPUDevice>(window);
    if (!dev->valid()) {
        VORTEX_ERROR("RHI", "WebGPU device creation failed");
        return nullptr;
    }
    return dev;
}

}  // namespace vortex::rhi::wgpu

namespace vortex::rhi {
std::unique_ptr<IGraphicsDevice> createWebGPUDevice(pf::IWindow& window) {
    return wgpu::createWebGPUDevice(window);
}
}
