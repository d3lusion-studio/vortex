#pragma once
#include "vortex/ecs/entity.hpp"
#include "vortex/ecs/registry.hpp"

#include <functional>

namespace vortex::ecs {

// A behaviour carried by the entity itself rather than by a global system. Where a
// normal system is one function that view()s many entities, a Callback is many
// functions — one per entity — that you invoke on demand. It is the ECS answer to
// "this particular door opens like THIS, that one like THAT": the logic rides along
// with the data instead of living in a switch keyed on a type tag.
//
// The stored function receives the Registry and its own entity, so it can read and
// write the rest of the world. It is plain data — copyable, serialize-skipped — and
// nothing runs it automatically; runCallbacks() (or your own view) does.
struct Callback {
    std::function<void(Registry&, Entity)> run;
};

// Invoke every entity's Callback once, in pool order. Structural changes made by a
// callback are fine — view() iterates a snapshot — but a callback that spawns new
// Callback-bearing entities will not see them run until the next call. Disabled
// entities are skipped, like any view().
inline void runCallbacks(Registry& registry) {
    registry.view<Callback>([&registry](Entity e, Callback& cb) {
        if (cb.run) cb.run(registry, e);
    });
}

}
