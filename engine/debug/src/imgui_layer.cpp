#include "vortex/debug/imgui_layer.hpp"

#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include <imgui.h>

#include "imgui_vert_spv.h"
#include "imgui_frag_spv.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace vortex::debug {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

ImTextureID packBindGroup(rhi::BindGroupHandle h) {
    return static_cast<ImTextureID>((static_cast<u64>(h.generation) << 32) | h.index);
}
rhi::BindGroupHandle unpackBindGroup(ImTextureID id) {
    const u64 bits = static_cast<u64>(id);
    return rhi::BindGroupHandle{static_cast<u32>(bits & 0xFFFFFFFFu),
                                static_cast<u32>(bits >> 32)};
}

struct PushConstants {
    f32 scale[2];
    f32 translate[2];
};

}

ImGuiLayer::ImGuiLayer(rhi::IGraphicsDevice& device, rhi::Format colorFormat)
    : m_device(device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "vortex_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.IniFilename = nullptr;   // don't write imgui.ini in examples
    ImGui::StyleColorsDark();

    createDeviceObjects(colorFormat);
}

ImGuiLayer::~ImGuiLayer() {
    m_device.waitIdle();
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        if (m_vertexBuffers[i].valid()) m_device.destroyBuffer(m_vertexBuffers[i]);
        if (m_indexBuffers[i].valid())  m_device.destroyBuffer(m_indexBuffers[i]);
    }
    if (m_fontBindGroup.valid()) m_device.destroyBindGroup(m_fontBindGroup);
    if (m_fontTexture.valid())   m_device.destroyTexture(m_fontTexture);
    if (m_sampler.valid())       m_device.destroySampler(m_sampler);
    if (m_pipeline.valid())      m_device.destroyPipeline(m_pipeline);
    ImGui::DestroyContext();
}

void ImGuiLayer::createDeviceObjects(rhi::Format colorFormat) {
    // Font atlas -> RGBA8 texture.
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    m_fontTexture = m_device.createTexture(
        {.width = static_cast<u32>(width), .height = static_cast<u32>(height),
         .debugName = "imgui_font"},
        pixels);
    m_sampler = m_device.createSampler({.minFilter = rhi::Filter::Linear,
                                        .magFilter = rhi::Filter::Linear,
                                        .addressU  = rhi::AddressMode::ClampToEdge,
                                        .addressV  = rhi::AddressMode::ClampToEdge});
    m_fontBindGroup = m_device.createBindGroup({.texture = m_fontTexture, .sampler = m_sampler});
    io.Fonts->SetTexID(packBindGroup(m_fontBindGroup));

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = toBytes(imgui_vert_spv, imgui_vert_spv_size);
    pd.fragmentSpirv       = toBytes(imgui_frag_spv, imgui_frag_spv_size);
    pd.vertexLayout.stride = sizeof(ImDrawVert);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float2,   .offset = offsetof(ImDrawVert, pos)},
        {.location = 1, .format = rhi::VertexFormat::Float2,   .offset = offsetof(ImDrawVert, uv)},
        {.location = 2, .format = rhi::VertexFormat::UNorm4x8, .offset = offsetof(ImDrawVert, col)},
    };
    pd.topology           = rhi::PrimitiveTopology::TriangleList;
    pd.cull               = rhi::CullMode::None;
    pd.colorFormat        = colorFormat;
    pd.alphaBlend         = true;
    pd.hasMaterialTexture = true;
    pd.pushConstantSize   = sizeof(PushConstants);
    pd.debugName          = "imgui_pipeline";
    m_pipeline = m_device.createGraphicsPipeline(pd);
}

void ImGuiLayer::newFrame(const ImGuiInput& input, f32 dt) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(input.displayWidth, input.displayHeight);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;

    io.AddMousePosEvent(input.mouse.x, input.mouse.y);
    io.AddMouseButtonEvent(0, input.mouseDown[0]);
    io.AddMouseButtonEvent(1, input.mouseDown[1]);
    io.AddMouseButtonEvent(2, input.mouseDown[2]);
    if (input.scroll != 0.0f) io.AddMouseWheelEvent(0.0f, input.scroll);

    ImGui::NewFrame();
}

void ImGuiLayer::ensureBufferCapacity(u32 vtxCount, u32 idxCount) {
    const u32 f = m_frame;
    if (m_vertexCapacity[f] < vtxCount) {
        if (m_vertexBuffers[f].valid()) {
            m_device.waitIdle();   // growth is rare; safe to drain before recreate
            m_device.destroyBuffer(m_vertexBuffers[f]);
        }
        m_vertexCapacity[f] = vtxCount + 4096;
        m_vertexBuffers[f] = m_device.createBuffer(
            {.size = static_cast<u64>(m_vertexCapacity[f]) * sizeof(ImDrawVert),
             .usage = rhi::BufferUsage::Vertex, .domain = rhi::MemoryDomain::Upload,
             .debugName = "imgui_vertices"});
    }
    if (m_indexCapacity[f] < idxCount) {
        if (m_indexBuffers[f].valid()) {
            m_device.waitIdle();
            m_device.destroyBuffer(m_indexBuffers[f]);
        }
        m_indexCapacity[f] = idxCount + 8192;
        m_indexBuffers[f] = m_device.createBuffer(
            {.size = static_cast<u64>(m_indexCapacity[f]) * sizeof(ImDrawIdx),
             .usage = rhi::BufferUsage::Index, .domain = rhi::MemoryDomain::Upload,
             .debugName = "imgui_indices"});
    }
}

