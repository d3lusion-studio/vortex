#include "wgpu_device.hpp"
#include "wgpu_command_list.hpp"

#include "vortex/core/assert.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstring>
#include <vector>

namespace vortex::rhi::wgpu {

namespace {

void errorCallback(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
    const std::string_view text = fromStrView(msg);
    VORTEX_ERROR("wgpu", "uncaptured error (type %d): %.*s", static_cast<int>(type),
                 static_cast<int>(text.size()), text.data());
}

struct AdapterResult { WGPUAdapter adapter = nullptr; bool done = false; };
void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg,
               void* ud, void*) {
    auto* r = static_cast<AdapterResult*>(ud);
    if (status == WGPURequestAdapterStatus_Success) {
        r->adapter = adapter;
    } else {
        const std::string_view text = fromStrView(msg);
        VORTEX_ERROR("RHI", "wgpu requestAdapter failed: %.*s",
                     static_cast<int>(text.size()), text.data());
    }
    r->done = true;
}

struct DeviceResult { WGPUDevice device = nullptr; bool done = false; };
void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg,
              void* ud, void*) {
    auto* r = static_cast<DeviceResult*>(ud);
    if (status == WGPURequestDeviceStatus_Success) {
        r->device = device;
    } else {
        const std::string_view text = fromStrView(msg);
        VORTEX_ERROR("RHI", "wgpu requestDevice failed: %.*s",
                     static_cast<int>(text.size()), text.data());
    }
    r->done = true;
}

struct WorkDone { bool done = false; };
void onWorkDone(WGPUQueueWorkDoneStatus, WGPUStringView, void* ud, void*) {
    static_cast<WorkDone*>(ud)->done = true;
}

// Drives the instance until `done` flips.
//
// The standard API is asynchronous everywhere. The pre-standard wgpu-native header happened to
// invoke these callbacks inline, and the old code asserted on exactly that — an assumption that was
// a property of one implementation, not of the API.
void pump(WGPUInstance instance, const bool& done) {
    while (!done) wgpuInstanceProcessEvents(instance);
}

// Slots the push-constant ring reserves per frame. At the 256-byte alignment most adapters report
// this is a 1 MiB buffer, allocated once, and far beyond what a frame of sprites pushes.
constexpr u64 kPushRingSlots = 4096;

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
    m_instance = wgpuCreateInstance(nullptr);
    if (!m_instance) { VORTEX_ERROR("RHI", "wgpuCreateInstance failed"); return; }

    m_surface = createWGPUSurface(m_instance, window.nativeHandleKind(),
                                  window.nativeDisplayHandle(), window.nativeWindowHandle());
    if (!m_surface) return;

    AdapterResult ar{};
    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = m_surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo adapterCb{};
    adapterCb.mode      = WGPUCallbackMode_AllowProcessEvents;
    adapterCb.callback  = onAdapter;
    adapterCb.userdata1 = &ar;
    wgpuInstanceRequestAdapter(m_instance, &opts, adapterCb);
    pump(m_instance, ar.done);
    m_adapter = ar.adapter;
    if (!m_adapter) return;

    // Request exactly what the adapter supports. Zero-initialised alignment limits would be
    // rejected ("0 is better than allowed") — alignment limits are inverted, so echoing the
    // adapter's own values back is always valid.
    WGPULimits supported = WGPU_LIMITS_INIT;
    wgpuAdapterGetLimits(m_adapter, &supported);
    m_pushStride = supported.minUniformBufferOffsetAlignment;
    if (m_pushStride < kMaxPushConstantSize) m_pushStride = kMaxPushConstantSize;

    DeviceResult dr{};
    WGPUDeviceDescriptor devDesc = WGPU_DEVICE_DESCRIPTOR_INIT;
    devDesc.requiredLimits = &supported;
    devDesc.uncapturedErrorCallbackInfo.callback = errorCallback;

    WGPURequestDeviceCallbackInfo deviceCb{};
    deviceCb.mode      = WGPUCallbackMode_AllowProcessEvents;
    deviceCb.callback  = onDevice;
    deviceCb.userdata1 = &dr;
    wgpuAdapterRequestDevice(m_adapter, &devDesc, deviceCb);
    pump(m_instance, dr.done);
    m_device = dr.device;
    if (!m_device) return;

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

    // A layout with no bindings, used to plug the holes in pipeline layouts (see m_emptyBGL).
    WGPUBindGroupLayoutDescriptor emptyDesc{};
    m_emptyBGL = wgpuDeviceCreateBindGroupLayout(m_device, &emptyDesc);

    createPushConstantRing();

    VORTEX_INFO("RHI", "WebGPU device initialised");
}

