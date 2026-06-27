#pragma once
#include "vortex/core/types.hpp"

#include <new>
#include <utility>

namespace vortex {

class FrameAllocator {
public:
    explicit FrameAllocator(usize capacity);
    ~FrameAllocator();

    FrameAllocator(const FrameAllocator&)            = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // Returns nullptr if the block is exhausted.
    [[nodiscard]] void* allocate(usize size, usize align = alignof(std::max_align_t));

    template <class T>
    [[nodiscard]] T* allocArray(usize count) {
        return static_cast<T*>(allocate(count * sizeof(T), alignof(T)));
    }

    // Construct a single object in the arena (trivially destroyed on reset()).
    template <class T, class... Args>
    [[nodiscard]] T* create(Args&&... args) {
        void* p = allocate(sizeof(T), alignof(T));
        return p ? new (p) T(std::forward<Args>(args)...) : nullptr;
    }

    void reset() { m_offset = 0; }

    [[nodiscard]] usize used()     const { return m_offset; }
    [[nodiscard]] usize capacity() const { return m_capacity; }

private:
    u8*   m_base;
    usize m_capacity;
    usize m_offset = 0;
};

}
