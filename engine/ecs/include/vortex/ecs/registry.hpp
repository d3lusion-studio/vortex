#pragma once
#include "vortex/core/assert.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace vortex::ecs {

class Registry;

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

// A parallel id space for event types, so an event and a component never index
// the same observer/hook table by accident. Kept separate from componentId so
// naming an event type never reserves a (never-used) component pool slot.
inline u32 nextEventId() {
    static u32 counter = 0;
    return counter++;
}
template <class E>
u32 eventTypeId() {
    static const u32 id = nextEventId();
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

// The context an observer receives when an event fires. Carries the event, the
// entity it fired at (invalid for a global trigger), and the Registry so the
// observer can react with more ECS work. Calling stopPropagation() halts a
// bubbling event before it reaches the next ancestor (see triggerPropagate).
template <class E>
class Trigger {
public:
    Trigger(Registry& reg, Entity target, const E& evt)
        : registry(reg), entity(target), event(evt) {}

    Registry& registry;
    Entity    entity;   // whom the event targets; invalid() for a global trigger
    const E&  event;

    void               stopPropagation() { m_stopped = true; }
    [[nodiscard]] bool stopped() const { return m_stopped; }

private:
    bool m_stopped = false;
};

// Owns entities and their components. Entities are recycled handles; components
// live in per-type sparse sets.
class Registry {
public:
    [[nodiscard]] Entity create() {
        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
            if (index < m_disabled.size()) m_disabled[index] = 0;  // a recycled slot starts enabled
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
        // Fire onRemove for every component the entity still owns while it is
        // alive, so a hook can read the component (and the rest of the entity)
        // one last time. Then tear the components down.
        for (u32 id = 0; id < m_pools.size(); ++id)
            if (m_pools[id] && m_pools[id]->contains(e.index)) fireRemove(id, e);
        for (auto& pool : m_pools)
            if (pool) pool->remove(e);
        if (e.index < m_disabled.size()) m_disabled[e.index] = 0;
        ++m_generations[e.index];   // invalidate every outstanding handle
        m_free.push_back(e.index);
        --m_aliveCount;
    }

    template <class T, class... Args>
    T& emplace(Entity e, Args&&... args) {
        VORTEX_ASSERT(alive(e), "emplace on a dead entity");
        const u32  id    = detail::componentId<T>();
        const bool isNew = !(id < m_pools.size() && m_pools[id] && m_pools[id]->contains(e.index));
        T&         ref   = pool<T>().emplace(e, std::forward<Args>(args)...);
        if (isNew && id < m_onAdd.size() && !m_onAdd[id].empty()) {
            fireAdd(id, e);
            return get<T>(e);   // a hook may have grown/moved the pool; re-resolve
        }
        return ref;
    }

    template <class T>
    void remove(Entity e) {
        const u32 id = detail::componentId<T>();
        if (!(id < m_pools.size() && m_pools[id] && m_pools[id]->contains(e.index))) return;
        fireRemove(id, e);
        if (auto* p = poolPtr<T>()) p->remove(e);   // hook may already have removed it
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
            if (!alive(e) || isDisabled(e)) continue;   // disabled entities are hidden from systems
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

    // Invoke fn(EntityA, T&, EntityB, T&) for every unordered pair of distinct
    // entities that both own a T. The classic use is broad-phase collision or any
    // "every pair once" interaction — N*(N-1)/2 calls, never (a,a) and never both
    // (a,b) and (b,a). Iteration is over a snapshot, so the body may add or remove
    // components without disturbing the walk. Disabled entities are skipped.
    template <class T, class Fn>
    void viewCombinations(Fn&& fn) {
        auto* driver = poolPtr<T>();
        if (!driver) return;
        const std::vector<Entity> snapshot = driver->entities();
        for (usize i = 0; i < snapshot.size(); ++i) {
            const Entity a = snapshot[i];
            if (!alive(a) || isDisabled(a)) continue;
            T* ca = tryGet<T>(a);
            if (!ca) continue;
            for (usize j = i + 1; j < snapshot.size(); ++j) {
                const Entity b = snapshot[j];
                if (!alive(b) || isDisabled(b)) continue;
                T* cb = tryGet<T>(b);
                if (!cb) continue;
                fn(a, *ca, b, *cb);
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

    // --- Entity disabling --------------------------------------------------------
    //
    // Hide an entity from view()/viewCombinations() without destroying it: its
    // components and handle stay valid, but no system iterating with view() sees
    // it, and (because extraction goes through view()) it stops being drawn. This
    // is how you "pause" or "shelve" an entity — a spawned-but-not-yet-active
    // pickup, a unit in reserve — and bring it back untouched with enable(). each()
    // still visits it, so bookkeeping that must cover every entity is unaffected.

    void disable(Entity e) {
        if (!alive(e)) return;
        if (e.index >= m_disabled.size()) m_disabled.resize(e.index + 1, 0);
        m_disabled[e.index] = 1;
    }

    void enable(Entity e) {
        if (e.index < m_disabled.size()) m_disabled[e.index] = 0;
    }

    [[nodiscard]] bool isDisabled(Entity e) const {
        return e.index < m_disabled.size() && m_disabled[e.index] != 0;
    }

    // --- Lifecycle hooks ---------------------------------------------------------
    //
    // Register a callback fired when a component of type T is added to / removed
    // from any entity. onRemove runs while the entity and component are still
    // alive (including during destroy(), once per still-owned component), so the
    // hook can read them a final time — the place to release a handle a component
    // owns, or unhook it from an external system. Hooks are code, not content:
    // they survive clear(). Adding T inside its own onAdd hook (or removing inside
    // onRemove) is allowed but re-entrant — keep such hooks simple.

    template <class T, class Fn>
    void onAdd(Fn&& fn) {
        const u32 id = detail::componentId<T>();
        if (id >= m_onAdd.size()) m_onAdd.resize(id + 1);
        m_onAdd[id].emplace_back(std::forward<Fn>(fn));
    }

    template <class T, class Fn>
    void onRemove(Fn&& fn) {
        const u32 id = detail::componentId<T>();
        if (id >= m_onRemove.size()) m_onRemove.resize(id + 1);
        m_onRemove[id].emplace_back(std::forward<Fn>(fn));
    }

    // --- Events & observers ------------------------------------------------------
    //
    // A push model that complements the pull model of view(): instead of a system
    // scanning for something each frame, code emits an event and every registered
    // observer reacts immediately, in registration order. Unlike lifecycle hooks
    // (which the Registry fires for you), events are your own types with whatever
    // payload you want. Observers, like hooks, survive clear().

    template <class E, class Fn>
    void observe(Fn&& fn) {
        const u32 id = detail::eventTypeId<E>();
        if (id >= m_observers.size()) m_observers.resize(id + 1);
        m_observers[id].emplace_back(
            [f = std::forward<Fn>(fn)](Registry& reg, Entity target, const void* evt) -> bool {
                Trigger<E> t(reg, target, *static_cast<const E*>(evt));
                f(t);
                return t.stopped();
            });
    }

    // Fire `event` at a single entity (invalid() for a global event). Returns true
    // if an observer called stopPropagation() — triggerPropagate() reads that to
    // decide whether to keep bubbling.
    template <class E>
    bool emit(Entity target, const E& event) {
        const u32 id = detail::eventTypeId<E>();
        if (id >= m_observers.size()) return false;
        bool stopped = false;
        for (auto& h : m_observers[id])
            if (h(*this, target, &event)) stopped = true;
        return stopped;
    }

    // A global event, not tied to any entity.
    template <class E>
    void trigger(const E& event) { emit<E>(Entity{}, event); }

    // Fire `event` at `start`, then bubble it up the hierarchy: parent, grandparent,
    // and so on, stopping when an observer calls stopPropagation() or the chain ends.
    // ParentComponent is any component with an `Entity value` field (ecs::Parent is
    // the built-in one) — templated so the Registry stays free of a concrete parent
    // type and a game can bubble along whatever relationship it models. This is how a
    // click on a button reaches the panel that contains it.
    template <class E, class ParentComponent>
    void triggerPropagate(Entity start, const E& event) {
        Entity cur = start;
        while (alive(cur)) {
            if (emit<E>(cur, event)) break;   // an observer stopped the bubble
            auto* parent = tryGet<ParentComponent>(cur);
            if (!parent) break;
            cur = parent->value;
        }
    }

    // Drop every entity, component and pool. Generations reset with them, so an
    // Entity held across a clear() may silently start naming a different entity —
    // treat every handle as dead afterwards. Loading a scene over another does
    // exactly this, which is why loadScene() takes the whole Scene.
    // Lifecycle hooks and observers are deliberately NOT dropped here: like the
    // Scene's systems they are code, not content, and a scene swap that silently
    // unregistered them would be a trap.
    void clear() {
        m_pools.clear();
        m_generations.clear();
        m_free.clear();
        m_disabled.clear();
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

    void fireAdd(u32 id, Entity e) {
        if (id < m_onAdd.size())
            for (auto& h : m_onAdd[id]) h(*this, e);
    }
    void fireRemove(u32 id, Entity e) {
        if (id < m_onRemove.size())
            for (auto& h : m_onRemove[id]) h(*this, e);
    }

    using LifecycleHook = std::function<void(Registry&, Entity)>;
    using ObserverHook  = std::function<bool(Registry&, Entity, const void*)>;

    std::vector<std::unique_ptr<detail::IPool>> m_pools;
    std::vector<u32>                            m_generations;  // per entity index
    std::vector<u32>                            m_free;         // recyclable indices
    std::vector<u8>                             m_disabled;     // per entity index, 1 = hidden
    usize                                       m_aliveCount = 0;

    std::vector<std::vector<LifecycleHook>>     m_onAdd;        // by component id
    std::vector<std::vector<LifecycleHook>>     m_onRemove;     // by component id
    std::vector<std::vector<ObserverHook>>      m_observers;    // by event type id
};

}
