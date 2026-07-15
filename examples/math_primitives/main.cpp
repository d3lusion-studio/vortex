// 2D math library slice, headless: bounding volumes, intersection/raycast tests,
// primitive measures, uniform sampling, and a game-defined custom primitive that
// plugs into the same generic algorithms. A CI regression test like anim_state —
// every stage prints what it verified and the process exits non-zero on the first
// lie.
//
//   1. Bounding volume intersections — AABB/circle overlap + ray casts
//   2. Primitives                    — area/perimeter/contains
//   3. Random sampling               — sampled points land inside, and fill evenly
//   4. Custom primitive              — a Star satisfies Bounded2d and joins the herd

#include "vortex/core/math/bounds2d.hpp"
#include "vortex/core/math/primitives2d.hpp"
#include "vortex/core/math/random.hpp"

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

void testBoundingIntersections() {
    std::printf("Bounding volume intersections\n");

    const Aabb2D a = Aabb2D::fromCenterHalf({0.0f, 0.0f}, {1.0f, 1.0f});
    const Aabb2D b = Aabb2D::fromCenterHalf({1.5f, 0.0f}, {1.0f, 1.0f});
    const Aabb2D c = Aabb2D::fromCenterHalf({3.0f, 0.0f}, {0.5f, 0.5f});
    check(intersects(a, b), "overlapping AABBs report intersect");
    check(!intersects(a, c), "separated AABBs report no intersect");

    const BoundingCircle c0{{0.0f, 0.0f}, 1.0f};
    const BoundingCircle c1{{1.5f, 0.0f}, 1.0f};
    const BoundingCircle c2{{3.0f, 0.0f}, 0.5f};
    check(intersects(c0, c1), "overlapping circles report intersect");
    check(!intersects(c0, c2), "separated circles report no intersect");
    check(intersects(c0, b), "circle vs AABB overlap detected");
    check(!intersects(c2, a), "circle vs AABB non-overlap detected");

    // Ray from the left, aimed +X, should hit the unit AABB's left face at x = -1.
    const Ray2D ray{{-5.0f, 0.0f}, {1.0f, 0.0f}};
    check(near(raycast(ray, a), 4.0f), "ray hits AABB at the expected distance");
    check(near(raycast(ray, c0), 4.0f), "ray hits circle at the expected distance");

    const Ray2D up{{-5.0f, 0.0f}, {0.0f, 1.0f}};
    check(raycast(up, a) < 0.0f, "ray pointing away misses");

    const Ray2D inside{{0.0f, 0.0f}, {1.0f, 0.0f}};
    check(near(raycast(inside, a), 0.0f), "ray originating inside hits at t = 0");
}

void testPrimitiveMeasures() {
    std::printf("Primitive measures\n");

    const Circle circle{2.0f};
    check(near(circle.area(), kPi * 4.0f), "circle area = pi r^2");
    check(near(circle.perimeter(), kTwoPi * 2.0f), "circle circumference = 2 pi r");

    const Rectangle rect{{1.5f, 0.5f}};   // 3 x 1
    check(near(rect.area(), 3.0f), "rectangle area = width * height");
    check(near(rect.perimeter(), 8.0f), "rectangle perimeter = 2(w + h)");
    check(rect.contains({1.0f, 0.0f}) && !rect.contains({2.0f, 0.0f}), "rectangle contains test");

    const Triangle2D tri{{0.0f, 0.0f}, {4.0f, 0.0f}, {0.0f, 3.0f}};
    check(near(tri.area(), 6.0f), "triangle area = 1/2 base * height");
    check(tri.contains({1.0f, 1.0f}) && !tri.contains({3.0f, 3.0f}), "triangle contains test");

    // A regular hexagon of circumradius 1: area = (3 sqrt 3 / 2) r^2.
    const RegularPolygon hex{1.0f, 6};
    check(near(hex.area(), 1.5f * std::sqrt(3.0f)), "regular hexagon area");

    // A rotated rectangle's AABB grows; a 90-degree turn swaps its extents.
    const Aabb2D turned = rect.aabb(Isometry2D::fromRotation(kHalfPi));
    check(near(turned.halfSize().x, 0.5f) && near(turned.halfSize().y, 1.5f),
          "rotated rectangle AABB swaps extents");
}

