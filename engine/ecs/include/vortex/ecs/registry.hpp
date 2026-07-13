#pragma once
#include "vortex/core/assert.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace vortex::ecs {

namespace detail {

// Monotonic per-type id, assigned on first use of a component type.
inline u32 nextComponentId() {
    static u32 counter = 0;
    return counter++;
}
template <class T>
u32 componentId() {
    static const u32 id = nextComponentId();
    return id;
}

// Type-erased base so the registry can own pools of unrelated component types
// and still drive destroy() across all of them.
class IPool {
public:
    virtual ~IPool() = default;
    virtual void remove(Entity e)                = 0;
    virtual bool contains(u32 entityIndex) const = 0;
};

// Sparse set: dense, contiguous component data + a sparse index keyed by entity
// index. Iteration walks the dense arrays linearly; lookup/remove are O(1).
template <class T>
class Pool final : public IPool {
public:
    static constexpr u32 kNull = 0xFFFFFFFFu;

    [[nodiscard]] bool contains(u32 entityIndex) const override {
        return entityIndex < m_sparse.size() && m_sparse[entityIndex] != kNull;
    }

    template <class... Args>
    T& emplace(Entity e, Args&&... args) {
        if (e.index >= m_sparse.size())
            m_sparse.resize(e.index + 1, kNull);

        if (m_sparse[e.index] != kNull) {                 // overwrite existing
            T& slot = m_data[m_sparse[e.index]];
            slot    = T{std::forward<Args>(args)...};
            return slot;
        }

        m_sparse[e.index] = static_cast<u32>(m_dense.size());
        m_dense.push_back(e);
        m_data.push_back(T{std::forward<Args>(args)...});
        return m_data.back();
    }

    void remove(Entity e) override {
        if (!contains(e.index)) return;
        const u32 slot = m_sparse[e.index];
        const u32 last = static_cast<u32>(m_dense.size() - 1);
        if (slot != last) {                               // swap-and-pop
            m_dense[slot] = m_dense[last];
            m_data[slot]  = std::move(m_data[last]);
            m_sparse[m_dense[slot].index] = slot;
        }
        m_dense.pop_back();
        m_data.pop_back();
        m_sparse[e.index] = kNull;
    }

    [[nodiscard]] T* tryGet(Entity e) {
        if (!contains(e.index)) return nullptr;
        return &m_data[m_sparse[e.index]];
    }

    [[nodiscard]] const std::vector<Entity>& entities() const { return m_dense; }
    [[nodiscard]] usize                       size() const { return m_dense.size(); }

private:
    std::vector<u32>    m_sparse;   // entity.index -> dense slot (or kNull)
    std::vector<Entity> m_dense;    // entity per dense slot
    std::vector<T>      m_data;     // component per dense slot
};

} // namespace detail

// Owns entities and their components. Entities are recycled handles; components
// live in per-type sparse sets.
class Registry {
public:
    [[nodiscard]] Entity create() {
        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<u32>(m_generations.size());
            m_generations.push_back(0);
        }
        ++m_aliveCount;
        return Entity{index, m_generations[index]};
    }

    [[nodiscard]] bool alive(Entity e) const {
        return e.valid() && e.index < m_generations.size() &&
               m_generations[e.index] == e.generation;
    }

    void destroy(Entity e) {
        if (!alive(e)) return;
        for (auto& pool : m_pools)
            if (pool) pool->remove(e);
        ++m_generations[e.index];   // invalidate every outstanding handle
        m_free.push_back(e.index);
        --m_aliveCount;
    }

    template <class T, class... Args>
    T& emplace(Entity e, Args&&... args) {
        VORTEX_ASSERT(alive(e), "emplace on a dead entity");
        return pool<T>().emplace(e, std::forward<Args>(args)...);
    }

    template <class T>
    void remove(Entity e) {
        if (auto* p = poolPtr<T>()) p->remove(e);
    }

    template <class T>
    [[nodiscard]] bool has(Entity e) {
        auto* p = poolPtr<T>();
        return p && p->contains(e.index);
    }

    template <class T>
    [[nodiscard]] T* tryGet(Entity e) {
        auto* p = poolPtr<T>();
        return p ? p->tryGet(e) : nullptr;
    }

    template <class T>
    [[nodiscard]] T& get(Entity e) {
        T* t = tryGet<T>(e);
        VORTEX_ASSERT(t, "get() on a missing component");
        return *t;
    }

    // Invoke fn(Entity, First&, Rest&...) for every alive entity owning all of
    // the listed components. Iteration is taken over a snapshot of the first
    // pool, so systems may add/remove components or destroy entities mid-view
    // without invalidating the walk.
    template <class First, class... Rest, class Fn>
    void view(Fn&& fn) {
        auto* driver = poolPtr<First>();
        if (!driver) return;
        const std::vector<Entity> snapshot = driver->entities();
        for (Entity e : snapshot) {
            if (!alive(e)) continue;
            First* c0 = tryGet<First>(e);
            if (!c0) continue;
            if constexpr (sizeof...(Rest) > 0) {
                if (!(has<Rest>(e) && ...)) continue;
                fn(e, *c0, get<Rest>(e)...);
            } else {
                fn(e, *c0);
            }
        }
    }

    template <class Fn>
    void each(Fn&& fn) const {
        std::vector<bool> isFree(m_generations.size(), false);
        for (u32 i : m_free)
            if (i < isFree.size()) isFree[i] = true;
        for (u32 i = 0; i < m_generations.size(); ++i)
            if (!isFree[i]) fn(Entity{i, m_generations[i]});
    }

    // Drop every entity, component and pool. Generations reset with them, so an
    // Entity held across a clear() may silently start naming a different entity —
    // treat every handle as dead afterwards. Loading a scene over another does
    // exactly this, which is why loadScene() takes the whole Scene.
    void clear() {
        m_pools.clear();
        m_generations.clear();
        m_free.clear();
        m_aliveCount = 0;
    }

    [[nodiscard]] usize aliveCount() const { return m_aliveCount; }
    [[nodiscard]] usize capacity() const { return m_generations.size(); }

private:
    template <class T>
    detail::Pool<T>& pool() {
        const u32 id = detail::componentId<T>();
        if (id >= m_pools.size()) m_pools.resize(id + 1);
        if (!m_pools[id]) m_pools[id] = std::make_unique<detail::Pool<T>>();
        return *static_cast<detail::Pool<T>*>(m_pools[id].get());
    }

    template <class T>
    detail::Pool<T>* poolPtr() {
        const u32 id = detail::componentId<T>();
        if (id >= m_pools.size() || !m_pools[id]) return nullptr;
        return static_cast<detail::Pool<T>*>(m_pools[id].get());
    }

    std::vector<std::unique_ptr<detail::IPool>> m_pools;
    std::vector<u32>                            m_generations;  // per entity index
    std::vector<u32>                            m_free;         // recyclable indices
    usize                                       m_aliveCount = 0;
};

}
