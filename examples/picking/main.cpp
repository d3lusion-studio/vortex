// Pointer picking, headless: hover, click, layered hit-testing, drag, drag-and-drop
// and a custom backend — all driven by synthetic pointer input and verified through
// the picking events, with no window or GPU. A CI regression test like the others.
//
//   1. Hover        — Over on enter, Out on leave
//   2. Click        — Down/Up/Click on press-and-release over one entity
//   3. Layering     — the topmost of overlapping pickables wins
//   4. Disabled     — a disabled entity is not pickable
//   5. Drag         — DragStart, Drag(s), DragEnd on a draggable
//   6. Drag & drop  — Drop reports what was dropped and onto whom
//   7. Custom backend — a replaced hit-test drives the same events

#include "vortex/core/log.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/picking.hpp"
#include "vortex/ecs/registry.hpp"

#include <cstdio>

using namespace vortex;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// Spawn a pickable box at `at` with half-size `half`.
ecs::Entity spawnBox(ecs::Registry& reg, Vec2 at, Vec2 half, i32 layer = 0, bool draggable = false) {
    const ecs::Entity e = reg.create();
    reg.emplace<ecs::Transform2D>(e, ecs::Transform2D{.position = at});
    reg.emplace<ecs::Pickable>(e, ecs::Pickable{.halfSize = half, .layer = layer, .draggable = draggable});
    return e;
}

ecs::PointerInput hoverAt(Vec2 world) { return {world, false, false, false}; }

