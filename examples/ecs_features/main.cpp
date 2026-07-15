// ECS feature slice, headless: the new Registry facilities proven without a window
// or GPU, so this doubles as a CI regression test the way anim_state does.
//
// Each stage exercises one facility the way gameplay would and prints what it
// verified; the process exits non-zero on the first lie.
//
//   1. Command buffer      — deferred spawn/destroy/add, applied on flush
//   2. Lifecycle hooks      — onAdd/onRemove fire on insert, remove, and destroy
//   3. Events & observers   — emit()/observe() with a typed payload
//   4. Observer propagation — an event bubbling up a Parent chain, and stopped
//   5. Entity disabling     — view() hides a disabled entity, enable() brings it back
//   6. Iter combinations    — every unordered pair visited exactly once
//   7. Delayed commands     — a command that fires only after its timer elapses
//   8. Callbacks            — per-entity behaviour run on demand

#include "vortex/core/log.hpp"
#include "vortex/ecs/callback.hpp"
#include "vortex/ecs/commands.hpp"
#include "vortex/ecs/components.hpp"   // ecs::Parent
#include "vortex/ecs/registry.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace vortex;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// --- Test-local components ------------------------------------------------------
struct Position { f32 x = 0.0f, y = 0.0f; };
struct Health   { i32 hp = 100; };
struct Tag      { int id = 0; };

// --- Test-local events ----------------------------------------------------------
struct DamageEvent { i32 amount = 0; };
struct ClickEvent  { int source = 0; };

void testCommandBuffer() {
    std::printf("Command buffer\n");
    ecs::Registry reg;
    ecs::CommandBuffer cmds;

    ecs::Entity doomed = reg.create();
    reg.emplace<Tag>(doomed, Tag{1});

    // Queue three edits; none takes effect until flush.
    cmds.spawn([](ecs::Registry& r, ecs::Entity e) {
        r.emplace<Position>(e, Position{5.0f, 6.0f});
        r.emplace<Tag>(e, Tag{2});
    });
    cmds.emplace<Health>(doomed, Health{42});
    cmds.destroy(doomed);

    check(reg.aliveCount() == 1, "edits deferred: only the pre-existing entity is alive before flush");
    check(!reg.has<Health>(doomed), "deferred emplace has not applied yet");

    cmds.flush(reg);

    check(reg.aliveCount() == 1, "after flush: one spawned, one destroyed -> count unchanged");
    check(!reg.alive(doomed), "deferred destroy applied");
    check(cmds.empty(), "buffer is drained after flush");

    int found = 0;
    Position seen{};
    reg.view<Position, Tag>([&](ecs::Entity, Position& p, Tag& t) {
        ++found; seen = p; check(t.id == 2, "spawned entity kept its Tag"); });
    check(found == 1 && seen.x == 5.0f && seen.y == 6.0f, "spawned entity has its Position");
}

void testLifecycleHooks() {
    std::printf("Lifecycle hooks\n");
    ecs::Registry reg;

    int adds = 0, removes = 0;
    reg.onAdd<Health>([&](ecs::Registry&, ecs::Entity) { ++adds; });
    reg.onRemove<Health>([&](ecs::Registry& r, ecs::Entity e) {
        // The component is still readable during onRemove.
        check(r.has<Health>(e), "onRemove sees the component still present");
        ++removes;
    });

    ecs::Entity a = reg.create();
    reg.emplace<Health>(a, Health{10});
    check(adds == 1, "onAdd fired on first insert");

    reg.emplace<Health>(a, Health{20});   // overwrite, not a new add
    check(adds == 1, "onAdd does not fire on overwrite");

    reg.remove<Health>(a);
    check(removes == 1, "onRemove fired on explicit remove");

    ecs::Entity b = reg.create();
    reg.emplace<Health>(b, Health{30});
    check(adds == 2, "onAdd fired for a second entity");
    reg.destroy(b);
    check(removes == 2, "onRemove fired during destroy for the still-owned component");
}

void testEventsAndObservers() {
    std::printf("Events & observers\n");
    ecs::Registry reg;

    i32 total = 0;
    int calls = 0;
    reg.observe<DamageEvent>([&](ecs::Trigger<DamageEvent>& t) {
        total += t.event.amount; ++calls; });
    reg.observe<DamageEvent>([&](ecs::Trigger<DamageEvent>&) { ++calls; });  // second observer

    reg.trigger(DamageEvent{7});
    reg.trigger(DamageEvent{5});

    check(total == 12, "both global triggers reached the accumulating observer");
    check(calls == 4, "both observers ran for each of the two events");
}

