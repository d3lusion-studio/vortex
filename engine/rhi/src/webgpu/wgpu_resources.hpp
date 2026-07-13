#pragma once
#include "wgpu_common.hpp"
#include "vortex/core/handle.hpp"

#include <vector>

namespace vortex::rhi::wgpu {

struct WebGPUBuffer {
    WGPUBuffer buffer = nullptr;
    u64        size   = 0;
};

struct WebGPUTexture {
    WGPUTexture     texture = nullptr;
    WGPUTextureView view    = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    u32  bpp    = 0;   // pixel pitch, for updateTexture
    u32  width  = 0;
    u32  height = 0;
    bool isDepth = false;
    bool ownsTexture = true;   // surface (backbuffer) textures are owned by the surface
};

struct WebGPUSampler {
    WGPUSampler sampler = nullptr;
};

struct WebGPUBindGroup {
    WGPUBindGroup group = nullptr;
};

struct WebGPUPipeline {
    WGPURenderPipeline pipeline = nullptr;
    WGPUPipelineLayout layout   = nullptr;
    u32  pushConstantSize = 0;
};

template <typename T, typename Tag>
class Pool {
public:
    using HandleT = Handle<Tag>;

    [[nodiscard]] HandleT create(const T& value) {
        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
            m_slots[index].value = value;
            m_slots[index].alive = true;
        } else {
            index = static_cast<u32>(m_slots.size());
            m_slots.push_back({value, 0u, true});
        }
        return HandleT{.index = index, .generation = m_slots[index].generation};
    }

    [[nodiscard]] T* get(HandleT h) {
        if (h.index >= m_slots.size()) return nullptr;
        Slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation) return nullptr;
        return &s.value;
    }

    void destroy(HandleT h) {
        if (h.index >= m_slots.size()) return;
        Slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation) return;
        s.alive = false;
        ++s.generation;
        m_free.push_back(h.index);
    }

    template <typename Fn>
    void forEachAlive(Fn&& fn) {
        for (Slot& s : m_slots)
            if (s.alive) fn(s.value);
    }

private:
    struct Slot {
        T    value;
        u32  generation = 0;
        bool alive      = false;
    };
    std::vector<Slot> m_slots;
    std::vector<u32>  m_free;
};

}
