import type { Lang } from '@/lib/i18n';

/**
 * Landing-page code snippets.
 *
 * These are trimmed from the real examples in `examples/` — never invent API here. When the
 * engine API changes, these break loudly in review rather than quietly lying to visitors.
 * `sourcePath` points at the file each snippet came from so it can be verified.
 */
export interface SnippetSource {
  id: string;
  label: string;
  /** Captions are prose, so they are translated; the code itself never is. */
  caption: Record<Lang, string>;
  sourcePath: string;
  code: string;
}

export const SNIPPETS: SnippetSource[] = [
  {
    id: 'physics',
    label: 'Physics',
    sourcePath: 'examples/physics_app/main.cpp',
    caption: {
      en: 'No PhysicsWorld to build, no step() to call — asking for app.physics() creates the world and the loop steps it in the fixed update.',
      vi: 'Không phải dựng PhysicsWorld, không phải gọi step() — chỉ cần gọi app.physics() là world được tạo, và vòng lặp tự step nó trong fixed update.',
    },
    code: `#include "vortex/app/app.hpp"
#include "vortex/physics/components.hpp"

using namespace vortex;

ecs::Entity spawnBox(app::App& a, Vec2 at, Vec2 size, Color color) {
    const ecs::Entity e = a.scene().spawn();
    a.registry().get<ecs::Transform2D>(e).position = at;

    a.registry().emplace<ecs::SpriteComp>(e, ecs::SpriteComp{
        .texture = a.whiteTexture(), .color = color, .size = size});

    a.registry().emplace<physics::RigidBody2D>(e, physics::RigidBody2D{
        .restitution = 0.25f});
    a.registry().emplace<physics::BoxCollider2D>(e, physics::BoxCollider2D{
        .halfExtents = size * 0.5f});
    return e;
}`,
  },
  {
    id: 'ecs',
    label: 'ECS',
    sourcePath: 'engine/ecs/include/vortex/ecs/registry.hpp',
    caption: {
      en: 'Archetype-free sparse-set registry. Iteration is linear over packed component arrays — cache-friendly by construction.',
      vi: 'Registry sparse-set, không dùng archetype. Duyệt tuyến tính trên mảng component đóng gói liên tục — thân thiện với cache ngay từ thiết kế.',
    },
    code: `// Systems are plain functions over views. No inheritance, no virtual dispatch.
// Iteration walks packed component arrays, and the view stays valid even if the
// system spawns or destroys entities mid-iteration.
void applyDrag(ecs::Registry& reg, f32 dt) {
    reg.view<physics::RigidBody2D, ecs::Transform2D>(
        [dt](ecs::Entity, physics::RigidBody2D& body, ecs::Transform2D& tf) {
            body.velocity *= 1.0f - 0.5f * dt;
            tf.position   += body.velocity * dt;
        });
}`,
  },
  {
    id: 'rhi',
    label: 'Backend-neutral',
    sourcePath: 'engine/rhi/include/vortex/rhi/device.hpp',
    caption: {
      en: 'The same gameplay and renderer code runs on Vulkan (desktop) and WebGPU (browser). Pick with -DVORTEX_RHI_BACKEND.',
      vi: 'Cùng một code gameplay và renderer chạy trên Vulkan (desktop) lẫn WebGPU (trình duyệt). Chọn backend bằng -DVORTEX_RHI_BACKEND.',
    },
    code: `// Nothing above the RHI ever names Vulkan or WebGPU. The backend is picked
// at startup — the same binary logic ships to desktop and to the browser.
std::unique_ptr<rhi::IGraphicsDevice> device =
    rhi::createDevice(rhi::GraphicsAPI::WebGPU, window);

auto swapchain = device->createSwapchain({
    .width  = window.width(),
    .height = window.height(),
});

// Draw lists are extracted from the ECS into flat, sortable arrays first,
// then submitted — extraction is data-oriented and parallelisable.
std::vector<renderer::RenderItem> items;
ecs::extractSprites(registry, items, &camera.visibleBounds());`,
  },
];
