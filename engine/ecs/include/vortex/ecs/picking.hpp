#pragma once
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/components.hpp"   // Transform2D
#include "vortex/ecs/entity.hpp"
#include "vortex/ecs/registry.hpp"

#include <cmath>
#include <functional>

// Pointer picking: turn a mouse position into "which entity is under it" and a
// stream of interaction events — hover, click, drag, drop. It is built on the
// Registry's event system, so gameplay reacts to a click by observing PointerClick,
// exactly the way it observes any other event; the picking code never calls into
// gameplay.
//
// Two seams make it general:
//
//   * The hit-test is a swappable backend. The default tests an oriented box per
//     Pickable, which covers sprites and UI; replace it to pick 3D meshes, to defer
//     to physics pointQuery, or to attach custom hit data (a texel, a bone, a UV).
//   * Events carry a HitResult, not just an entity, so an observer sees WHERE the
//     hit landed and whatever the backend chose to report with it.

namespace vortex::ecs {

// Marks an entity as clickable and describes its clickable box in local space
// (before the entity's Transform2D). The default backend tests this box; a custom
// backend may ignore it.
struct Pickable {
    Vec2 halfSize{0.5f, 0.5f};   // local half-extents, scaled by the entity's transform
    i32  layer     = 0;          // topmost wins: higher layer, then later-drawn, is picked first
    bool draggable = false;
};

// What a hit-test returns. `position` and `data` are the "hit data" a backend
// reports; the default fills position with the pointer's world point and leaves
// `data` at 0 for a game to repurpose (a face index, an atlas cell, ...).
struct HitResult {
    Entity entity;
    Vec2   position{0.0f, 0.0f};
    i32    layer = 0;
    u32    data  = 0;

    [[nodiscard]] bool valid() const { return entity.valid(); }
};

// The pointer's state for one update: where it is, and the edges of its button.
struct PointerInput {
    Vec2 world{0.0f, 0.0f};
    bool down     = false;   // held this frame
    bool pressed  = false;   // went down this frame
    bool released = false;   // went up this frame
};

// --- Events, delivered through Registry::emit at the entity they concern ---------
// The target entity is the observer's Trigger::entity; these payloads add the hit.
struct PointerOver      { HitResult hit; };
struct PointerOut       { HitResult hit; };
struct PointerDown      { HitResult hit; };
struct PointerUp        { HitResult hit; };
struct PointerClick     { HitResult hit; };   // press and release on the same entity, no drag
struct PointerDragStart { HitResult hit; };
struct PointerDrag      { HitResult hit; Vec2 delta; };
struct PointerDragEnd   { HitResult hit; };
struct PointerDrop      { Entity dropped; HitResult hit; };   // target is the drop zone

// Oriented-box hit test over every Pickable, skipping `ignore` (used to see the
// drop zone beneath the entity being dragged). Topmost by layer, ties to the later
// one, matching painter's draw order.
[[nodiscard]] inline HitResult pickBoxes(Registry& reg, Vec2 world, Entity ignore = {}) {
    HitResult best;
    bool      found = false;
    reg.view<Transform2D, Pickable>([&](Entity e, Transform2D& tf, Pickable& p) {
        if (e == ignore) return;
        const Vec2 half  = {p.halfSize.x * tf.scale.x, p.halfSize.y * tf.scale.y};
        const Vec2 local = rotate(world - tf.position, -tf.rotation);   // world -> entity space
        if (std::fabs(local.x) <= half.x && std::fabs(local.y) <= half.y) {
            if (!found || p.layer >= best.layer) {
                best  = HitResult{e, world, p.layer, 0};
                found = true;
            }
        }
    });
    return best;
}

// Drives picking for a single pointer. Feed it a PointerInput every frame; it hit-
// tests, tracks hover/press/drag across frames, and emits the events above. Own one
// per pointer (mouse, or each touch).
class PickingSystem {
public:
    using Backend = std::function<HitResult(Registry&, Vec2 world, Entity ignore)>;

    PickingSystem() : m_backend(pickBoxes) {}

    // Swap the hit-test. This is the "custom picking backend": a mesh raycaster, a
    // physics point query, anything that returns a HitResult.
    void setBackend(Backend backend) { m_backend = std::move(backend); }

    // The entity currently under the pointer (invalid if none). Handy for cursors.
    [[nodiscard]] Entity hovered() const { return m_hovered; }
    [[nodiscard]] Entity dragging() const { return m_dragging; }

    void update(Registry& reg, const PointerInput& in) {
        const HitResult hit  = m_backend(reg, in.world, m_dragging);   // ignore the dragged entity
        const Entity    hitE = hit.entity;

        // Hover transitions.
        if (hitE != m_hovered) {
            if (reg.alive(m_hovered)) reg.emit(m_hovered, PointerOut{HitResult{m_hovered, in.world, 0, 0}});
            if (reg.alive(hitE))      reg.emit(hitE, PointerOver{hit});
            m_hovered = hitE;
        }

        // Press: record who was pressed, announce it, and begin a drag if allowed.
        if (in.pressed) {
            m_pressed   = hitE;
            m_dragMoved = false;
            if (reg.alive(hitE)) {
                reg.emit(hitE, PointerDown{hit});
                if (const Pickable* p = reg.tryGet<Pickable>(hitE); p && p->draggable) {
                    m_dragging = hitE;
                    reg.emit(hitE, PointerDragStart{hit});
                }
            }
        }

        // Drag motion.
        if (in.down && reg.alive(m_dragging)) {
            const Vec2 delta = in.world - m_lastWorld;
            if (lengthSquared(delta) > 0.0f) {
                m_dragMoved = true;
                reg.emit(m_dragging, PointerDrag{HitResult{m_dragging, in.world, 0, 0}, delta});
            }
        }

        // Release: up, then click (no drag), then drag-end and drop.
        if (in.released) {
            if (reg.alive(hitE)) reg.emit(hitE, PointerUp{hit});
            if (!m_dragMoved && hitE.valid() && hitE == m_pressed)
                reg.emit(hitE, PointerClick{hit});
            if (reg.alive(m_dragging)) {
                reg.emit(m_dragging, PointerDragEnd{HitResult{m_dragging, in.world, 0, 0}});
                if (reg.alive(hitE) && hitE != m_dragging)
                    reg.emit(hitE, PointerDrop{m_dragging, hit});
            }
            m_dragging  = Entity{};
            m_pressed   = Entity{};
            m_dragMoved = false;
        }

        m_lastWorld = in.world;
    }

private:
    Backend m_backend;
    Entity  m_hovered;
    Entity  m_pressed;
    Entity  m_dragging;
    Vec2    m_lastWorld{0.0f, 0.0f};
    bool    m_dragMoved = false;
};

}
