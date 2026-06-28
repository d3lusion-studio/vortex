#include "vortex/renderer/render_graph.hpp"

#include "vortex/rhi/command_list.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_types.hpp"

namespace vortex::renderer {

RenderGraph::RenderGraph(rhi::IGraphicsDevice& device) : m_device(device) {
    m_sampler = m_device.createSampler({.minFilter = rhi::Filter::Linear,
                                        .magFilter = rhi::Filter::Linear,
                                        .addressU  = rhi::AddressMode::ClampToEdge,
                                        .addressV  = rhi::AddressMode::ClampToEdge});
    m_frame = rhi::kMaxFramesInFlight - 1;
}

RenderGraph::~RenderGraph() {
    m_device.waitIdle();
    for (PoolEntry& e : m_pool) destroyPoolEntry(e);
    if (m_sampler.valid()) m_device.destroySampler(m_sampler);
}

void RenderGraph::destroyPoolEntry(PoolEntry& e) {
    for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i) {
        if (e.bind[i].valid()) { m_device.destroyBindGroup(e.bind[i]); e.bind[i] = {}; }
        if (e.tex[i].valid())  { m_device.destroyTexture(e.tex[i]);    e.tex[i]  = {}; }
    }
}

void RenderGraph::beginFrame() {
    m_frame = (m_frame + 1) % rhi::kMaxFramesInFlight;
    m_resources.clear();
    m_passes.clear();
}

usize RenderGraph::acquirePool(const char* name, u32 width, u32 height,
                               rhi::Format format, bool isDepth) {
    usize index = m_pool.size();
    for (usize i = 0; i < m_pool.size(); ++i)
        if (m_pool[i].name == name) { index = i; break; }

    if (index == m_pool.size()) {
        m_pool.push_back({});
        m_pool[index].name = name;
    }

    PoolEntry& e = m_pool[index];
    const bool stale = e.width != width || e.height != height ||
                       e.format != format || e.isDepth != isDepth ||
                       !e.tex[0].valid();
    if (stale) {
        m_device.waitIdle();        // a target may still be in flight on resize
        destroyPoolEntry(e);
        e.width = width; e.height = height; e.format = format; e.isDepth = isDepth;

        rhi::TextureDesc desc{};
        desc.width  = width;
        desc.height = height;
        desc.format = format;
        desc.usage  = isDepth ? rhi::TextureUsage::DepthStencil
                              : (rhi::TextureUsage::Sampled | rhi::TextureUsage::RenderTarget);
        desc.debugName = e.name.c_str();
        for (u32 i = 0; i < rhi::kMaxFramesInFlight; ++i)
            e.tex[i] = m_device.createTexture(desc, nullptr);
    }
    return index;
}

RenderGraph::ResourceId RenderGraph::addTransient(usize poolIndex, u32 width, u32 height) {
    Resource r{};
    r.poolIndex = static_cast<i32>(poolIndex);
    r.width     = width;
    r.height    = height;
    r.state     = rhi::ResourceState::Undefined;
    m_resources.push_back(r);
    return static_cast<ResourceId>(m_resources.size() - 1);
}

RenderGraph::ResourceId RenderGraph::importBackbuffer(rhi::TextureHandle h, u32 width, u32 height) {
    Resource r{};
    r.poolIndex = -1;
    r.imported  = h;
    r.width     = width;
    r.height    = height;
    r.state     = rhi::ResourceState::Undefined;
    m_resources.push_back(r);
    return static_cast<ResourceId>(m_resources.size() - 1);
}

RenderGraph::ResourceId RenderGraph::colorTarget(const char* name, u32 width, u32 height,
                                                 rhi::Format format) {
    const usize idx = acquirePool(name, width, height, format, /*isDepth=*/false);
    return addTransient(idx, width, height);
}

RenderGraph::ResourceId RenderGraph::depthTarget(const char* name, u32 width, u32 height) {
    const usize idx = acquirePool(name, width, height, rhi::Format::D32_SFLOAT, /*isDepth=*/true);
    return addTransient(idx, width, height);
}

rhi::TextureHandle RenderGraph::texture(ResourceId id) const {
    if (id >= m_resources.size()) return {};
    const Resource& r = m_resources[id];
    if (r.poolIndex < 0) return r.imported;
    return m_pool[static_cast<usize>(r.poolIndex)].tex[m_frame];
}

rhi::BindGroupHandle RenderGraph::sampledBindGroup(ResourceId id) {
    if (id >= m_resources.size()) return {};
    const Resource& r = m_resources[id];
    if (r.poolIndex < 0) return {};   // imported targets aren't sampled by the graph
    PoolEntry& e = m_pool[static_cast<usize>(r.poolIndex)];
    if (!e.bind[m_frame].valid())
        e.bind[m_frame] = m_device.createBindGroup({.texture = e.tex[m_frame], .sampler = m_sampler});
    return e.bind[m_frame];
}

void RenderGraph::PassBuilder::sample(ResourceId id) { m_pass.samples.push_back(id); }

void RenderGraph::PassBuilder::writeColor(ResourceId id, const f32 clearColor[4],
                                          rhi::LoadOp loadOp) {
    m_pass.colorWrite = id;
    m_pass.colorLoadOp = loadOp;
    for (int i = 0; i < 4; ++i) m_pass.clearColor[i] = clearColor[i];
}

void RenderGraph::PassBuilder::writeDepth(ResourceId id, f32 clearDepth, rhi::LoadOp loadOp) {
    m_pass.depthWrite  = id;
    m_pass.clearDepth  = clearDepth;
    m_pass.depthLoadOp = loadOp;
}

void RenderGraph::addPass(const char* name, const std::function<void(PassBuilder&)>& setup,
                          ExecuteFn execute) {
    m_passes.push_back({});
    Pass& p   = m_passes.back();
    p.name    = name;
    p.execute = std::move(execute);
    PassBuilder builder(p);
    if (setup) setup(builder);
}

void RenderGraph::execute(rhi::ICommandList& cmd) {
    for (Pass& pass : m_passes) {
        // Make sampled inputs shader-readable (auto-barrier from their last state).
        for (ResourceId s : pass.samples) {
            if (s >= m_resources.size()) continue;
            Resource& res = m_resources[s];
            if (res.state != rhi::ResourceState::ShaderRead) {
                cmd.transition(texture(s), rhi::ResourceState::ShaderRead);
                res.state = rhi::ResourceState::ShaderRead;
            }
        }

        if (pass.colorWrite == kInvalid || pass.colorWrite >= m_resources.size()) continue;
        const Resource& cw = m_resources[pass.colorWrite];

        rhi::RenderPassDesc pd{};
        pd.color.target = texture(pass.colorWrite);
        pd.color.loadOp = pass.colorLoadOp;
        for (int i = 0; i < 4; ++i) pd.color.clearColor[i] = pass.clearColor[i];
        pd.width  = cw.width;
        pd.height = cw.height;

        if (pass.depthWrite != kInvalid && pass.depthWrite < m_resources.size()) {
            pd.depth.target     = texture(pass.depthWrite);
            pd.depth.loadOp     = pass.depthLoadOp;
            pd.depth.clearDepth = pass.clearDepth;
        }

        cmd.beginRenderPass(pd);
        if (pass.execute) pass.execute(cmd);
        cmd.endRenderPass();

        // Record the post-pass states so a later sample triggers a barrier.
        m_resources[pass.colorWrite].state = rhi::ResourceState::RenderTarget;
        if (pass.depthWrite != kInvalid && pass.depthWrite < m_resources.size())
            m_resources[pass.depthWrite].state = rhi::ResourceState::DepthTarget;
    }
}

}
