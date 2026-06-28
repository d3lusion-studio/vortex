#pragma once
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <functional>
#include <string>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; class ICommandList; }

namespace vortex::renderer {

class RenderGraph {
public:
    using ResourceId = u32;
    static constexpr ResourceId kInvalid = 0xFFFFFFFFu;

    using ExecuteFn = std::function<void(rhi::ICommandList&)>;

    // One graph node: what it samples, what it writes, and how to record it.
    struct Pass {
        std::string             name;
        std::vector<ResourceId> samples;
        ResourceId  colorWrite    = kInvalid;
        f32         clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        rhi::LoadOp colorLoadOp   = rhi::LoadOp::Clear;
        ResourceId  depthWrite    = kInvalid;
        f32         clearDepth    = 1.0f;
        rhi::LoadOp depthLoadOp   = rhi::LoadOp::Clear;
        ExecuteFn   execute;
    };

    explicit RenderGraph(rhi::IGraphicsDevice& device);
    ~RenderGraph();
    RenderGraph(const RenderGraph&)            = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    // Start a new frame: clears the pass list and advances the frame-in-flight
    // index. Call once per acquired (valid) device frame.
    void beginFrame();

    // Import the swapchain backbuffer; the graph reads/writes but does not own it.
    [[nodiscard]] ResourceId importBackbuffer(rhi::TextureHandle, u32 width, u32 height);

    // Declare (or reuse) a transient colour target. Recreated on size/format change.
    [[nodiscard]] ResourceId colorTarget(const char* name, u32 width, u32 height,
                                         rhi::Format format);
    // Declare (or reuse) a transient depth target (D32_SFLOAT).
    [[nodiscard]] ResourceId depthTarget(const char* name, u32 width, u32 height);

    // Records what a pass reads/writes. Issued inside addPass's setup callback.
    class PassBuilder {
    public:
        void sample(ResourceId);                                  // shader-read input
        void writeColor(ResourceId, const f32 clearColor[4],
                        rhi::LoadOp loadOp = rhi::LoadOp::Clear);
        void writeDepth(ResourceId, f32 clearDepth = 1.0f,
                        rhi::LoadOp loadOp = rhi::LoadOp::Clear);
    private:
        friend class RenderGraph;
        explicit PassBuilder(Pass& p) : m_pass(p) {}
        Pass& m_pass;
    };

    void addPass(const char* name, const std::function<void(PassBuilder&)>& setup,
                 ExecuteFn execute);

    // Order passes, insert barriers, and record them into cmd.
    void execute(rhi::ICommandList& cmd);

    // GPU handles for a resource — valid for the current frame (use in execute fns).
    [[nodiscard]] rhi::TextureHandle    texture(ResourceId) const;
    [[nodiscard]] rhi::BindGroupHandle  sampledBindGroup(ResourceId);

private:
    // A persistent transient target: one texture per frame-in-flight, plus a
    // cached sampling bind group each. Survives across frames; only rebuilt when
    // its size/format changes.
    struct PoolEntry {
        std::string         name;
        bool                isDepth = false;
        rhi::Format         format  = rhi::Format::Undefined;
        u32                 width = 0, height = 0;
        rhi::TextureHandle   tex[rhi::kMaxFramesInFlight]{};
        rhi::BindGroupHandle bind[rhi::kMaxFramesInFlight]{};
    };

    // Per-frame resource record (imported handle, or a reference into the pool).
    struct Resource {
        i32                poolIndex = -1;        // -1 => imported
        rhi::TextureHandle imported;
        u32                width = 0, height = 0;
        rhi::ResourceState state = rhi::ResourceState::Undefined;
    };

    // Find or (re)create the persistent pool entry for a transient target,
    // returning its index in m_pool.
    usize acquirePool(const char* name, u32 width, u32 height,
                      rhi::Format format, bool isDepth);
    ResourceId addTransient(usize poolIndex, u32 width, u32 height);
    void destroyPoolEntry(PoolEntry&);

    rhi::IGraphicsDevice& m_device;
    rhi::SamplerHandle    m_sampler;
    u32                   m_frame = 0;

    std::vector<PoolEntry> m_pool;        // persistent across frames
    std::vector<Resource>  m_resources;   // per-frame
    std::vector<Pass>      m_passes;      // per-frame
};

}
