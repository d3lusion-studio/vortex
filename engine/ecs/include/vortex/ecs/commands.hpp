#pragma once
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/ecs/registry.hpp"

#include <functional>
#include <utility>
#include <vector>

namespace vortex::ecs {

// A queue of structural edits (spawn, destroy, add/remove component) recorded now
// and applied later, all at once, by flush().
//
// Why defer at all when Registry::view() already snapshots its driver pool and
// tolerates edits mid-iteration? Two reasons the snapshot does not cover:
//
//   * Ordering. A system that destroys entity A and, later in the same pass,
//     reads A, wants A to still be there until the pass ends. Recording the
//     destroy and flushing after the pass gives every system the same consistent
//     world for the whole frame.
//   * Batching. Deciding "spawn 200 of these next frame" from inside a tight loop
//     is one push_back each here, then one flush — instead of interleaving
//     allocation with iteration.
//
// A CommandBuffer holds std::function closures, so it owns copies of whatever the
// command needs; nothing here points back into the Registry until flush(Registry&).
class CommandBuffer {
public:
    using Command = std::function<void(Registry&)>;

    // Create an entity at flush time and run `init(registry, newEntity)` on it. The
    // entity does not exist yet, so init is how you populate it — the deferred
    // equivalent of `Entity e = reg.create(); ...`.
    void spawn(std::function<void(Registry&, Entity)> init) {
        m_commands.emplace_back(
            [f = std::move(init)](Registry& reg) {
                const Entity e = reg.create();
                if (f) f(reg, e);
            });
    }

    void destroy(Entity e) {
        m_commands.emplace_back([e](Registry& reg) { reg.destroy(e); });
    }

    // Add T{args...} to `e` at flush time. The arguments are decay-copied into the
    // command now, so they must outlive this call by value, not by reference. The
    // add is skipped if `e` is dead by the time we flush.
    template <class T, class... Args>
    void emplace(Entity e, Args&&... args) {
        m_commands.emplace_back(
            [e, ... a = std::forward<Args>(args)](Registry& reg) mutable {
                if (reg.alive(e)) reg.emplace<T>(e, std::move(a)...);
            });
    }

    template <class T>
    void remove(Entity e) {
        m_commands.emplace_back([e](Registry& reg) {
            if (reg.alive(e)) reg.remove<T>(e);
        });
    }

    void disable(Entity e) {
        m_commands.emplace_back([e](Registry& reg) { reg.disable(e); });
    }

    void enable(Entity e) {
        m_commands.emplace_back([e](Registry& reg) { reg.enable(e); });
    }

    // Escape hatch for anything the typed helpers do not cover — a multi-step edit,
    // a trigger, a call into game state. Runs in queue order like the rest.
    void custom(Command cmd) { m_commands.emplace_back(std::move(cmd)); }

    // Apply every recorded command, in the order recorded, then empty the queue so
    // the same buffer can be reused next frame. A command may itself spawn/destroy;
    // those take effect immediately (we are past iteration by now). The queue is
    // swapped out before running, so a command that records into THIS buffer again
    // is safe — the new command waits for the next flush rather than corrupting the
    // one in flight.
    void flush(Registry& registry) {
        std::vector<Command> batch;
        batch.swap(m_commands);
        for (auto& cmd : batch) cmd(registry);
    }

    void clear() { m_commands.clear(); }

    [[nodiscard]] bool  empty() const { return m_commands.empty(); }
    [[nodiscard]] usize size() const { return m_commands.size(); }

private:
    std::vector<Command> m_commands;
};

// A CommandBuffer whose commands fire after a wall-clock delay instead of at the
// next flush. Feed it dt every frame; when an entry's timer reaches zero the
// command runs and the entry is dropped. This is the ECS-level "do X in N seconds"
// — respawn a pickup, end a power-up, detonate a fused bomb — without a hand-rolled
// timer component and a system to tick it on every entity.
class DelayedCommands {
public:
    using Command = std::function<void(Registry&)>;

    // Run `cmd` after `seconds`. A delay <= 0 means the next update() runs it.
    void after(f32 seconds, Command cmd) {
        m_entries.push_back({seconds, std::move(cmd)});
    }

    // Advance every timer by dt and run whatever came due, in the order it was
    // scheduled. Due commands are collected first and run after the pending list is
    // compacted, so a command that schedules another via after() is safe: the new
    // entry lands in the pending list and waits for the next update() rather than
    // being ticked or run inside this one.
    void update(f32 dt, Registry& registry) {
        std::vector<Command> due;
        usize                write = 0;
        for (usize i = 0; i < m_entries.size(); ++i) {
            m_entries[i].remaining -= dt;
            if (m_entries[i].remaining <= 0.0f) {
                due.push_back(std::move(m_entries[i].command));
            } else {
                if (write != i) m_entries[write] = std::move(m_entries[i]);
                ++write;
            }
        }
        m_entries.resize(write);
        for (auto& cmd : due) cmd(registry);
    }

    void clear() { m_entries.clear(); }

    [[nodiscard]] usize pending() const { return m_entries.size(); }

private:
    struct Entry {
        f32     remaining;
        Command command;
    };
    std::vector<Entry> m_entries;
};

}