void testRandomSampling() {
    std::printf("Random sampling\n");
    Random rng{1337u};

    // Every sampled point must land inside its primitive, and the samples should
    // fill it: a Monte-Carlo area estimate (hits in a sub-region vs total) should
    // land near the analytic ratio.
    const Circle circle{1.0f};
    int inside = 0, upperRightQuadrant = 0;
    constexpr int kN = 40000;
    for (int i = 0; i < kN; ++i) {
        const Vec2 p = circle.sampleInterior(rng);
        if (circle.contains(p)) ++inside;
        if (p.x > 0.0f && p.y > 0.0f) ++upperRightQuadrant;
    }
    check(inside == kN, "every circle sample lands inside the circle");
    const f32 quadrantFraction = static_cast<f32>(upperRightQuadrant) / kN;
    check(near(quadrantFraction, 0.25f, 0.02f), "circle samples spread evenly across quadrants");

    // Triangle samples must all be inside, and the centroid of many samples must
    // approach the triangle's own centroid (uniform sampling has no bias).
    const Triangle2D tri{{0.0f, 0.0f}, {6.0f, 0.0f}, {0.0f, 6.0f}};
    Vec2 mean{0.0f, 0.0f};
    int  triInside = 0;
    for (int i = 0; i < kN; ++i) {
        const Vec2 p = tri.sampleInterior(rng);
        if (tri.contains(p)) ++triInside;
        mean += p;
    }
    mean = mean * (1.0f / kN);
    check(triInside == kN, "every triangle sample lands inside the triangle");
    check(near(mean.x, tri.centroid().x, 0.05f) && near(mean.y, tri.centroid().y, 0.05f),
          "triangle sample mean approaches its centroid");
}

// --- A game-defined primitive ---------------------------------------------------
// A five-pointed star. It knows nothing about the engine's shapes, yet by answering
// the same questions it satisfies Bounded2d and rides through boundingCirclesOverlap
// exactly like a Circle does.
struct Star {
    f32 outerRadius = 1.0f;
    f32 innerRadius = 0.5f;

    [[nodiscard]] Vec2 point(u32 i) const noexcept {
        const f32 r = (i % 2 == 0) ? outerRadius : innerRadius;
        return Vec2::fromAngle(kHalfPi + static_cast<f32>(i) * (kPi / 5.0f)) * r;
    }
    [[nodiscard]] Aabb2D aabb(Isometry2D iso = {}) const noexcept {
        Vec2 pts[10];
        for (u32 i = 0; i < 10; ++i) pts[i] = iso.apply(point(i));
        return Aabb2D::fromPoints(pts, 10);
    }
    [[nodiscard]] BoundingCircle boundingCircle(Isometry2D iso = {}) const noexcept {
        return {iso.translation, outerRadius};
    }
};

void testCustomPrimitive() {
    std::printf("Custom primitive\n");
    static_assert(Bounded2d<Star>, "Star must satisfy Bounded2d");

    const Star star;
    const Aabb2D box = star.aabb();
    check(near(box.max.y, 1.0f), "star's top outer point sets the AABB top");
    check(near(box.min.x, -box.max.x), "star AABB is symmetric about the vertical axis");
    check(box.min.y < 0.0f && box.min.y > -star.outerRadius,
          "star AABB bottom sits below centre but above the outer radius");

    // The custom Star and a stock Circle share one generic broad-phase.
    const Circle nearby{0.5f};
    const Circle faraway{0.5f};
    check(boundingCirclesOverlap(star, {}, nearby, Isometry2D::fromTranslation({1.0f, 0.0f})),
          "generic overlap sees the nearby circle");
    check(!boundingCirclesOverlap(star, {}, faraway, Isometry2D::fromTranslation({5.0f, 0.0f})),
          "generic overlap rejects the distant circle");
}

}

int main() {
    testBoundingIntersections();
    testPrimitiveMeasures();
    testRandomSampling();
    testCustomPrimitive();

    if (g_failures == 0) {
        std::printf("\nAll math primitive checks passed.\n");
        return 0;
    }
    std::printf("\n%d math primitive check(s) FAILED.\n", g_failures);
    return 1;
}
