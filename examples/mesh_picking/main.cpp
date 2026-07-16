// 3D mesh picking, headless: the ray math, nearest-hit selection through transforms,
// precise triangle picking, and the camera-driven backend feeding the same
// PickingSystem that drives 2D picking — all on the CPU, no window or GPU. A CI
// regression test like the others.
//
//   1. Ray math      — ray vs AABB / sphere / triangle, hits and misses
//   2. Occlusion     — the nearest of several meshes along a ray wins
//   3. Transforms    — rotating an entity changes what the ray hits
//   4. Precise picking — a triangle proxy rejects a ray a loose AABB would accept
//   5. Camera backend — a screen-space click picks the mesh under the cursor

#include "vortex/core/log.hpp"
#include "vortex/core/math/bounds3d.hpp"
#include "vortex/ecs/components.hpp"
#include "vortex/ecs/picking.hpp"
#include "vortex/ecs/picking3d.hpp"
#include "vortex/ecs/registry.hpp"
#include "vortex/renderer/camera.hpp"

#include <cmath>
#include <cstdio>

using namespace vortex;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

bool near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// A box pickable via its AABB, positioned by the entity's transform.
ecs::Entity spawnBox(ecs::Registry& reg, Vec3 pos, Vec3 half, Quat rot = Quat::identity()) {
    const ecs::Entity e = reg.create();
    reg.emplace<ecs::Transform3D>(e, ecs::Transform3D{.position = pos, .rotation = rot});
    reg.emplace<ecs::MeshPickable>(e, ecs::MeshPickable{
        .bounds = Aabb3D::fromCenterHalf({0.0f, 0.0f, 0.0f}, half)});
    return e;
}

