#include "vortex/debug/inspector.hpp"

#include "vortex/ecs/components.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/ecs/scene.hpp"

#include <imgui.h>

#include <cstdio>

namespace vortex::debug {

using namespace vortex::ecs;

namespace {
void transformUI(Transform2D& t) {
    ImGui::DragFloat2("position", &t.position.x, 1.0f);
    ImGui::DragFloat("rotation", &t.rotation, 0.01f);
    ImGui::DragFloat2("scale", &t.scale.x, 0.01f, 0.0f, 100.0f);
}

void worldTransformUI(const WorldTransform2D& w) {
    ImGui::TextDisabled("derived from Transform2D + Parent");
    ImGui::Text("world pos: %.1f, %.1f", w.matrix.at(0, 3), w.matrix.at(1, 3));
}

void spriteUI(SpriteComp& s) {
    ImGui::ColorEdit4("color", &s.color.x);
    ImGui::DragFloat2("size", &s.size.x, 0.5f, 0.0f, 10000.0f);
    ImGui::DragInt("layer", &s.layer);
    ImGui::Text("texture: %u (gen %u)", s.texture.index, s.texture.generation);
}

void velocityUI(Velocity& v) {
    ImGui::DragFloat2("value", &v.value.x, 1.0f);
}

void parentUI(Parent& p) {
    int idx = static_cast<int>(p.value.index);
    if (ImGui::InputInt("entity index", &idx)) {
        if (idx < 0) idx = 0;
        p.value.index = static_cast<u32>(idx);
    }
    ImGui::DragScalar("generation", ImGuiDataType_U32, &p.value.generation);
}

bool removableHeader(const char* label, bool& remove) {
    bool open = true;
    const bool body = ImGui::CollapsingHeader(label, &open, ImGuiTreeNodeFlags_DefaultOpen);
    if (!open) remove = true;
    return body && open;
}

}

void EntityInspector::drawList(Registry& registry) {
    ImGui::BeginChild("entity_list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    registry.each([&](Entity e) {
        char label[64];
        std::snprintf(label, sizeof(label), "Entity %u (gen %u)", e.index, e.generation);
        if (ImGui::Selectable(label, m_selected == e)) m_selected = e;
    });
    ImGui::EndChild();
}

void EntityInspector::drawComponents(Registry& registry, Entity e) {
    if (!registry.alive(e)) {
        ImGui::TextDisabled("No entity selected.");
        return;
    }

    ImGui::Text("Entity %u (gen %u)", e.index, e.generation);
    ImGui::Separator();

    if (auto* t = registry.tryGet<Transform2D>(e))
        if (ImGui::CollapsingHeader("Transform2D", ImGuiTreeNodeFlags_DefaultOpen)) transformUI(*t);
    if (auto* w = registry.tryGet<WorldTransform2D>(e))
        if (ImGui::CollapsingHeader("WorldTransform2D")) worldTransformUI(*w);

    bool removeSprite = false, removeVelocity = false, removeParent = false;
    if (auto* s = registry.tryGet<SpriteComp>(e))
        if (removableHeader("SpriteComp", removeSprite)) spriteUI(*s);
    if (auto* v = registry.tryGet<Velocity>(e))
        if (removableHeader("Velocity", removeVelocity)) velocityUI(*v);
    if (auto* p = registry.tryGet<Parent>(e))
        if (removableHeader("Parent", removeParent)) parentUI(*p);

    ImGui::Separator();
    if (ImGui::Button("Add component")) ImGui::OpenPopup("add_component");
    if (ImGui::BeginPopup("add_component")) {
        if (!registry.has<SpriteComp>(e) && ImGui::MenuItem("SpriteComp")) registry.emplace<SpriteComp>(e);
        if (!registry.has<Velocity>(e)   && ImGui::MenuItem("Velocity"))   registry.emplace<Velocity>(e);
        if (!registry.has<Parent>(e)     && ImGui::MenuItem("Parent"))     registry.emplace<Parent>(e);
        ImGui::EndPopup();
    }

    if (removeSprite)   registry.remove<SpriteComp>(e);
    if (removeVelocity) registry.remove<Velocity>(e);
    if (removeParent)   registry.remove<Parent>(e);
}

void EntityInspector::draw(Registry& registry) {
    ImGui::Begin("Entities");
    ImGui::Text("alive: %zu", registry.aliveCount());
    ImGui::Separator();
    drawList(registry);
    ImGui::End();

    ImGui::Begin("Inspector");
    drawComponents(registry, m_selected);
    ImGui::End();
}

void EntityInspector::draw(Scene& scene) {
    Registry& registry = scene.registry();

    ImGui::Begin("Entities");
    ImGui::Text("alive: %zu", registry.aliveCount());
    if (ImGui::Button("Spawn")) m_selected = scene.spawn();
    ImGui::SameLine();
    if (ImGui::Button("Destroy") && registry.alive(m_selected)) {
        scene.destroy(m_selected);
        m_selected = {};
    }
    ImGui::Separator();
    drawList(registry);
    ImGui::End();

    ImGui::Begin("Inspector");
    drawComponents(registry, m_selected);
    ImGui::End();
}

}
