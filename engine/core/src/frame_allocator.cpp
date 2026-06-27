#include "vortex/core/memory/frame_allocator.hpp"

#include "vortex/core/assert.hpp"

#include <cstdlib>

namespace vortex {

FrameAllocator::FrameAllocator(usize capacity)
    : m_base(static_cast<u8*>(std::malloc(capacity))), m_capacity(capacity) {
    VORTEX_ASSERT(m_base != nullptr, "FrameAllocator: out of memory");
}

FrameAllocator::~FrameAllocator() { std::free(m_base); }

void* FrameAllocator::allocate(usize size, usize align) {
    const usize aligned = (m_offset + (align - 1)) & ~(align - 1);
    if (aligned + size > m_capacity) {
        VORTEX_ASSERT(false, "FrameAllocator: capacity exhausted");
        return nullptr;
    }
    void* p  = m_base + aligned;
    m_offset = aligned + size;
    return p;
}

}
