#pragma once
#include "vortex/ecs/entity.hpp"

namespace vortex::ecs { class Scene; class Registry; }

namespace vortex::debug {

class EntityInspector {
public:
    void draw(ecs::Scene& scene);

    void draw(ecs::Registry& registry);

    [[nodiscard]] ecs::Entity selected() const { return m_selected; }
    void select(ecs::Entity e) { m_selected = e; }

private:
    void drawList(ecs::Registry& registry);
    void drawComponents(ecs::Registry& registry, ecs::Entity e);

    ecs::Entity m_selected{};
};

}
