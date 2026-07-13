#include "vortex/core/log.hpp"
#include "vortex/core/memory/object_pool.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/jobs/job_system.hpp"
#include "vortex/platform/clock.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/renderer/camera2d.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <string_view>
#include <vector>

using namespace vortex;

namespace {

struct Vertex {
    Vec2 pos;
    Vec4 color;
};

std::vector<std::byte> loadSpirv(pf::IFileSystem& fs, const char* path) {
    auto bytes = fs.readFile(path);
    if (bytes.empty()) VORTEX_ERROR("App", "Failed to load shader: %s", path);
    return bytes;
}

// Quick standalone proof that ObjectPool recycles slots and invalidates handles.
void objectPoolSelfTest() {
    ObjectPool<int> pool;
    const auto a = pool.create(10);
    const auto b = pool.create(20);
    pool.destroy(a);
    const bool staleGone = pool.get(a) == nullptr;          // a was invalidated
    const auto c = pool.create(30);                          // reuses a's slot
    const bool recycled  = c.index == a.index && c.generation != a.generation;
    const bool bIntact   = pool.get(b) && *pool.get(b) == 20;
    VORTEX_INFO("Pool", "ObjectPool self-test: stale=%d recycled=%d intact=%d alive=%zu",
                staleGone, recycled, bIntact, pool.aliveCount());
}

}