void ImGuiLayer::render(rhi::ICommandList& cmd) {
    ImGui::Render();
    const ImDrawData* dd = ImGui::GetDrawData();
    if (!dd || dd->TotalVtxCount == 0) return;

    const u32 vtxCount = static_cast<u32>(dd->TotalVtxCount);
    const u32 idxCount = static_cast<u32>(dd->TotalIdxCount);
    ensureBufferCapacity(vtxCount, idxCount);

    // Concatenate all command lists into the per-frame buffers.
    std::vector<ImDrawVert> verts(vtxCount);
    std::vector<ImDrawIdx>  idxs(idxCount);
    u32 vtxBase = 0, idxBase = 0;
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        std::memcpy(&verts[vtxBase], cl->VtxBuffer.Data,
                    cl->VtxBuffer.Size * sizeof(ImDrawVert));
        std::memcpy(&idxs[idxBase], cl->IdxBuffer.Data,
                    cl->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxBase += static_cast<u32>(cl->VtxBuffer.Size);
        idxBase += static_cast<u32>(cl->IdxBuffer.Size);
    }

    const rhi::BufferHandle vbo = m_vertexBuffers[m_frame];
    const rhi::BufferHandle ibo = m_indexBuffers[m_frame];
    m_device.updateBuffer(vbo, verts.data(), verts.size() * sizeof(ImDrawVert));
    m_device.updateBuffer(ibo, idxs.data(), idxs.size() * sizeof(ImDrawIdx));

    // Ortho with top-left origin, matching ImGui's screen space (Vulkan Y-down).
    PushConstants pc;
    pc.scale[0] = 2.0f / dd->DisplaySize.x;
    pc.scale[1] = 2.0f / dd->DisplaySize.y;
    pc.translate[0] = -1.0f - dd->DisplayPos.x * pc.scale[0];
    pc.translate[1] = -1.0f - dd->DisplayPos.y * pc.scale[1];

    cmd.setPipeline(m_pipeline);
    cmd.pushConstants(&pc, sizeof(pc));
    cmd.setVertexBuffer(0, vbo);
    cmd.setIndexBuffer(ibo, sizeof(ImDrawIdx) == 2 ? rhi::IndexType::U16 : rhi::IndexType::U32);

    const ImVec2 clipOff   = dd->DisplayPos;
    const ImVec2 clipScale = dd->FramebufferScale;
    const f32 fbW = dd->DisplaySize.x * clipScale.x;
    const f32 fbH = dd->DisplaySize.y * clipScale.y;

    u32 globalVtx = 0, globalIdx = 0;
    for (int n = 0; n < dd->CmdListsCount; ++n) {
        const ImDrawList* cl = dd->CmdLists[n];
        for (int c = 0; c < cl->CmdBuffer.Size; ++c) {
            const ImDrawCmd& pcmd = cl->CmdBuffer[c];
            if (pcmd.UserCallback) continue;   // custom callbacks unsupported here

            // Clip rect -> framebuffer scissor, clamped to the surface.
            f32 cx = (pcmd.ClipRect.x - clipOff.x) * clipScale.x;
            f32 cy = (pcmd.ClipRect.y - clipOff.y) * clipScale.y;
            f32 cz = (pcmd.ClipRect.z - clipOff.x) * clipScale.x;
            f32 cw = (pcmd.ClipRect.w - clipOff.y) * clipScale.y;
            cx = std::max(cx, 0.0f);
            cy = std::max(cy, 0.0f);
            cz = std::min(cz, fbW);
            cw = std::min(cw, fbH);
            if (cz <= cx || cw <= cy) continue;

            cmd.setScissor(static_cast<i32>(cx), static_cast<i32>(cy),
                           static_cast<u32>(cz - cx), static_cast<u32>(cw - cy));

            rhi::BindGroupHandle bg = unpackBindGroup(pcmd.GetTexID());
            cmd.setBindGroup(0, bg.valid() ? bg : m_fontBindGroup);

            cmd.drawIndexed(pcmd.ElemCount, 1,
                            globalIdx + pcmd.IdxOffset,
                            static_cast<i32>(globalVtx + pcmd.VtxOffset), 0);
        }
        globalVtx += static_cast<u32>(cl->VtxBuffer.Size);
        globalIdx += static_cast<u32>(cl->IdxBuffer.Size);
    }

    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
}

void ImGuiLayer::showDemoWindow(bool* open) { ImGui::ShowDemoWindow(open); }

bool ImGuiLayer::wantsMouse() const    { return ImGui::GetIO().WantCaptureMouse; }
bool ImGuiLayer::wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

}