void WebGPUDevice::createPushConstantRing() {
    m_pushRingSize = static_cast<u64>(m_pushStride) * kPushRingSlots;

    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = strView("push_constant_ring");
    bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bd.size  = m_pushRingSize;
    m_pushRing = wgpuDeviceCreateBuffer(m_device, &bd);

    // One dynamic-offset uniform binding: a single bind group serves every push in the frame, and
    // only the offset changes from draw to draw.
    WGPUBindGroupLayoutEntry entry{};
    entry.binding                 = 0;
    entry.visibility              = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type             = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = true;
    entry.buffer.minBindingSize   = kMaxPushConstantSize;

    WGPUBindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 1;
    bglDesc.entries    = &entry;
    m_pushBGL = wgpuDeviceCreateBindGroupLayout(m_device, &bglDesc);

    WGPUBindGroupEntry bge{};
    bge.binding = 0;
    bge.buffer  = m_pushRing;
    bge.offset  = 0;
    bge.size    = kMaxPushConstantSize;

    WGPUBindGroupDescriptor bgd{};
    bgd.layout     = m_pushBGL;
    bgd.entryCount = 1;
    bgd.entries    = &bge;
    m_pushBindGroup = wgpuDeviceCreateBindGroup(m_device, &bgd);
}

u32 WebGPUDevice::writePushConstants(const void* data, u32 size) {
    VORTEX_ASSERT(size <= kMaxPushConstantSize, "push constant block exceeds kMaxPushConstantSize");
    if (size > kMaxPushConstantSize) return UINT32_MAX;

    if (m_pushRingHead + m_pushStride > m_pushRingSize) {
        static bool warned = false;
        if (!warned) {
            VORTEX_WARN("RHI", "push-constant ring exhausted this frame; draws will be skipped");
            warned = true;
        }
        return UINT32_MAX;
    }

    const u64 offset = m_pushRingHead;
    m_pushRingHead += m_pushStride;

    // The binding is minBindingSize bytes wide regardless of how few the caller pushed, so the
    // slot is written whole — a short write would leave the tail as stale data from last frame.
    u8 staging[kMaxPushConstantSize]{};
    std::memcpy(staging, data, size);
    wgpuQueueWriteBuffer(m_queue, m_pushRing, offset, staging, kMaxPushConstantSize);

    return static_cast<u32>(offset);
}