int main() {
    objectPoolSelfTest();

    const char* quadsEnv = std::getenv("VORTEX_QUADS");
    const u32 quadCount = quadsEnv ? static_cast<u32>(std::strtoul(quadsEnv, nullptr, 10)) : 20000u;

    auto window = pf::createWindow({.width = 1280, .height = 720, .title = "Vortex MT Record"});
    auto input  = pf::createInputProvider(*window);
    auto clock  = pf::createClock();
    auto fs     = pf::createFileSystem();
    auto device = rhi::createDevice(*window);

    int fbw = 0, fbh = 0;
    window->getFramebufferSize(fbw, fbh);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbw), .height = static_cast<u32>(fbh)}, *window);

    jobs::JobSystem jobs;
    const u32 groups = std::min(jobs.workerCount() + 1, 32u);

    // Build a grid of coloured quads into one static vertex + index buffer.
    const u32 cols = static_cast<u32>(std::ceil(std::sqrt(static_cast<f32>(quadCount))));
    const f32 spacing = 18.0f, half = 7.0f;
    std::vector<Vertex> verts(static_cast<usize>(quadCount) * 4);
    std::vector<u32>    indices(static_cast<usize>(quadCount) * 6);
    for (u32 q = 0; q < quadCount; ++q) {
        const u32 cx = q % cols, cy = q / cols;
        const Vec2 c{(static_cast<f32>(cx) - cols * 0.5f) * spacing,
                     (static_cast<f32>(cy) - cols * 0.5f) * spacing};
        const Vec4 col{0.3f + 0.7f * static_cast<f32>(cx % 7) / 7.0f,
                       0.3f + 0.7f * static_cast<f32>(cy % 5) / 5.0f,
                       0.5f + 0.5f * static_cast<f32>((cx + cy) % 3) / 3.0f, 1.0f};
        Vertex* v = &verts[static_cast<usize>(q) * 4];
        v[0] = {{c.x - half, c.y + half}, col};
        v[1] = {{c.x + half, c.y + half}, col};
        v[2] = {{c.x + half, c.y - half}, col};
        v[3] = {{c.x - half, c.y - half}, col};
        u32* idx = &indices[static_cast<usize>(q) * 6];
        const u32 base = q * 4;
        idx[0] = base; idx[1] = base + 1; idx[2] = base + 2;
        idx[3] = base + 2; idx[4] = base + 3; idx[5] = base;
    }
    rhi::BufferHandle vbo = device->createBuffer(
        {.size = verts.size() * sizeof(Vertex), .usage = rhi::BufferUsage::Vertex,
         .domain = rhi::MemoryDomain::Device, .debugName = "mt_vbo"}, verts.data());
    rhi::BufferHandle ibo = device->createBuffer(
        {.size = indices.size() * sizeof(u32), .usage = rhi::BufferUsage::Index,
         .domain = rhi::MemoryDomain::Device, .debugName = "mt_ibo"}, indices.data());

    rhi::GraphicsPipelineDesc pd;
    pd.vertexSpirv         = loadSpirv(*fs, VORTEX_MT_SHADER_DIR "/mt.vert.spv");
    pd.fragmentSpirv       = loadSpirv(*fs, VORTEX_MT_SHADER_DIR "/mt.frag.spv");
    pd.vertexLayout.stride = sizeof(Vertex);
    pd.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex, pos)},
        {.location = 1, .format = rhi::VertexFormat::Float4, .offset = offsetof(Vertex, color)},
    };
    pd.colorFormat      = swapchain->format();
    pd.pushConstantSize = sizeof(Mat4);
    pd.debugName        = "mt_pipeline";
    rhi::PipelineHandle pipeline = device->createGraphicsPipeline(pd);

    renderer::Camera2D camera;
    camera.viewportWidth  = static_cast<f32>(fbw);
    camera.viewportHeight = static_cast<f32>(fbh);
    camera.zoom = std::min(fbw, fbh) / (cols * spacing + 80.0f);

    VORTEX_INFO("App", "%u quads, %u draws/frame, %u record groups. ESC to quit.",
                quadCount, quadCount, groups);

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;
    int lastW = fbw, lastH = fbh;

    // Per-mode running averages of CPU record time + GPU time.
    f64 cpuSingle = 0.0, cpuMt = 0.0, gpuAccum = 0.0;
    u32 nSingle = 0, nMt = 0;
    std::vector<rhi::ICommandList*> secs(groups);

    while (!window->shouldClose()) {
        clock->tick();
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
        camera.viewportWidth  = static_cast<f32>(w);
        camera.viewportHeight = static_cast<f32>(h);
        const Mat4 viewProj = camera.viewProjection();
        const bool mt = (frameCount & 1) != 0;   // alternate modes for a fair comparison

        profiler::beginFrame();
        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) { profiler::endFrame(); continue; }

        const rhi::Viewport view{.x = 0, .y = 0, .width = static_cast<f32>(frame.width),
                                 .height = static_cast<f32>(frame.height)};

        rhi::RenderPassDesc pass;
        pass.color.target = frame.backbuffer;
        pass.color.loadOp = rhi::LoadOp::Clear;
        pass.color.setClear(Color::fromRgb(0x0A0A12));
        pass.width = frame.width; pass.height = frame.height;
        pass.secondaryContents = mt;

        frame.cmd->beginRenderPass(pass);

        if (mt) {
            VORTEX_PROFILE_ZONE("record.mt");
            const u32 per = (quadCount + groups - 1) / groups;
            jobs.parallelFor(groups, [&](usize g) {
                rhi::ICommandList* sec = device->acquireSecondaryCommandList();
                sec->setPipeline(pipeline);
                sec->setViewport(view);
                sec->setScissor(0, 0, frame.width, frame.height);
                sec->setVertexBuffer(0, vbo);
                sec->setIndexBuffer(ibo, rhi::IndexType::U32);
                sec->pushConstants(&viewProj, sizeof(Mat4));
                const u32 begin = static_cast<u32>(g) * per;
                const u32 end   = std::min(begin + per, quadCount);
                for (u32 q = begin; q < end; ++q) sec->drawIndexed(6, 1, q * 6, 0, 0);
                secs[g] = sec;
            }, 1);
            device->executeSecondary(*frame.cmd, secs.data(), groups);
        } else {
            VORTEX_PROFILE_ZONE("record.single");
            frame.cmd->setPipeline(pipeline);
            frame.cmd->setViewport(view);
            frame.cmd->setScissor(0, 0, frame.width, frame.height);
            frame.cmd->setVertexBuffer(0, vbo);
            frame.cmd->setIndexBuffer(ibo, rhi::IndexType::U32);
            frame.cmd->pushConstants(&viewProj, sizeof(Mat4));
            for (u32 q = 0; q < quadCount; ++q) frame.cmd->drawIndexed(6, 1, q * 6, 0, 0);
        }

        frame.cmd->endRenderPass();
        device->endFrame();
        profiler::endFrame();
        VORTEX_PROFILE_FRAME_MARK();

        for (const profiler::Entry& e : profiler::lastFrame()) {
            if (std::string_view(e.name) == "record.single") { cpuSingle += e.ms; ++nSingle; }
            if (std::string_view(e.name) == "record.mt")     { cpuMt     += e.ms; ++nMt; }
        }
        gpuAccum += device->gpuFrameTimeMs();

        if (++frameCount % 120 == 0) {
            const f64 s = nSingle ? cpuSingle / nSingle : 0.0;
            const f64 m = nMt ? cpuMt / nMt : 0.0;
            VORTEX_INFO("MT", "record CPU: single %.2f ms | mt %.2f ms (%.1fx) | GPU %.2f ms",
                        s, m, m > 0.0 ? s / m : 0.0, gpuAccum / 120.0);
            cpuSingle = cpuMt = gpuAccum = 0.0; nSingle = nMt = 0;
        }
    }

    device->waitIdle();
    device->destroyPipeline(pipeline);
    device->destroyBuffer(ibo);
    device->destroyBuffer(vbo);
    swapchain.reset();
    VORTEX_INFO("App", "Done after %llu frames.", static_cast<unsigned long long>(frameCount));
    return 0;
}
