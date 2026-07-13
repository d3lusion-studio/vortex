#include "wgpu_command_list.hpp"
#include "wgpu_device.hpp"

#include "vortex/core/log.hpp"

#include <algorithm>
#include <cstring>

namespace vortex::rhi::wgpu {

void WebGPUCommandList::beginRenderPass(const RenderPassDesc& desc) {
    if (m_deferred) return;   // secondaries record into an existing pass

    WebGPUTexture* color = m_device->getTexture(desc.color.target);
    if (!color || !color->view) return;

    // Must use the INIT macro, not {}: a zero-initialised depthSlice reads as "slice 0", and the
    // validator then rejects a 2D view for having been given a 3D-only parameter.
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view       = color->view;
    ca.loadOp     = toWGPULoadOp(desc.color.loadOp);
    ca.storeOp    = toWGPUStoreOp(desc.color.storeOp);
    ca.clearValue = {desc.color.clearColor[0], desc.color.clearColor[1],
                     desc.color.clearColor[2], desc.color.clearColor[3]};

    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments     = &ca;

    WGPURenderPassDepthStencilAttachment ds{};
    WebGPUTexture* depth = m_device->getTexture(desc.depth.target);
    if (depth && depth->view) {
        ds.view              = depth->view;
        ds.depthLoadOp       = toWGPULoadOp(desc.depth.loadOp);
        ds.depthStoreOp      = toWGPUStoreOp(desc.depth.storeOp);
        ds.depthClearValue   = desc.depth.clearDepth;
        ds.stencilLoadOp     = WGPULoadOp_Undefined;
        ds.stencilStoreOp    = WGPUStoreOp_Undefined;
        rp.depthStencilAttachment = &ds;
    }

    m_encoder = wgpuCommandEncoderBeginRenderPass(m_device->frameEncoder(), &rp);
}

void WebGPUCommandList::endRenderPass() {
    if (m_deferred || !m_encoder) return;
    wgpuRenderPassEncoderEnd(m_encoder);
    wgpuRenderPassEncoderRelease(m_encoder);
    m_encoder = nullptr;
}

void WebGPUCommandList::transition(TextureHandle, ResourceState) {
    // WebGPU tracks resource state / barriers internally — nothing to do.
}


void WebGPUCommandList::setPipeline(PipelineHandle h) {
    if (m_deferred) { m_commands.push_back({.op = Op::Pipeline, .pipeline = h}); return; }
    doPipeline(h);
}

void WebGPUCommandList::setBindGroup(u32 slot, BindGroupHandle h) {
    if (m_deferred) { m_commands.push_back({.op = Op::BindGroup, .bindGroup = h, .slot = slot}); return; }
    doBindGroup(slot, h);
}

void WebGPUCommandList::pushConstants(const void* data, u32 size) {
    if (m_deferred) {
        Cmd c{.op = Op::Push};
        c.pushOffset = static_cast<u32>(m_pushData.size());
        c.pushSize   = size;
        m_pushData.resize(m_pushData.size() + size);
        std::memcpy(m_pushData.data() + c.pushOffset, data, size);
        m_commands.push_back(c);
        return;
    }
    doPush(data, size);
}

void WebGPUCommandList::setViewport(const Viewport& vp) {
    if (m_deferred) { m_commands.push_back({.op = Op::Viewport, .viewport = vp}); return; }
    doViewport(vp);
}

void WebGPUCommandList::setScissor(i32 x, i32 y, u32 w, u32 h) {
    if (m_deferred) { m_commands.push_back({.op = Op::Scissor, .sx = x, .sy = y, .sw = w, .sh = h}); return; }
    doScissor(x, y, w, h);
}

void WebGPUCommandList::setVertexBuffer(u32 slot, BufferHandle h, u64 offset) {
    if (m_deferred) {
        m_commands.push_back({.op = Op::VertexBuffer, .buffer = h, .slot = slot, .offset = offset});
        return;
    }
    doVertexBuffer(slot, h, offset);
}

void WebGPUCommandList::setIndexBuffer(BufferHandle h, IndexType type) {
    if (m_deferred) { m_commands.push_back({.op = Op::IndexBuffer, .buffer = h, .indexType = type}); return; }
    doIndexBuffer(h, type);
}

void WebGPUCommandList::draw(u32 vc, u32 ic, u32 fv, u32 fi) {
    if (m_deferred) { m_commands.push_back({.op = Op::Draw, .a = vc, .b = ic, .c = fv, .e = fi}); return; }
    doDraw(vc, ic, fv, fi);
}

void WebGPUCommandList::drawIndexed(u32 ic, u32 inst, u32 fi, i32 vo, u32 finst) {
    if (m_deferred) {
        m_commands.push_back({.op = Op::DrawIndexed, .a = ic, .b = inst, .c = fi, .e = finst, .d = vo});
        return;
    }
    doDrawIndexed(ic, inst, fi, vo, finst);
}

void WebGPUCommandList::dispatch(u32, u32, u32) {
    // Stub: compute requires a WGPUComputePassEncoder + compute pipeline, neither
    // of which exist yet. Warn once instead of recording invalid work.
    static bool warned = false;
    if (!warned) { VORTEX_WARN("RHI", "dispatch() is not implemented (compute stub)"); warned = true; }
}


void WebGPUCommandList::doPipeline(PipelineHandle h) {
    WebGPUPipeline* p = m_device->getPipeline(h);
    if (!p) return;
    m_currentPushSize = p->pushConstantSize;
    wgpuRenderPassEncoderSetPipeline(m_encoder, p->pipeline);
}

void WebGPUCommandList::doBindGroup(u32 slot, BindGroupHandle h) {
    WebGPUBindGroup* g = m_device->getBindGroup(h);
    if (!g) return;
    wgpuRenderPassEncoderSetBindGroup(m_encoder, slot, g->group, 0, nullptr);
}

void WebGPUCommandList::doPush(const void* data, u32 size) {
    // WebGPU has no push constants. The block goes into the device's per-frame uniform ring, and
    // the draw reads it through a dynamic offset into a bind group at the reserved group index —
    // which is exactly where the WebGPU shader variant declares it (set 3).
    const u32 offset = m_device->writePushConstants(data, size);
    if (offset == UINT32_MAX) return;   // ring exhausted; skip rather than draw with stale data

    wgpuRenderPassEncoderSetBindGroup(m_encoder, kPushConstantGroup, m_device->pushBindGroup(),
                                      1, &offset);
}

void WebGPUCommandList::doViewport(const Viewport& vp) {
    wgpuRenderPassEncoderSetViewport(m_encoder, vp.x, vp.y, vp.width, vp.height,
                                     vp.minDepth, vp.maxDepth);
}

void WebGPUCommandList::doScissor(i32 x, i32 y, u32 w, u32 h) {
    wgpuRenderPassEncoderSetScissorRect(m_encoder, static_cast<u32>(std::max(0, x)),
                                        static_cast<u32>(std::max(0, y)), w, h);
}

void WebGPUCommandList::doVertexBuffer(u32 slot, BufferHandle h, u64 offset) {
    WebGPUBuffer* b = m_device->getBuffer(h);
    if (!b) return;
    wgpuRenderPassEncoderSetVertexBuffer(m_encoder, slot, b->buffer, offset, WGPU_WHOLE_SIZE);
}

void WebGPUCommandList::doIndexBuffer(BufferHandle h, IndexType type) {
    WebGPUBuffer* b = m_device->getBuffer(h);
    if (!b) return;
    const WGPUIndexFormat fmt = (type == IndexType::U16) ? WGPUIndexFormat_Uint16
                                                         : WGPUIndexFormat_Uint32;
    wgpuRenderPassEncoderSetIndexBuffer(m_encoder, b->buffer, fmt, 0, WGPU_WHOLE_SIZE);
}

void WebGPUCommandList::doDraw(u32 vc, u32 ic, u32 fv, u32 fi) {
    wgpuRenderPassEncoderDraw(m_encoder, vc, ic, fv, fi);
}

void WebGPUCommandList::doDrawIndexed(u32 ic, u32 inst, u32 fi, i32 vo, u32 finst) {
    wgpuRenderPassEncoderDrawIndexed(m_encoder, ic, inst, fi, vo, finst);
}


void WebGPUCommandList::replayOnto(WGPURenderPassEncoder enc) {
    m_encoder  = enc;
    m_deferred = false;
    for (const Cmd& c : m_commands) {
        switch (c.op) {
            case Op::Pipeline:     doPipeline(c.pipeline); break;
            case Op::BindGroup:    doBindGroup(c.slot, c.bindGroup); break;
            case Op::Push:         doPush(m_pushData.data() + c.pushOffset, c.pushSize); break;
            case Op::Viewport:     doViewport(c.viewport); break;
            case Op::Scissor:      doScissor(c.sx, c.sy, c.sw, c.sh); break;
            case Op::VertexBuffer: doVertexBuffer(c.slot, c.buffer, c.offset); break;
            case Op::IndexBuffer:  doIndexBuffer(c.buffer, c.indexType); break;
            case Op::Draw:         doDraw(c.a, c.b, c.c, c.e); break;
            case Op::DrawIndexed:  doDrawIndexed(c.a, c.b, c.c, c.d, c.e); break;
        }
    }
    m_encoder  = nullptr;
    m_deferred = true;
}

}