WebGPUDevice::~WebGPUDevice() {
    waitIdle();

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

    if (m_pushBindGroup) wgpuBindGroupRelease(m_pushBindGroup);
    if (m_pushRing)      { wgpuBufferDestroy(m_pushRing); wgpuBufferRelease(m_pushRing); }
    if (m_pushBGL)       wgpuBindGroupLayoutRelease(m_pushBGL);
    if (m_emptyBGL)      wgpuBindGroupLayoutRelease(m_emptyBGL);
    if (m_materialBGL)   wgpuBindGroupLayoutRelease(m_materialBGL);
    if (m_uniformBGL)    wgpuBindGroupLayoutRelease(m_uniformBGL);
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
    WGPUBufferUsage usage = WGPUBufferUsage_CopyDst;   // allow updateBuffer()
    if (hasFlag(desc.usage, BufferUsage::Vertex))  usage |= WGPUBufferUsage_Vertex;
    if (hasFlag(desc.usage, BufferUsage::Index))   usage |= WGPUBufferUsage_Index;
    if (hasFlag(desc.usage, BufferUsage::Uniform)) usage |= WGPUBufferUsage_Uniform;
    if (hasFlag(desc.usage, BufferUsage::Storage)) usage |= WGPUBufferUsage_Storage;
    if (hasFlag(desc.usage, BufferUsage::Staging)) usage |= WGPUBufferUsage_CopySrc;

    const u64 size = (desc.size + 3) & ~u64{3};   // WebGPU requires 4-byte sizes

    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label            = strView(desc.debugName);
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

    WGPUTextureUsage usage = WGPUTextureUsage_None;
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

    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.label         = strView(desc.debugName);
    td.usage         = usage;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = {desc.width, desc.height, 1};
    td.format        = tex.format;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    tex.texture      = wgpuDeviceCreateTexture(m_device, &td);

    if (pixels) {
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tex.texture;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
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
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
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
    // WebGPU consumes WGSL only: SPIR-V is a Vulkan-side detail, and browsers reject it outright.
    // The shader build transpiles every stage to both languages (see cmake/VortexShaders.cmake).
    if (desc.vertexWgsl.empty() || desc.fragmentWgsl.empty()) {
        VORTEX_ERROR("RHI", "WebGPU pipeline '%s' has no WGSL; the shader was never transpiled",
                     desc.debugName ? desc.debugName : "?");
        return {};
    }

    auto makeModule = [&](const std::string& wgsl) -> WGPUShaderModule {
        WGPUShaderSourceWGSL src = WGPU_SHADER_SOURCE_WGSL_INIT;
        src.code = strView(wgsl);
        WGPUShaderModuleDescriptor smd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        smd.nextInChain = &src.chain;
        return wgpuDeviceCreateShaderModule(m_device, &smd);
    };
    WGPUShaderModule vs = makeModule(desc.vertexWgsl);
    WGPUShaderModule fs = makeModule(desc.fragmentWgsl);

    // A pipeline layout is indexed by @group, so the array must be dense: a shader that binds its
    // material at 0 and its (emulated) push constants at 3 still needs placeholders at 1 and 2.
    WGPUBindGroupLayout bgls[kPushConstantGroup + 1];
    u32 bglCount = 0;
    if (desc.hasMaterialTexture) bgls[bglCount++] = m_materialBGL;
    if (desc.hasUniformBuffer)   bgls[bglCount++] = m_uniformBGL;

    if (desc.pushConstantSize > 0) {
        while (bglCount < kPushConstantGroup) bgls[bglCount++] = m_emptyBGL;
        bgls[bglCount++] = m_pushBGL;
    }

    WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pld.bindGroupLayoutCount = bglCount;
    pld.bindGroupLayouts     = bglCount > 0 ? bgls : nullptr;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(m_device, &pld);

    // Vertex layout.
    std::vector<WGPUVertexAttribute> attrs;
    attrs.reserve(desc.vertexLayout.attributes.size());
    for (const VertexAttribute& a : desc.vertexLayout.attributes) {
        WGPUVertexAttribute wa = WGPU_VERTEX_ATTRIBUTE_INIT;
        wa.format         = toWGPUVertexFormat(a.format);
        wa.offset         = a.offset;
        wa.shaderLocation = a.location;
        attrs.push_back(wa);
    }

    WGPUVertexBufferLayout vbl = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbl.arrayStride    = desc.vertexLayout.stride;
    vbl.stepMode       = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = attrs.size();
    vbl.attributes     = attrs.data();

    const bool hasVertices = desc.vertexLayout.stride > 0;

    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pd.label              = strView(desc.debugName);
    pd.layout             = layout;
    pd.vertex.module      = vs;
    pd.vertex.entryPoint  = strView("main");
    pd.vertex.bufferCount = hasVertices ? 1u : 0u;
    pd.vertex.buffers     = hasVertices ? &vbl : nullptr;

    pd.primitive.topology  = toWGPUTopology(desc.topology);
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.primitive.cullMode  = toWGPUCull(desc.cull);

    pd.multisample.count = desc.sampleCount > 0 ? desc.sampleCount : 1;
    pd.multisample.mask  = 0xFFFFFFFFu;

    // Depth-stencil.
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (desc.depthFormat != Format::Undefined) {
        ds.format            = toWGPUFormat(desc.depthFormat);
        ds.depthWriteEnabled = desc.depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare      = desc.depthTest ? toWGPUCompare(desc.depthCompare)
                                              : WGPUCompareFunction_Always;
        pd.depthStencil = &ds;
    }

    // Fragment + colour target.
    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};

    WGPUBlendState additive{};
    additive.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
    additive.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format    = toWGPUFormat(desc.colorFormat);
    colorTarget.writeMask = WGPUColorWriteMask_All;
    if (desc.additiveBlend)    colorTarget.blend = &additive;
    else if (desc.alphaBlend)  colorTarget.blend = &blend;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fs;
    fragment.entryPoint  = strView("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    // A depth-only pass (the shadow map) has no colour attachment at all.
    if (desc.colorFormat != Format::Undefined) pd.fragment = &fragment;

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
    // Take the surface's *preferred* format, which is always first. Forcing BGRA8Unorm for parity
    // with Vulkan costs a full-surface copy every frame in the browser, where the canvas prefers
    // RGBA8Unorm — and the swapchain format is not something anything above the RHI can see.
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &caps);
    const WGPUTextureFormat chosen = caps.formatCount > 0 ? caps.formats[0]
                                                          : WGPUTextureFormat_BGRA8Unorm;
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    return std::make_unique<WebGPUSwapchain>(*this, m_surface, desc, chosen);
}

FrameContext WebGPUDevice::beginFrame(ISwapchain& scBase) {
    auto& sc = static_cast<WebGPUSwapchain&>(scBase);
    FrameContext fc{};

    if (sc.needsConfigure()) sc.configure();

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(sc.surface(), &st);

    // Suboptimal still hands back a usable texture — the surface merely wants reconfiguring, which
    // is the normal state for one frame after a resize. Anything else does not.
    const bool suboptimal = st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal;
    const bool usable     = st.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal || suboptimal;
    if (!usable) {
        if (st.texture) wgpuTextureRelease(st.texture);
        sc.markNeedsConfigure();
        return fc;
    }
    if (suboptimal) sc.markNeedsConfigure();

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

    // Every frame restarts at the bottom of the push-constant ring. Safe because the previous
    // frame's command buffer was already submitted, and queue writes are ordered against it.
    m_pushRingHead = 0;

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

    wgpuInstanceProcessEvents(m_instance);
    m_frameActive = false;
}

void WebGPUDevice::waitIdle() {
    if (!m_device || !m_queue) return;

    WorkDone wd{};
    WGPUQueueWorkDoneCallbackInfo cb{};
    cb.mode      = WGPUCallbackMode_AllowProcessEvents;
    cb.callback  = onWorkDone;
    cb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(m_queue, cb);
    pump(m_instance, wd.done);
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