void testHover() {
    std::printf("Hover\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    const ecs::Entity box = spawnBox(reg, {0.0f, 0.0f}, {50.0f, 50.0f});

    int over = 0, out = 0;
    reg.observe<ecs::PointerOver>([&](ecs::Trigger<ecs::PointerOver>& t) {
        if (t.entity == box) ++over; });
    reg.observe<ecs::PointerOut>([&](ecs::Trigger<ecs::PointerOut>& t) {
        if (t.entity == box) ++out; });

    pick.update(reg, hoverAt({500.0f, 0.0f}));   // outside
    check(over == 0 && out == 0, "no events while the pointer is off the box");
    pick.update(reg, hoverAt({10.0f, 10.0f}));   // enter
    check(over == 1 && pick.hovered() == box, "Over fires on enter and hovered() reports it");
    pick.update(reg, hoverAt({20.0f, -20.0f}));  // still inside
    check(over == 1, "Over does not re-fire while staying inside");
    pick.update(reg, hoverAt({500.0f, 0.0f}));   // leave
    check(out == 1 && !pick.hovered().valid(), "Out fires on leave and hover clears");
}

void testClick() {
    std::printf("Click\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    const ecs::Entity box = spawnBox(reg, {0.0f, 0.0f}, {50.0f, 50.0f});

    int down = 0, up = 0, click = 0;
    Vec2 clickPos{};
    reg.observe<ecs::PointerDown>([&](ecs::Trigger<ecs::PointerDown>&) { ++down; });
    reg.observe<ecs::PointerUp>([&](ecs::Trigger<ecs::PointerUp>&) { ++up; });
    reg.observe<ecs::PointerClick>([&](ecs::Trigger<ecs::PointerClick>& t) {
        ++click; clickPos = t.event.hit.position; });

    pick.update(reg, {{5.0f, 5.0f}, /*down*/true, /*pressed*/true, /*released*/false});
    check(down == 1 && click == 0, "Down fires on press, Click waits for release");
    pick.update(reg, {{5.0f, 5.0f}, /*down*/false, /*pressed*/false, /*released*/true});
    check(up == 1 && click == 1, "Up and Click fire on release over the same entity");
    check(clickPos.x == 5.0f && clickPos.y == 5.0f, "Click carries the hit position");

    // Press on the box, release off it: Up (off entity) but no Click.
    int clickBefore = click;
    pick.update(reg, {{5.0f, 5.0f}, true, true, false});
    pick.update(reg, {{500.0f, 0.0f}, false, false, true});
    check(click == clickBefore, "no Click when release lands off the pressed entity");
    (void)box;
}

void testLayering() {
    std::printf("Layering\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    spawnBox(reg, {0.0f, 0.0f}, {50.0f, 50.0f}, /*layer*/0);
    const ecs::Entity top = spawnBox(reg, {0.0f, 0.0f}, {30.0f, 30.0f}, /*layer*/5);

    ecs::Entity hovered;
    reg.observe<ecs::PointerOver>([&](ecs::Trigger<ecs::PointerOver>& t) { hovered = t.entity; });
    pick.update(reg, hoverAt({0.0f, 0.0f}));
    check(hovered == top, "the higher-layer entity is picked where two overlap");
}

void testDisabled() {
    std::printf("Disabled\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    const ecs::Entity box = spawnBox(reg, {0.0f, 0.0f}, {50.0f, 50.0f});
    reg.disable(box);

    int over = 0;
    reg.observe<ecs::PointerOver>([&](ecs::Trigger<ecs::PointerOver>&) { ++over; });
    pick.update(reg, hoverAt({0.0f, 0.0f}));
    check(over == 0 && !pick.hovered().valid(), "a disabled entity is not pickable");
}

void testDrag() {
    std::printf("Drag\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    const ecs::Entity item = spawnBox(reg, {0.0f, 0.0f}, {40.0f, 40.0f}, 0, /*draggable*/true);

    int start = 0, drag = 0, end = 0;
    Vec2 totalDelta{};
    reg.observe<ecs::PointerDragStart>([&](ecs::Trigger<ecs::PointerDragStart>& t) {
        if (t.entity == item) ++start; });
    reg.observe<ecs::PointerDrag>([&](ecs::Trigger<ecs::PointerDrag>& t) {
        ++drag; totalDelta += t.event.delta; });
    reg.observe<ecs::PointerDragEnd>([&](ecs::Trigger<ecs::PointerDragEnd>&) { ++end; });

    pick.update(reg, {{0.0f, 0.0f},  true, true,  false});   // press -> DragStart
    check(start == 1 && pick.dragging() == item, "DragStart fires and dragging() reports the item");
    pick.update(reg, {{20.0f, 0.0f}, true, false, false});   // move
    pick.update(reg, {{40.0f, 10.0f}, true, false, false});  // move
    check(drag == 2, "Drag fires once per moved frame");
    check(totalDelta.x == 40.0f && totalDelta.y == 10.0f, "Drag deltas sum to the travel");
    pick.update(reg, {{40.0f, 10.0f}, false, false, true});  // release -> DragEnd
    check(end == 1 && !pick.dragging().valid(), "DragEnd fires and dragging clears");
}

void testDragAndDrop() {
    std::printf("Drag and drop\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    const ecs::Entity item = spawnBox(reg, {0.0f, 0.0f},   {30.0f, 30.0f}, 0, /*draggable*/true);
    const ecs::Entity zone = spawnBox(reg, {300.0f, 0.0f}, {80.0f, 80.0f}, 0);

    ecs::Entity dropped, onto;
    int drops = 0;
    reg.observe<ecs::PointerDrop>([&](ecs::Trigger<ecs::PointerDrop>& t) {
        ++drops; dropped = t.event.dropped; onto = t.entity; });

    pick.update(reg, {{0.0f, 0.0f},   true, true,  false});  // grab the item
    pick.update(reg, {{300.0f, 0.0f}, true, false, false});  // drag over the zone
    pick.update(reg, {{300.0f, 0.0f}, false, false, true});  // release on the zone
    check(drops == 1, "Drop fires when a dragged item is released over another pickable");
    check(dropped == item && onto == zone, "Drop names what was dropped and the zone it landed on");
}

void testCustomBackend() {
    std::printf("Custom backend\n");
    ecs::Registry reg;
    ecs::PickingSystem pick;
    const ecs::Entity target = reg.create();   // no Transform/Pickable — the default backend can't see it

    // A backend that ignores geometry entirely and always reports `target`, with a
    // custom `data` payload the events then carry.
    pick.setBackend([target](ecs::Registry&, Vec2 world, ecs::Entity) {
        return ecs::HitResult{target, world, 0, /*data*/77u};
    });

    u32 seenData = 0;
    reg.observe<ecs::PointerClick>([&](ecs::Trigger<ecs::PointerClick>& t) {
        seenData = t.event.hit.data; });

    pick.update(reg, {{123.0f, 0.0f}, true, true, false});
    pick.update(reg, {{123.0f, 0.0f}, false, false, true});
    check(pick.hovered() == target, "the custom backend decides what is hovered");
    check(seenData == 77u, "custom hit data rides along on the events");
}

}

int main() {
    testHover();
    testClick();
    testLayering();
    testDisabled();
    testDrag();
    testDragAndDrop();
    testCustomBackend();

    if (g_failures == 0) {
        std::printf("\nAll picking checks passed.\n");
        return 0;
    }
    std::printf("\n%d picking check(s) FAILED.\n", g_failures);
    return 1;
}
