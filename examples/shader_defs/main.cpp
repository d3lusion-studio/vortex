// Shader defs + specialized pipelines, through the RHI. One fragment source
// (effect.frag) is compiled at BUILD time into four SPIR-V variants — the WAVE and
// TINT feature blocks each on or off — and a renderer::PipelineCache specializes a
// pipeline for whichever variant is asked for, building each exactly once.
//
// This is the engine's answer to Bevy's "Shader Defs" + "Specialized Mesh Pipeline":
// the def flags are fixed at build time (the architecture compiles GLSL ahead of
// time, not in-process), and the runtime selects and caches the variant it needs.
//
// Controls: 1..4 pick a variant; Space cycles. Esc quits. It also auto-cycles.
//
// Headless self-check: VORTEX_SHADERDEFS_CHECK=1 exercises the cache — every variant
// built once, re-requests served from cache, all handles valid and distinct — and
// exits non-zero if any of that fails (needs a GPU, like any RHI example).

#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/pipeline_cache.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace vortex;

namespace {

// The def flags packed into the pipeline-cache key. Two flags -> four variants.
enum DefBits : u64 { kWave = 1u << 0, kTint = 1u << 1 };
constexpr u32 kVariantCount = 4;

const char* variantName(u64 mask) {
    switch (mask) {
    case 0:                 return "plain";
    case kWave:             return "wave";
    case kTint:             return "tint";
    case kWave | kTint:     return "wave+tint";
    default:                return "?";
    }
}

std::vector<std::byte> loadSpirv(pf::IFileSystem& fs, const std::string& path) {
    auto bytes = fs.readFile(path.c_str());
    if (bytes.empty()) VORTEX_ERROR("ShaderDefs", "Failed to load shader: %s", path.c_str());
    return bytes;
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Shader Defs"});
    auto input  = pf::createInputProvider(*window);
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    // The vertex stage is shared; the fragment stage is the specialized part. Load
    // the full-screen vertex SPIR-V and every fragment variant, indexed by def mask.
    const std::string dir = VORTEX_SHADERDEFS_DIR;
    const std::vector<std::byte> vertSpirv = loadSpirv(*fs, dir + "/fullscreen.vert.spv");
    std::array<std::vector<std::byte>, kVariantCount> fragSpirv;
    for (u64 mask = 0; mask < kVariantCount; ++mask)
        fragSpirv[mask] = loadSpirv(*fs, dir + "/effect." + std::to_string(mask) + ".spv");

    renderer::PipelineCache pipelines(*device);

    // Builds the pipeline for a def mask. The cache calls this only on a miss.
    const rhi::Format colorFormat = swapchain->format();
    auto buildVariant = [&](rhi::IGraphicsDevice& dev, u64 key) -> rhi::PipelineHandle {
        rhi::GraphicsPipelineDesc pd;
        pd.vertexSpirv   = vertSpirv;                 // no vertex buffer: full-screen triangle
        pd.fragmentSpirv = fragSpirv[key];
        pd.colorFormat   = colorFormat;
        pd.debugName     = "shader_defs_variant";
        VORTEX_INFO("ShaderDefs", "  building pipeline for variant '%s' (mask %llu)",
                    variantName(key), static_cast<unsigned long long>(key));
        return dev.createGraphicsPipeline(pd);
    };

    // -------------------------------------------------------------- headless check
    if (std::getenv("VORTEX_SHADERDEFS_CHECK")) {
        std::array<rhi::PipelineHandle, kVariantCount> handles;
        for (u64 mask = 0; mask < kVariantCount; ++mask)
            handles[mask] = pipelines.get(mask, buildVariant);          // build each

        for (u64 mask = 0; mask < kVariantCount; ++mask)
            (void)pipelines.get(mask, buildVariant);                    // re-request: cache hits

        bool ok = pipelines.size() == kVariantCount &&
                  pipelines.misses() == kVariantCount &&                // each built exactly once
                  pipelines.hits() == kVariantCount;                    // each reused exactly once
        for (u64 i = 0; i < kVariantCount && ok; ++i) {
            if (!handles[i].valid()) ok = false;
            for (u64 j = i + 1; j < kVariantCount; ++j)
                if (handles[i] == handles[j]) ok = false;               // distinct pipelines
        }
        std::printf("\n[%s] Shader-defs self-check: %zu variants built, %llu misses, %llu hits\n",
                    ok ? "PASS" : "FAIL", pipelines.size(),
                    static_cast<unsigned long long>(pipelines.misses()),
                    static_cast<unsigned long long>(pipelines.hits()));
        device->waitIdle();
        pipelines.clear();
        swapchain.reset();
        return ok ? 0 : 1;
    }

    // -------------------------------------------------------------- interactive
    VORTEX_INFO("ShaderDefs", "Keys 1-4 pick a variant, Space cycles, Esc quits.");
    u64 mask = 0;
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;

    while (!window->shouldClose()) {
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        if (input->isKeyPressed(pf::Key::Num1)) mask = 0;
        if (input->isKeyPressed(pf::Key::Num2)) mask = kWave;
        if (input->isKeyPressed(pf::Key::Num3)) mask = kTint;
        if (input->isKeyPressed(pf::Key::Num4)) mask = kWave | kTint;
        if (input->isKeyPressed(pf::Key::Space)) mask = (mask + 1) % kVariantCount;
        if (frameCount % 120 == 119) mask = (mask + 1) % kVariantCount;   // auto-cycle

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x0D0D14));
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setPipeline(pipelines.get(mask, buildVariant));   // specialized on demand
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);
        frame.cmd->draw(3);   // full-screen triangle, no vertex buffer
        frame.cmd->endRenderPass();

        device->endFrame();
        ++frameCount;
    }

    VORTEX_INFO("ShaderDefs", "Built %zu pipeline variants (%llu cache hits). Goodbye.",
                pipelines.size(), static_cast<unsigned long long>(pipelines.hits()));
    device->waitIdle();
    pipelines.clear();
    swapchain.reset();
    return 0;
}
