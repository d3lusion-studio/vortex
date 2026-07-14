#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_graph.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <vector>

namespace vortex::rhi { class IGraphicsDevice; }

namespace vortex::renderer {

// HDR post-processing chain: bloom (bright-pass -> separable blur -> additive
// composite) followed by ACES tone mapping and linear->sRGB. Each stage is a
// fullscreen pass added to the render graph, so effects compose as graph nodes.
// The curve that maps HDR scene light onto the display range. `None` just clamps,
// which is the honest way to see what the scene actually produced.
enum class ToneMapper { None, Reinhard, ACES, Filmic };

class PostProcess {
public:
    struct Settings {
        f32  bloomThreshold = 1.0f;   // luminance above which pixels bloom
        f32  bloomIntensity = 0.6f;   // additive strength of the blurred bloom
        f32  exposure       = 1.0f;   // multiplier before tone mapping
        bool bloom          = true;
        bool fxaa           = true;   // edge anti-aliasing on the tone-mapped image

        ToneMapper toneMapper = ToneMapper::ACES;

        // Colour grading. The filter multiplies the HDR scene *before* the tone
        // curve; contrast/saturation/gamma shape the image after it.
        Vec3 colorFilter{1.0f, 1.0f, 1.0f};
        f32  contrast   = 1.0f;
        f32  saturation = 1.0f;
        f32  gamma      = 1.0f;
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

    // Per-pixel motion blur from the G-buffer's velocity target. Camera motion AND
    // object motion, because the velocity was computed per vertex against that instance's
    // own previous transform.
    //
    // Returns the blurred colour target — feed THAT to addPasses() as the scene, since
    // a pass cannot read and write the same texture. Add it before the post chain.
    // Deferred only: without a G-buffer there is no velocity to read.
    [[nodiscard]] RenderGraph::ResourceId addMotionBlur(
        RenderGraph& graph, RenderGraph::ResourceId sceneHdr,
        RenderGraph::ResourceId velocity, u32 width, u32 height,
        f32 strength = 1.0f, u32 samples = 8);

private:
    rhi::IGraphicsDevice& m_device;
    rhi::Format           m_hdrFormat;
    rhi::Format           m_outputFormat;
    rhi::PipelineHandle   m_bright;
    rhi::PipelineHandle   m_blur;
    rhi::PipelineHandle   m_composite;
    rhi::PipelineHandle   m_tonemap;
    rhi::PipelineHandle   m_fxaa;
    rhi::PipelineHandle   m_motionBlur;

    // Motion blur reads two textures (colour + depth), which is one more than the plain
    // fullscreen set holds — so it borrows the PBR material set, and needs a neutral
    // texture for the slots it does not use.
    rhi::TextureHandle    m_white;
    rhi::SamplerHandle    m_sampler;
    struct MotionCache {
        rhi::TextureHandle   scene, velocity;
        rhi::BindGroupHandle group;
    };
    std::vector<MotionCache> m_motionCache;
};

}
