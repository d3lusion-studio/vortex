#include "vortex/app/scene_manager.hpp"

#include "vortex/core/log.hpp"

namespace vortex::app {

// There is always an active scene. Starting with one called "main" means the simple
// case — a game that never thinks about scenes at all — needs no setup.
SceneManager::SceneManager() {
    create("main");
}

ecs::Scene& SceneManager::create(std::string name) {
    if (const i32 existing = indexOf(name); existing >= 0) {
        VORTEX_WARN("Scene", "scene '%s' already exists; returning the existing one",
                    name.c_str());
        return *m_scenes[static_cast<usize>(existing)].scene;
    }

    m_scenes.push_back({std::move(name), std::make_unique<ecs::Scene>()});
    return *m_scenes.back().scene;
}

bool SceneManager::destroy(std::string_view name) {
    const i32 index = indexOf(name);
    if (index < 0) return false;

    const auto slot = static_cast<usize>(index);
    if (slot == m_active) {
        VORTEX_ERROR("Scene", "cannot destroy the active scene '%.*s'",
                     static_cast<int>(name.size()), name.data());
        return false;
    }

    m_scenes.erase(m_scenes.begin() + index);

    // Erasing shifts everything after it down by one; fix up the indices that point
    // past the hole, or the active scene silently becomes its neighbour.
    if (m_active > slot) --m_active;
    if (m_pending > index)       --m_pending;
    else if (m_pending == index) m_pending = -1;   // the queued target is gone

    return true;
}

i32 SceneManager::indexOf(std::string_view name) const {
    for (usize i = 0; i < m_scenes.size(); ++i)
        if (m_scenes[i].name == name) return static_cast<i32>(i);
    return -1;
}

ecs::Scene* SceneManager::find(std::string_view name) {
    const i32 index = indexOf(name);
    return index >= 0 ? m_scenes[static_cast<usize>(index)].scene.get() : nullptr;
}

const ecs::Scene* SceneManager::find(std::string_view name) const {
    return const_cast<SceneManager*>(this)->find(name);
}

bool SceneManager::requestSwitch(std::string_view name) {
    const i32 index = indexOf(name);
    if (index < 0) {
        VORTEX_ERROR("Scene", "no scene named '%.*s'",
                     static_cast<int>(name.size()), name.data());
        return false;
    }
    m_pending = index;
    return true;
}

bool SceneManager::applyPendingSwitch() {
    if (m_pending < 0) return false;

    const auto target = static_cast<usize>(m_pending);
    m_pending = -1;
    if (target == m_active) return false;

    m_active = target;
    VORTEX_INFO("Scene", "switched to '%s'", m_scenes[m_active].name.c_str());
    return true;
}

std::vector<std::string> SceneManager::names() const {
    std::vector<std::string> out;
    out.reserve(m_scenes.size());
    for (const Entry& entry : m_scenes) out.push_back(entry.name);
    return out;
}

}