void testRayMath() {
    std::printf("Ray math\n");
    const Aabb3D box = Aabb3D::fromCenterHalf({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
    const Ray3D  down{{0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
    check(near(raycast(down, box), 4.0f), "ray hits AABB top face at the expected distance");

    const Ray3D away{{0.0f, 5.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    check(raycast(away, box) < 0.0f, "ray pointing away from the AABB misses");

    const Sphere3D sphere{{0.0f, 0.0f, 0.0f}, 2.0f};
    const Ray3D    intoSphere{{0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    check(near(raycast(intoSphere, sphere), 8.0f), "ray hits sphere at the expected distance");

    // A triangle in the z=0 plane; ray straight at its interior, then just outside.
    const Vec3 v0{-1.0f, -1.0f, 0.0f}, v1{1.0f, -1.0f, 0.0f}, v2{0.0f, 1.0f, 0.0f};
    const Ray3D hit{{0.0f, -0.2f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    const Ray3D miss{{2.0f, 2.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
    Vec3 n;
    check(near(rayTriangle(hit, v0, v1, v2, &n), 5.0f), "ray through the triangle interior hits");
    check(rayTriangle(miss, v0, v1, v2) < 0.0f, "ray outside the triangle misses");
    check(near(std::fabs(n.z), 1.0f), "triangle normal points along z");
}

void testOcclusion() {
    std::printf("Occlusion (nearest wins)\n");
    ecs::Registry reg;
    const ecs::Entity front  = spawnBox(reg, {0.0f, 0.0f, 0.0f},  {0.5f, 0.5f, 0.5f});
    const ecs::Entity middle = spawnBox(reg, {0.0f, 0.0f, -3.0f}, {0.5f, 0.5f, 0.5f});
    (void)spawnBox(reg, {0.0f, 0.0f, -6.0f}, {0.5f, 0.5f, 0.5f});

    const ecs::MeshHit hit = ecs::pickMeshes(reg, Ray3D{{0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}});
    check(hit.entity == front, "the nearest box along the ray is the one picked");
    check(near(hit.distance, 9.5f), "hit distance is to the front face of the nearest box");

    // Ignore the front box and the ray should fall through to the next one.
    const ecs::MeshHit behind = ecs::pickMeshes(reg,
        Ray3D{{0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}}, /*ignore=*/front);
    check(behind.entity == middle, "ignoring the nearest exposes the one behind it");
}

void testTransforms() {
    std::printf("Transforms\n");
    // A box long along local X. A ray at x = 1.5 threads through it while it lies
    // flat, but a 90-degree turn about Y swings that length onto Z and the ray misses.
    ecs::Registry reg;
    const ecs::Entity box = spawnBox(reg, {0.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 0.5f});
    const Ray3D ray{{1.5f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    check(ecs::pickMeshes(reg, ray).entity == box, "ray hits the long box while it is unrotated");

    reg.get<ecs::Transform3D>(box).rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kHalfPi);
    check(!ecs::pickMeshes(reg, ray).valid(), "rotating the box 90 degrees makes the ray miss");
}

void testPrecisePicking() {
    std::printf("Precise triangle picking\n");
    ecs::Registry reg;

    // A pickable whose proxy is a single triangle tucked in the +x/+y corner. A ray
    // down the local origin would hit its bounding box but misses the triangle.
    const ecs::Entity tri = reg.create();
    reg.emplace<ecs::Transform3D>(tri, ecs::Transform3D{});
    reg.emplace<ecs::MeshPickable>(tri, ecs::MeshPickable{
        .triangles = {{1.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 0.0f}}});

    const Ray3D throughOrigin{{0.0f, 0.0f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    check(!ecs::pickMeshes(reg, throughOrigin).valid(),
          "a triangle proxy rejects a ray its AABB would have accepted");

    const Ray3D throughTriangle{{1.3f, 1.3f, 10.0f}, {0.0f, 0.0f, -1.0f}};
    check(ecs::pickMeshes(reg, throughTriangle).entity == tri,
          "a ray through the actual triangle hits");
}

void testCameraBackend() {
    std::printf("Camera backend + PickingSystem\n");
    ecs::Registry reg;
    const ecs::Entity front = spawnBox(reg, {0.0f, 0.0f, 0.0f},  {0.6f, 0.6f, 0.6f});
    (void)spawnBox(reg, {0.0f, 0.0f, -3.0f}, {0.6f, 0.6f, 0.6f});   // occluded

    renderer::Camera cam;
    cam.mode           = renderer::Camera::Mode::Perspective;
    cam.position       = {0.0f, 0.0f, 5.0f};
    cam.target         = {0.0f, 0.0f, 0.0f};
    cam.up             = {0.0f, 1.0f, 0.0f};
    cam.viewportWidth  = 800.0f;
    cam.viewportHeight = 600.0f;
    cam.aspect         = 800.0f / 600.0f;

    ecs::PickingSystem pick;
    pick.setBackend(ecs::meshPickBackend(cam));

    ecs::Entity clicked;
    int clicks = 0;
    reg.observe<ecs::PointerClick>([&](ecs::Trigger<ecs::PointerClick>& t) {
        ++clicks; clicked = t.entity; });

    const Vec2 center{400.0f, 300.0f};   // screen centre
    pick.update(reg, {center, /*down*/true, /*pressed*/true, /*released*/false});
    pick.update(reg, {center, /*down*/false, /*pressed*/false, /*released*/true});
    check(clicks == 1 && clicked == front, "clicking screen centre picks the front mesh (not the occluded one)");

    // Point the cursor into empty space: nothing is hovered.
    pick.update(reg, {{5.0f, 5.0f}, false, false, false});
    check(!pick.hovered().valid(), "a ray into empty space hovers nothing");
}

}

int main() {
    testRayMath();
    testOcclusion();
    testTransforms();
    testPrecisePicking();
    testCameraBackend();

    if (g_failures == 0) {
        std::printf("\nAll mesh-picking checks passed.\n");
        return 0;
    }
    std::printf("\n%d mesh-picking check(s) FAILED.\n", g_failures);
    return 1;
}
