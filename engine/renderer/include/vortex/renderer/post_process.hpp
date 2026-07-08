#pragma once
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_graph.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::rhi { class IGraphicsDevice; }

namespace vortex::renderer {

// HDR post-processing chain: bloom (bright-pass -> separable blur -> additive
// composite) followed by ACES tone mapping and linear->sRGB. Each stage is a
// fullscreen pass added to the render graph, so effects compose as graph nodes.
class PostProcess {
public:
    struct Settings {
        f32  bloomThreshold = 1.0f;   // luminance above which pixels bloom
        f32  bloomIntensity = 0.6f;   // additive strength of the blurred bloom
        f32  exposure       = 1.0f;   // multiplier before tone mapping
        bool bloom          = true;
        bool fxaa           = true;   // edge anti-aliasing on the tone-mapped image
    };

    // `hdrFormat` is the scene colour target's format (a float format for HDR);
    // `outputFormat` is the final target (the swapchain/backbuffer format).
    PostProcess(rhi::IGraphicsDevice& device, rhi::Format hdrFormat, rhi::Format outputFormat);
    ~PostProcess();
    PostProcess(const PostProcess&)            = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    // Adds the bloom + tone-map passes. `sceneHdr` must already have the lit
    // scene rendered into it; `output` is the final (backbuffer) target. Call
    // between graph.beginFrame() and graph.execute().
    void addPasses(RenderGraph& graph, RenderGraph::ResourceId sceneHdr,
                   RenderGraph::ResourceId output, u32 width, u32 height,
                   const Settings& settings);

    void addPasses(RenderGraph& graph, RenderGraph::ResourceId sceneHdr,
                   RenderGraph::ResourceId output, u32 width, u32 height) {
        addPasses(graph, sceneHdr, output, width, height, Settings{});
    }

private:
    rhi::IGraphicsDevice& m_device;
    rhi::Format           m_hdrFormat;
    rhi::Format           m_outputFormat;
    rhi::PipelineHandle   m_bright;
    rhi::PipelineHandle   m_blur;
    rhi::PipelineHandle   m_composite;
    rhi::PipelineHandle   m_tonemap;
    rhi::PipelineHandle   m_fxaa;
};

}
