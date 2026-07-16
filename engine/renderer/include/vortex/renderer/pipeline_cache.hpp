#pragma once
#include "vortex/core/types.hpp"
#include "vortex/rhi/device.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <functional>
#include <unordered_map>

namespace vortex::renderer {

// Caches specialized graphics pipelines by an opaque u64 key. The first request for
// a key builds the pipeline (through the caller's builder) and stores it; every later
// request for the same key returns the stored handle.
//
// This is the "specialized pipeline" pattern. A pipeline is not one object but a
// family: the same shaders vary by blend mode, by mesh vertex layout, by which
// shader-def features are switched on. Building every combination up front is
// wasteful (most are never used) and building one per draw is ruinous. So you fold
// the choices that matter into a key and let the cache build each distinct variant
// exactly once, the frame it is first needed.
//
// The key is yours to define — pack shader-def flags, a topology, a colour format,
// whatever specializes the pipeline — as long as equal keys mean interchangeable
// pipelines. The cache owns every pipeline it builds and destroys them at clear()
// or destruction.
class PipelineCache {
public:
    // Builds the pipeline for a key that missed. Handed the device and the key so a
    // single builder can switch on the key to pick shaders and state.
    using Builder = std::function<rhi::PipelineHandle(rhi::IGraphicsDevice&, u64 key)>;

    explicit PipelineCache(rhi::IGraphicsDevice& device) : m_device(device) {}
    ~PipelineCache() { clear(); }
    PipelineCache(const PipelineCache&)            = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;

    [[nodiscard]] rhi::PipelineHandle get(u64 key, const Builder& build) {
        if (auto it = m_cache.find(key); it != m_cache.end()) {
            ++m_hits;
            return it->second;
        }
        ++m_misses;
        const rhi::PipelineHandle handle = build(m_device, key);
        m_cache.emplace(key, handle);
        return handle;
    }

    [[nodiscard]] bool  contains(u64 key) const { return m_cache.find(key) != m_cache.end(); }
    [[nodiscard]] usize size()   const { return m_cache.size(); }
    [[nodiscard]] u64   hits()   const { return m_hits; }    // requests served from cache
    [[nodiscard]] u64   misses() const { return m_misses; }  // requests that built a pipeline

    void clear() {
        for (auto& [key, handle] : m_cache) m_device.destroyPipeline(handle);
        m_cache.clear();
    }

private:
    rhi::IGraphicsDevice&                        m_device;
    std::unordered_map<u64, rhi::PipelineHandle> m_cache;
    u64                                          m_hits   = 0;
    u64                                          m_misses = 0;
};

}
