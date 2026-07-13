#pragma once
#include "vortex/core/types.hpp"
#include "vortex/ecs/scene.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace vortex::app {

// Named scenes, one of them active. Scenes are held by pointer, so a Scene& handed
// out to gameplay code stays valid when another scene is created or destroyed —
// only destroying that scene invalidates it.
//
// A switch requested during a frame does not take effect until the top of the next
// one. Swapping the world out from under a system that is halfway through iterating
// it is the kind of bug that takes a week to find, so the API makes it impossible.
class SceneManager {
public:
    SceneManager();

    ecs::Scene& create(std::string name);

    // Destroys the scene and everything in it. Refuses to destroy the active scene —
    // switch away first, or the loop would have nothing to run.
    bool destroy(std::string_view name);

    [[nodiscard]] ecs::Scene*       find(std::string_view name);
    [[nodiscard]] const ecs::Scene* find(std::string_view name) const;

    [[nodiscard]] ecs::Scene&       active()       { return *m_scenes[m_active].scene; }
    [[nodiscard]] const ecs::Scene& active() const { return *m_scenes[m_active].scene; }
    [[nodiscard]] std::string_view  activeName() const { return m_scenes[m_active].name; }

    // Queues the switch. False if there is no such scene, in which case nothing is
    // queued and the current scene keeps running.
    bool requestSwitch(std::string_view name);

    // Applies a queued switch. App calls this at the frame boundary; call it
    // yourself only if you are driving your own loop. Returns true if the active
    // scene changed.
    bool applyPendingSwitch();

    [[nodiscard]] bool  switchPending() const noexcept { return m_pending >= 0; }
    [[nodiscard]] usize count() const noexcept { return m_scenes.size(); }

    [[nodiscard]] std::vector<std::string> names() const;

private:
    struct Entry {
        std::string                 name;
        std::unique_ptr<ecs::Scene> scene;
    };

    [[nodiscard]] i32 indexOf(std::string_view name) const;

    std::vector<Entry> m_scenes;
    usize              m_active  = 0;
    i32                m_pending = -1;   // index queued by requestSwitch, or -1
};

}