void testObserverPropagation() {
    std::printf("Observer propagation\n");
    ecs::Registry reg;

    // grandparent <- parent <- child
    ecs::Entity grandparent = reg.create();
    ecs::Entity parent      = reg.create();
    ecs::Entity child       = reg.create();
    reg.emplace<ecs::Parent>(parent, ecs::Parent{grandparent});
    reg.emplace<ecs::Parent>(child,  ecs::Parent{parent});
    reg.emplace<Tag>(grandparent, Tag{100});
    reg.emplace<Tag>(parent,      Tag{200});
    reg.emplace<Tag>(child,       Tag{300});

    // An observer that records which entities a bubbling event visited, in order.
    std::vector<int> visited;
    reg.observe<ClickEvent>([&](ecs::Trigger<ClickEvent>& t) {
        if (Tag* tag = t.registry.tryGet<Tag>(t.entity)) visited.push_back(tag->id);
    });

    reg.triggerPropagate<ClickEvent, ecs::Parent>(child, ClickEvent{1});
    check(visited.size() == 3 && visited[0] == 300 && visited[1] == 200 && visited[2] == 100,
          "event bubbled child -> parent -> grandparent");

    // Now stop the bubble at the parent.
    visited.clear();
    reg.observe<ClickEvent>([&](ecs::Trigger<ClickEvent>& t) {
        if (Tag* tag = t.registry.tryGet<Tag>(t.entity); tag && tag->id == 200)
            t.stopPropagation();
    });
    reg.triggerPropagate<ClickEvent, ecs::Parent>(child, ClickEvent{2});
    check(visited.size() == 2 && visited.back() == 200,
          "stopPropagation() halted the bubble at the parent");
}

void testEntityDisabling() {
    std::printf("Entity disabling\n");
    ecs::Registry reg;

    std::vector<ecs::Entity> es;
    for (int i = 0; i < 3; ++i) {
        ecs::Entity e = reg.create();
        reg.emplace<Tag>(e, Tag{i});
        es.push_back(e);
    }

    auto countVisible = [&] {
        int n = 0;
        reg.view<Tag>([&](ecs::Entity, Tag&) { ++n; });
        return n;
    };

    check(countVisible() == 3, "all three visible to view() initially");

    reg.disable(es[1]);
    check(reg.isDisabled(es[1]), "entity reports disabled");
    check(countVisible() == 2, "view() hides the disabled entity");
    check(reg.alive(es[1]) && reg.has<Tag>(es[1]), "disabled entity is still alive with its component");

    reg.enable(es[1]);
    check(countVisible() == 3, "enable() brings it back into view()");
}

void testIterCombinations() {
    std::printf("Iter combinations\n");
    ecs::Registry reg;

    for (int i = 0; i < 4; ++i) {
        ecs::Entity e = reg.create();
        reg.emplace<Tag>(e, Tag{i});
    }

    int pairs = 0;
    bool selfPair = false;
    reg.viewCombinations<Tag>([&](ecs::Entity a, Tag&, ecs::Entity b, Tag&) {
        ++pairs;
        if (a == b) selfPair = true;
    });
    check(pairs == 6, "4 entities -> 6 unordered pairs (N*(N-1)/2)");
    check(!selfPair, "no entity is ever paired with itself");
}

void testDelayedCommands() {
    std::printf("Delayed commands\n");
    ecs::Registry reg;
    ecs::DelayedCommands delayed;

    bool fired = false;
    delayed.after(1.0f, [&](ecs::Registry&) { fired = true; });
    check(delayed.pending() == 1, "one command scheduled");

    delayed.update(0.5f, reg);
    check(!fired && delayed.pending() == 1, "not fired before its delay elapses");

    delayed.update(0.6f, reg);   // cumulative 1.1s > 1.0s
    check(fired, "fired once the delay elapsed");
    check(delayed.pending() == 0, "spent command is dropped");
}

void testCallbacks() {
    std::printf("Callbacks\n");
    ecs::Registry reg;

    ecs::Entity a = reg.create();
    reg.emplace<Health>(a, Health{100});
    reg.emplace<ecs::Callback>(a, ecs::Callback{
        .run = [](ecs::Registry& r, ecs::Entity self) { r.get<Health>(self).hp -= 10; }});

    ecs::Entity b = reg.create();
    reg.emplace<Health>(b, Health{100});
    reg.emplace<ecs::Callback>(b, ecs::Callback{
        .run = [](ecs::Registry& r, ecs::Entity self) { r.get<Health>(self).hp = 0; }});

    ecs::runCallbacks(reg);
    ecs::runCallbacks(reg);

    check(reg.get<Health>(a).hp == 80, "entity a's callback ran twice (100 -> 80)");
    check(reg.get<Health>(b).hp == 0, "entity b's own distinct behaviour ran");
}

}

int main() {
    testCommandBuffer();
    testLifecycleHooks();
    testEventsAndObservers();
    testObserverPropagation();
    testEntityDisabling();
    testIterCombinations();
    testDelayedCommands();
    testCallbacks();

    if (g_failures == 0) {
        std::printf("\nAll ECS feature checks passed.\n");
        return 0;
    }
    std::printf("\n%d ECS feature check(s) FAILED.\n", g_failures);
    return 1;
}
