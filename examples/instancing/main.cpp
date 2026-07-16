// Instancing through the RHI: a whole grid of quads drawn in ONE draw call. The
// quad corners are generated in the shader; the per-instance offset and colour come
// from a single per-instance vertex buffer. draw(4, N) issues N instances at once —
// there is no per-quad CPU work and no per-quad draw.
//
// Headless self-check: VORTEX_INSTANCING_CHECK=1 gives every instance a distinct
// colour, renders the grid into an offscreen target, reads it back, and counts the
// distinct non-background colours. With no MSAA the edges are hard, so exactly N
// colours must appear — proof all N instances came out of the one draw call. Exits
// non-zero otherwise (needs a GPU).

#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>

using namespace vortex;

namespace {

struct Instance {
    f32 ox, oy;         // clip-space centre
    f32 r, g, b, a;     // colour
};

constexpr u32 kCols = 5;
constexpr u32 kRows = 5;
constexpr u32 kCount = kCols * kRows;

// A grid of instances, each a distinct colour so readback can tell them apart.
std::vector<Instance> makeGrid() {
    std::vector<Instance> out;
    out.reserve(kCount);
    for (u32 row = 0; row < kRows; ++row)
        for (u32 col = 0; col < kCols; ++col) {
            const f32 ox = -0.72f + static_cast<f32>(col) * 0.36f;
            const f32 oy = -0.72f + static_cast<f32>(row) * 0.36f;
            // Distinct, quantisation-safe colours: 8/255 steps per axis.
            const f32 r = (40.0f + 8.0f * col) / 255.0f;
            const f32 g = (40.0f + 8.0f * row) / 255.0f;
            out.push_back({ox, oy, r, g, 0.75f, 1.0f});
        }
    return out;
}

std::vector<std::byte> loadSpirv(pf::IFileSystem& fs, const char* path) {
    auto bytes = fs.readFile(path);
    if (bytes.empty()) VORTEX_ERROR("Instancing", "Failed to load shader: %s", path);
    return bytes;
}

void drawGrid(rhi::ICommandList& cmd, rhi::TextureHandle target, u32 w, u32 h,
              rhi::PipelineHandle pipeline, rhi::BufferHandle instances) {
    rhi::RenderPassDesc pass;
    pass.color.target = target;
    pass.color.loadOp = rhi::LoadOp::Clear;
    pass.color.setClear(Color::fromRgb(0x0D0D14));
    pass.width  = w;
    pass.height = h;

    cmd.beginRenderPass(pass);
    cmd.setPipeline(pipeline);
    cmd.setViewport({.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(w), .height = static_cast<f32>(h)});
    cmd.setScissor(0, 0, w, h);
    cmd.setVertexBuffer(0, instances);
    cmd.draw(4, kCount);   // 4 corners, kCount instances — one draw call
    cmd.endRenderPass();
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex Instancing"});
    auto input  = pf::createInputProvider(*window);
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    const std::vector<Instance> grid = makeGrid();
    rhi::BufferHandle instanceBuffer = device->createBuffer(
        {.size = grid.size() * sizeof(Instance), .usage = rhi::BufferUsage::Vertex,
         .domain = rhi::MemoryDomain::Upload, .debugName = "instances"},
        grid.data());

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv        = loadSpirv(*fs, VORTEX_INSTANCING_SHADER_DIR "/instancing.vert.spv");
    pd.fragmentSpirv      = loadSpirv(*fs, VORTEX_INSTANCING_SHADER_DIR "/instancing.frag.spv");
    pd.vertexLayout.stride = sizeof(Instance);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float2, .offset = offsetof(Instance, ox)},
        {.location = 1, .format = rhi::VertexFormat::Float4, .offset = offsetof(Instance, r)},
    };
    pd.vertexLayout.perInstance = true;   // advance the buffer once per instance
    pd.topology     = rhi::PrimitiveTopology::TriangleStrip;
    pd.colorFormat  = swapchain->format();
    pd.debugName    = "instancing_pipeline";
    rhi::PipelineHandle pipeline = device->createGraphicsPipeline(pd);

    // -------------------------------------------------------------- headless check
    if (std::getenv("VORTEX_INSTANCING_CHECK")) {
        constexpr u32 kSize = 160;
        rhi::TextureHandle offscreen = device->createTexture(
            {.width = kSize, .height = kSize, .format = swapchain->format(),
             .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
             .debugName = "instancing_offscreen"});

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        bool pass = false;
        if (frame.valid) {
            drawGrid(*frame.cmd, offscreen, kSize, kSize, pipeline, instanceBuffer);
            rhi::RenderPassDesc bb;   // keep the backbuffer defined for present
            bb.color.target = frame.backbuffer;
            bb.color.loadOp = rhi::LoadOp::Clear;
            bb.color.setClear(Color::fromRgb(0x000000));
            bb.width = frame.width; bb.height = frame.height;
            frame.cmd->beginRenderPass(bb);
            frame.cmd->endRenderPass();
            device->endFrame();

            std::vector<u8> pixels(static_cast<usize>(kSize) * kSize * 4);
            device->readTexture(offscreen, pixels.data());

            // The clear colour, as it appears in the readback, is the first pixel's
            // value (a corner the grid never covers). Every other distinct value is
            // an instance colour.
            const u32 clearKey = *reinterpret_cast<const u32*>(&pixels[0]);
            std::set<u32> colors;
            for (usize i = 0; i < pixels.size(); i += 4) {
                const u32 key = *reinterpret_cast<const u32*>(&pixels[i]);
                if (key != clearKey) colors.insert(key);
            }
            pass = colors.size() == kCount;
            std::printf("\n[%s] Instancing self-check: %zu distinct instance colours (expected %u), one draw call\n",
                        pass ? "PASS" : "FAIL", colors.size(), kCount);
        }

        device->waitIdle();
        device->destroyTexture(offscreen);
        device->destroyPipeline(pipeline);
        device->destroyBuffer(instanceBuffer);
        swapchain.reset();
        return pass ? 0 : 1;
    }

    // -------------------------------------------------------------- interactive
    VORTEX_INFO("Instancing", "%u instances in one draw call. ESC to quit.", kCount);
    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;

    while (!window->shouldClose()) {
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastW || h != lastH) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastW = w; lastH = h;
        }

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;
        drawGrid(*frame.cmd, frame.backbuffer, frame.width, frame.height, pipeline, instanceBuffer);
        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    device->destroyPipeline(pipeline);
    device->destroyBuffer(instanceBuffer);
    swapchain.reset();
    VORTEX_INFO("Instancing", "Goodbye.");
    return 0;
}
