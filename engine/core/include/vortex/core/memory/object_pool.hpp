#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/types.hpp"

#include <utility>
#include <vector>

namespace vortex {

template <class T, class Tag = T>
class ObjectPool {
public:
    using HandleT = Handle<Tag>;

    template <class... Args>
    [[nodiscard]] HandleT create(Args&&... args) {
        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
            m_slots[index].value = T{std::forward<Args>(args)...};
            m_slots[index].alive = true;
        } else {
            index = static_cast<u32>(m_slots.size());
            m_slots.push_back({T{std::forward<Args>(args)...}, 0u, true});
        }
        ++m_alive;
        return HandleT{index, m_slots[index].generation};
    }

    [[nodiscard]] T* get(HandleT h) {
        if (h.index >= m_slots.size()) return nullptr;
        Slot& s = m_slots[h.index];
        return (s.alive && s.generation == h.generation) ? &s.value : nullptr;
    }

    [[nodiscard]] const T* get(HandleT h) const {
        if (h.index >= m_slots.size()) return nullptr;
        const Slot& s = m_slots[h.index];
        return (s.alive && s.generation == h.generation) ? &s.value : nullptr;
    }

    void destroy(HandleT h) {
        if (h.index >= m_slots.size()) return;
        Slot& s = m_slots[h.index];
        if (!s.alive || s.generation != h.generation) return;
        s.alive = false;
        ++s.generation;            // invalidate every outstanding handle to this slot
        m_free.push_back(h.index);
        --m_alive;
    }

    template <class Fn>
    void forEach(Fn&& fn) {
        for (u32 i = 0; i < m_slots.size(); ++i)
            if (m_slots[i].alive) fn(HandleT{i, m_slots[i].generation}, m_slots[i].value);
    }

    [[nodiscard]] usize aliveCount() const { return m_alive; }
    [[nodiscard]] usize capacity()   const { return m_slots.size(); }

    void clear() {
        m_slots.clear();
        m_free.clear();
        m_alive = 0;
    }

private:
    struct Slot {
        T    value;
        u32  generation = 0;
        bool alive      = false;
    };
    std::vector<Slot> m_slots;
    std::vector<u32>  m_free;
    usize             m_alive = 0;
};

}
