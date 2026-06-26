#include "vortex/core/log.hpp"
#include "vortex/platform/filesystem.hpp"
#include "vortex/platform/input.hpp"
#include "vortex/platform/window.hpp"
#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/swapchain.hpp"

#include <cstddef>
#include <cstdlib>

using namespace vortex;

namespace {

struct Vertex {
    f32 x, y;        // clip-space position
    f32 r, g, b;     // vertex colour
};

std::vector<std::byte> loadSpirv(pf::IFileSystem& fs, const char* path) {
    auto bytes = fs.readFile(path);
    if (bytes.empty())
        VORTEX_ERROR("App", "Failed to load shader: %s", path);
    return bytes;
}

}

int main() {
    auto window = pf::createWindow({.width = 1280, .height = 720,
                                    .title = "Vortex Triangle"});
    auto input  = pf::createInputProvider(*window);
    auto fs     = pf::createFileSystem();

    auto device = rhi::createDevice(rhi::GraphicsAPI::Vulkan, *window);

    int fbWidth = 0, fbHeight = 0;
    window->getFramebufferSize(fbWidth, fbHeight);
    auto swapchain = device->createSwapchain(
        {.width = static_cast<u32>(fbWidth), .height = static_cast<u32>(fbHeight),
         .present = rhi::PresentMode::Fifo}, *window);

    const Vertex vertices[] = {
        { 0.0f, -0.5f, 1.0f, 0.0f, 0.0f},
        { 0.5f,  0.5f, 0.0f, 1.0f, 0.0f},
        {-0.5f,  0.5f, 0.0f, 0.0f, 1.0f},
    };
    rhi::BufferHandle vbo = device->createBuffer(
        {.size = sizeof(vertices), .usage = rhi::BufferUsage::Vertex,
         .domain = rhi::MemoryDomain::Upload, .debugName = "triangle_vbo"},
        vertices);

    rhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.vertexSpirv         = loadSpirv(*fs, VORTEX_TRIANGLE_SHADER_DIR "/triangle.vert.spv");
    pipeDesc.fragmentSpirv       = loadSpirv(*fs, VORTEX_TRIANGLE_SHADER_DIR "/triangle.frag.spv");
    pipeDesc.vertexLayout.stride = sizeof(Vertex);
    pipeDesc.vertexLayout.attributes = {
        {.location = 0, .format = rhi::VertexFormat::Float2, .offset = offsetof(Vertex, x)},
        {.location = 1, .format = rhi::VertexFormat::Float3, .offset = offsetof(Vertex, r)},
    };
    pipeDesc.colorFormat = swapchain->format();
    pipeDesc.debugName   = "triangle_pipeline";
    rhi::PipelineHandle pipeline = device->createGraphicsPipeline(pipeDesc);

    VORTEX_INFO("App", "Rendering a triangle through the RHI. ESC to quit.");

    int lastWidth = fbWidth, lastHeight = fbHeight;

    const char* maxFramesEnv = std::getenv("VORTEX_MAX_FRAMES");
    const u64 maxFrames = maxFramesEnv ? std::strtoull(maxFramesEnv, nullptr, 10) : 0;
    u64 frameCount = 0;

    while (!window->shouldClose()) {
        input->newFrame();
        window->pollEvents();
        if (input->isKeyPressed(pf::Key::Escape)) break;
        if (maxFrames != 0 && frameCount >= maxFrames) break;

        int w = 0, h = 0;
        window->getFramebufferSize(w, h);
        if (w != lastWidth || h != lastHeight) {
            swapchain->requestResize(static_cast<u32>(w), static_cast<u32>(h));
            lastWidth  = w;
            lastHeight = h;
        }

        rhi::FrameContext frame = device->beginFrame(*swapchain);
        if (!frame.valid) continue;

        rhi::RenderPassDesc pass;
        pass.color.target     = frame.backbuffer;
        pass.color.loadOp     = rhi::LoadOp::Clear;
        pass.color.clearColor[0] = 0.05f;
        pass.color.clearColor[1] = 0.05f;
        pass.color.clearColor[2] = 0.08f;
        pass.color.clearColor[3] = 1.0f;
        pass.width  = frame.width;
        pass.height = frame.height;

        frame.cmd->beginRenderPass(pass);
        frame.cmd->setPipeline(pipeline);
        frame.cmd->setViewport({.x = 0.0f, .y = 0.0f,
                                .width = static_cast<f32>(frame.width),
                                .height = static_cast<f32>(frame.height)});
        frame.cmd->setScissor(0, 0, frame.width, frame.height);
        frame.cmd->setVertexBuffer(0, vbo);
        frame.cmd->draw(3);
        frame.cmd->endRenderPass();

        device->endFrame();
        ++frameCount;
    }

    device->waitIdle();
    device->destroyPipeline(pipeline);
    device->destroyBuffer(vbo);
    swapchain.reset();

    VORTEX_INFO("App", "Goodbye.");
    return 0;
}
