#include "vortex/renderer/post_process.hpp"

#include "vortex/core/profiler.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

#include "fullscreen_vert_spv.h"
#include "bright_frag_spv.h"
#include "blur_frag_spv.h"
#include "composite_frag_spv.h"
#include "tonemap_frag_spv.h"
#include "fxaa_frag_spv.h"

#include <cstring>

namespace vortex::renderer {

namespace {

std::vector<std::byte> toBytes(const unsigned char* data, unsigned long size) {
    std::vector<std::byte> out(size);
    std::memcpy(out.data(), data, size);
    return out;
}

struct PostPush { f32 v[4]; };

// A fullscreen post pipeline: no vertex buffer, samples one texture at set 0.
rhi::PipelineHandle makeFullscreen(rhi::IGraphicsDevice& device,
                                   const unsigned char* frag, unsigned long fragSize,
                                   const char* fragWgsl,
                                   rhi::Format colorFormat, bool additive,
                                   const char* name) {
    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv        = toBytes(fullscreen_vert_spv, fullscreen_vert_spv_size);
    pd.fragmentSpirv      = toBytes(frag, fragSize);
    pd.vertexWgsl         = fullscreen_vert_spv_wgsl;
    pd.fragmentWgsl       = fragWgsl;
    pd.colorFormat        = colorFormat;
    pd.hasMaterialTexture = true;
    pd.additiveBlend      = additive;
    pd.pushConstantSize   = sizeof(PostPush);
    pd.debugName          = name;
    return device.createGraphicsPipeline(pd);
}

} // namespace

PostProcess::PostProcess(rhi::IGraphicsDevice& device, rhi::Format hdrFormat,
                         rhi::Format outputFormat)
    : m_device(device), m_hdrFormat(hdrFormat), m_outputFormat(outputFormat) {
    m_bright    = makeFullscreen(device, bright_frag_spv, bright_frag_spv_size,
                                 bright_frag_spv_wgsl,
                                 hdrFormat, false, "bloom_bright");
    m_blur      = makeFullscreen(device, blur_frag_spv, blur_frag_spv_size,
                                 blur_frag_spv_wgsl,
                                 hdrFormat, false, "bloom_blur");
    m_composite = makeFullscreen(device, composite_frag_spv, composite_frag_spv_size,
                                 composite_frag_spv_wgsl,
                                 hdrFormat, true, "bloom_composite");
    m_tonemap   = makeFullscreen(device, tonemap_frag_spv, tonemap_frag_spv_size,
                                 tonemap_frag_spv_wgsl,
                                 outputFormat, false, "tonemap");
    m_fxaa      = makeFullscreen(device, fxaa_frag_spv, fxaa_frag_spv_size,
                                 fxaa_frag_spv_wgsl,
                                 outputFormat, false, "fxaa");
}

PostProcess::~PostProcess() {
    m_device.waitIdle();
    m_device.destroyPipeline(m_fxaa);
    m_device.destroyPipeline(m_tonemap);
    m_device.destroyPipeline(m_composite);
    m_device.destroyPipeline(m_blur);
    m_device.destroyPipeline(m_bright);
}

void PostProcess::addPasses(RenderGraph& graph, RenderGraph::ResourceId sceneHdr,
                            RenderGraph::ResourceId output, u32 width, u32 height,
                            const Settings& s) {
    const f32 black[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    auto fullscreen = [](rhi::ICommandList& cmd, rhi::PipelineHandle pipe,
                         rhi::BindGroupHandle input, u32 w, u32 h, const PostPush& push) {
        cmd.setViewport({.x = 0.0f, .y = 0.0f,
                         .width = static_cast<f32>(w), .height = static_cast<f32>(h)});
        cmd.setScissor(0, 0, w, h);
        cmd.setPipeline(pipe);
        cmd.setBindGroup(0, input);
        cmd.pushConstants(&push, sizeof(PostPush));
        cmd.draw(3);
    };

    if (s.bloom) {
        VORTEX_PROFILE_ZONE("post.bloom");
        const u32 bw = width  > 1 ? width  / 2 : 1;
        const u32 bh = height > 1 ? height / 2 : 1;
        const auto bright = graph.colorTarget("bloom_bright", bw, bh, m_hdrFormat);
        const auto blurH  = graph.colorTarget("bloom_blurH",  bw, bh, m_hdrFormat);
        const auto blurV  = graph.colorTarget("bloom_blurV",  bw, bh, m_hdrFormat);

        graph.addPass("bloom_bright",
            [&](RenderGraph::PassBuilder& b) { b.sample(sceneHdr); b.writeColor(bright, black); },
            [this, &graph, fullscreen, sceneHdr, bw, bh, s](rhi::ICommandList& cmd) {
                fullscreen(cmd, m_bright, graph.sampledBindGroup(sceneHdr), bw, bh,
                           {{s.bloomThreshold, 0, 0, 0}});
            });

        graph.addPass("bloom_blur_h",
            [&](RenderGraph::PassBuilder& b) { b.sample(bright); b.writeColor(blurH, black); },
            [this, &graph, fullscreen, bright, bw, bh](rhi::ICommandList& cmd) {
                fullscreen(cmd, m_blur, graph.sampledBindGroup(bright), bw, bh,
                           {{1.0f / static_cast<f32>(bw), 0.0f, 0, 0}});
            });

        graph.addPass("bloom_blur_v",
            [&](RenderGraph::PassBuilder& b) { b.sample(blurH); b.writeColor(blurV, black); },
            [this, &graph, fullscreen, blurH, bw, bh](rhi::ICommandList& cmd) {
                fullscreen(cmd, m_blur, graph.sampledBindGroup(blurH), bw, bh,
                           {{0.0f, 1.0f / static_cast<f32>(bh), 0, 0}});
            });

        // Add the blurred bloom back onto the scene (additive blend, keep scene).
        graph.addPass("bloom_composite",
            [&](RenderGraph::PassBuilder& b) {
                b.sample(blurV);
                b.writeColor(sceneHdr, black, rhi::LoadOp::Load);
            },
            [this, &graph, fullscreen, blurV, width, height, s](rhi::ICommandList& cmd) {
                fullscreen(cmd, m_composite, graph.sampledBindGroup(blurV), width, height,
                           {{s.bloomIntensity, 0, 0, 0}});
            });
    }

    // Tone map into the backbuffer directly, unless FXAA needs an LDR source
    // texture first (the backbuffer is not sampleable by the graph).
    const auto ldrTarget = s.fxaa ? graph.colorTarget("post_ldr", width, height, m_outputFormat)
                                  : output;

    graph.addPass("tonemap",
        [&](RenderGraph::PassBuilder& b) { b.sample(sceneHdr); b.writeColor(ldrTarget, black); },
        [this, &graph, fullscreen, sceneHdr, ldrTarget, width, height, s](rhi::ICommandList& cmd) {
            fullscreen(cmd, m_tonemap, graph.sampledBindGroup(sceneHdr), width, height,
                       {{s.exposure, 0, 0, 0}});
        });

    if (s.fxaa) {
        graph.addPass("fxaa",
            [&](RenderGraph::PassBuilder& b) { b.sample(ldrTarget); b.writeColor(output, black); },
            [this, &graph, fullscreen, ldrTarget, width, height](rhi::ICommandList& cmd) {
                fullscreen(cmd, m_fxaa, graph.sampledBindGroup(ldrTarget), width, height,
                           {{1.0f / static_cast<f32>(width), 1.0f / static_cast<f32>(height), 0, 0}});
            });
    }
}

}
