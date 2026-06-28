#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::rhi { class IGraphicsDevice; class ICommandList; }

namespace vortex::debug {


struct ImGuiInput {
    f32  displayWidth  = 0.0f;
    f32  displayHeight = 0.0f;
    Vec2 mouse{0.0f, 0.0f};      // pixels, top-left origin
    bool mouseDown[3] = {false, false, false};   // L, R, M
    f32  scroll = 0.0f;          // wheel delta this frame
};

class ImGuiLayer {
public:
    ImGuiLayer(rhi::IGraphicsDevice& device, rhi::Format colorFormat);
    ~ImGuiLayer();
    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void newFrame(const ImGuiInput& input, f32 dt);
    void render(rhi::ICommandList& cmd);

    void showDemoWindow(bool* open = nullptr);

    [[nodiscard]] bool wantsMouse() const;
    [[nodiscard]] bool wantsKeyboard() const;

private:
    void createDeviceObjects(rhi::Format colorFormat);
    void ensureBufferCapacity(u32 vtxCount, u32 idxCount);

    rhi::IGraphicsDevice& m_device;

    rhi::PipelineHandle  m_pipeline;
    rhi::SamplerHandle   m_sampler;
    rhi::TextureHandle   m_fontTexture;
    rhi::BindGroupHandle m_fontBindGroup;

    rhi::BufferHandle m_vertexBuffers[rhi::kMaxFramesInFlight]{};
    rhi::BufferHandle m_indexBuffers[rhi::kMaxFramesInFlight]{};
    u32               m_vertexCapacity[rhi::kMaxFramesInFlight]{};
    u32               m_indexCapacity[rhi::kMaxFramesInFlight]{};
    u32               m_frame = 0;
};

}
