#pragma once
#include "vortex/core/math/bounds2d.hpp"
#include "vortex/core/math/random.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"

#include <cmath>
#include <concepts>

// A small library of 2D geometric primitives. Each shape is defined in its own
// local space (origin-centred where that makes sense) and answers the same handful
// of questions: how big is it (area/perimeter), is a point inside it, what bounding
// volume encloses it under a given pose, and give me a uniformly random point in it.
//
// The uniform pattern is deliberate — it is what lets the concepts below treat an
// engine primitive and a game's own custom shape identically. Implement the same
// members on your type and every generic algorithm here accepts it (see the
// Measured2d / Bounded2d concepts and math_primitives example).

namespace vortex {

// ------------------------------------------------------------------- Circle

struct Circle {
    f32 radius = 0.5f;

    [[nodiscard]] f32 area()      const noexcept { return kPi * radius * radius; }
    [[nodiscard]] f32 perimeter() const noexcept { return kTwoPi * radius; }

    [[nodiscard]] bool contains(Vec2 localPoint) const noexcept {
        return lengthSquared(localPoint) <= radius * radius;
    }

    [[nodiscard]] Aabb2D aabb(Isometry2D iso = {}) const noexcept {
        return Aabb2D::fromCenterHalf(iso.translation, {radius, radius});   // rotation-invariant
    }
    [[nodiscard]] BoundingCircle boundingCircle(Isometry2D iso = {}) const noexcept {
        return {iso.translation, radius};
    }

    [[nodiscard]] Vec2 sampleInterior(Random& rng) const noexcept {
        return rng.insideUnitCircle() * radius;
    }
    [[nodiscard]] Vec2 sampleBoundary(Random& rng) const noexcept {
        return rng.onUnitCircle() * radius;
    }
};

// ------------------------------------------------------------------- Rectangle
// Origin-centred, described by its half-extents (Bevy's convention), so a unit
// square is halfSize = {0.5, 0.5}.

struct Rectangle {
    Vec2 halfSize{0.5f, 0.5f};

    [[nodiscard]] f32 area()      const noexcept { return 4.0f * halfSize.x * halfSize.y; }
    [[nodiscard]] f32 perimeter() const noexcept { return 4.0f * (halfSize.x + halfSize.y); }

    [[nodiscard]] bool contains(Vec2 localPoint) const noexcept {
        return std::fabs(localPoint.x) <= halfSize.x && std::fabs(localPoint.y) <= halfSize.y;
    }

    [[nodiscard]] Aabb2D aabb(Isometry2D iso = {}) const noexcept {
        const Vec2 corners[4] = {
            iso.apply({-halfSize.x, -halfSize.y}), iso.apply({halfSize.x, -halfSize.y}),
            iso.apply({halfSize.x, halfSize.y}),   iso.apply({-halfSize.x, halfSize.y})};
        return Aabb2D::fromPoints(corners, 4);
    }
    [[nodiscard]] BoundingCircle boundingCircle(Isometry2D iso = {}) const noexcept {
        return {iso.translation, length(halfSize)};
    }

    [[nodiscard]] Vec2 sampleInterior(Random& rng) const noexcept {
        return {rng.range(-halfSize.x, halfSize.x), rng.range(-halfSize.y, halfSize.y)};
    }
};

// ------------------------------------------------------------------- Triangle
// Three explicit vertices; the primitive that carries its own coordinates rather
// than sitting at the origin.

struct Triangle2D {
    Vec2 a{0.0f, 0.5f}, b{-0.5f, -0.5f}, c{0.5f, -0.5f};

    [[nodiscard]] f32 area() const noexcept {
        return std::fabs(cross(b - a, c - a)) * 0.5f;
    }
    [[nodiscard]] f32 perimeter() const noexcept {
        return distance(a, b) + distance(b, c) + distance(c, a);
    }
    [[nodiscard]] Vec2 centroid() const noexcept { return (a + b + c) * (1.0f / 3.0f); }

    // Inside if the point is on the same side of all three edges. Winding-agnostic:
    // a point that produces no mix of strictly-positive and strictly-negative
    // edge signs is inside (boundary counts as inside).
    [[nodiscard]] bool contains(Vec2 p) const noexcept {
        const f32  d1      = cross(b - a, p - a);
        const f32  d2      = cross(c - b, p - b);
        const f32  d3      = cross(a - c, p - c);
        const bool hasNeg  = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
        const bool hasPos  = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
        return !(hasNeg && hasPos);
    }

    [[nodiscard]] Aabb2D aabb(Isometry2D iso = {}) const noexcept {
        const Vec2 v[3] = {iso.apply(a), iso.apply(b), iso.apply(c)};
        return Aabb2D::fromPoints(v, 3);
    }
    [[nodiscard]] BoundingCircle boundingCircle(Isometry2D iso = {}) const noexcept {
        const Vec2 v[3] = {iso.apply(a), iso.apply(b), iso.apply(c)};
        return BoundingCircle::fromPoints(v, 3);
    }

    // Uniform over the area. Sample the parallelogram spanned by two edges and fold
    // the far half back across the diagonal into the triangle.
    [[nodiscard]] Vec2 sampleInterior(Random& rng) const noexcept {
        f32 u = rng.nextFloat();
        f32 v = rng.nextFloat();
        if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
        return a + (b - a) * u + (c - a) * v;
    }
};

// ------------------------------------------------------------------- Segment

struct Segment2D {
    Vec2 a{-0.5f, 0.0f}, b{0.5f, 0.0f};

    [[nodiscard]] f32  length() const noexcept { return distance(a, b); }
    [[nodiscard]] Vec2 at(f32 t) const noexcept { return lerp(a, b, t); }

    [[nodiscard]] Vec2 closestPoint(Vec2 p) const noexcept {
        const Vec2 ab   = b - a;
        const f32  len2 = lengthSquared(ab);
        if (len2 <= kEpsilon) return a;
        return a + ab * saturate(dot(p - a, ab) / len2);
    }

    [[nodiscard]] Vec2 sampleInterior(Random& rng) const noexcept { return at(rng.nextFloat()); }
};

// ------------------------------------------------------------------- RegularPolygon
// `sides` vertices on a circle of `circumradius`, the first pointing straight up.

struct RegularPolygon {
    f32 circumradius = 0.5f;
    u32 sides        = 6;

    [[nodiscard]] Vec2 vertex(u32 i) const noexcept {
        const f32 step = kTwoPi / static_cast<f32>(sides);
        return Vec2::fromAngle(kHalfPi + static_cast<f32>(i) * step) * circumradius;
    }

    [[nodiscard]] f32 area() const noexcept {
        const f32 n = static_cast<f32>(sides);
        return 0.5f * n * circumradius * circumradius * std::sin(kTwoPi / n);
    }
    [[nodiscard]] f32 perimeter() const noexcept {
        const f32 n = static_cast<f32>(sides);
        return 2.0f * n * circumradius * std::sin(kPi / n);
    }

    [[nodiscard]] Aabb2D aabb(Isometry2D iso = {}) const noexcept {
        Aabb2D box{iso.apply(vertex(0)), iso.apply(vertex(0))};
        for (u32 i = 1; i < sides; ++i) {
            const Vec2 p = iso.apply(vertex(i));
            box.min = {p.x < box.min.x ? p.x : box.min.x, p.y < box.min.y ? p.y : box.min.y};
            box.max = {p.x > box.max.x ? p.x : box.max.x, p.y > box.max.y ? p.y : box.max.y};
        }
        return box;
    }
    [[nodiscard]] BoundingCircle boundingCircle(Isometry2D iso = {}) const noexcept {
        return {iso.translation, circumradius};
    }

    // Uniform over the area: pick one of the `sides` wedges by area (they are equal),
    // then sample that triangle (centre, vertex i, vertex i+1).
    [[nodiscard]] Vec2 sampleInterior(Random& rng) const noexcept {
        const u32 i = rng.nextBounded(sides);
        return Triangle2D{{0.0f, 0.0f}, vertex(i), vertex((i + 1) % sides)}.sampleInterior(rng);
    }
};

// ------------------------------------------------------------------- Capsule
// A vertical stadium: a segment along local Y of half-length `halfLength`, thickened
// by `radius` and rounded at both ends.

struct Capsule2D {
    f32 radius     = 0.25f;
    f32 halfLength = 0.5f;

    [[nodiscard]] f32 area() const noexcept {
        return kPi * radius * radius + 4.0f * radius * halfLength;   // two half-discs + core box
    }
    [[nodiscard]] f32 perimeter() const noexcept {
        return kTwoPi * radius + 4.0f * halfLength;
    }

    [[nodiscard]] bool contains(Vec2 p) const noexcept {
        const Vec2 closest{0.0f, clamp(p.y, -halfLength, halfLength)};
        return lengthSquared(p - closest) <= radius * radius;
    }

    [[nodiscard]] Aabb2D aabb(Isometry2D iso = {}) const noexcept {
        const Vec2 half{radius, halfLength + radius};
        const Vec2 corners[4] = {iso.apply({-half.x, -half.y}), iso.apply({half.x, -half.y}),
                                 iso.apply({half.x, half.y}),   iso.apply({-half.x, half.y})};
        return Aabb2D::fromPoints(corners, 4);
    }
    [[nodiscard]] BoundingCircle boundingCircle(Isometry2D iso = {}) const noexcept {
        return {iso.translation, halfLength + radius};
    }
};

// ------------------------------------------------------------------- Traits
//
// The contract the primitives above share, as C++20 concepts. Constrain a generic
// algorithm on these and it accepts any type — engine or game-defined — that answers
// the same questions. This is the "custom primitive" story: no base class, no
// registration, just the right members.

template <class T>
concept Measured2d = requires(const T& s) {
    { s.area() }      -> std::convertible_to<f32>;
    { s.perimeter() } -> std::convertible_to<f32>;
};

template <class T>
concept Bounded2d = requires(const T& s, Isometry2D iso) {
    { s.aabb(iso) }           -> std::same_as<Aabb2D>;
    { s.boundingCircle(iso) } -> std::same_as<BoundingCircle>;
};

template <class T>
concept Samplable2d = requires(const T& s, Random& rng) {
    { s.sampleInterior(rng) } -> std::same_as<Vec2>;
};

// Do two posed shapes' bounding circles overlap? A cheap, rotation-invariant broad
// phase that works for any Bounded2d types, including a caller's own.
template <class A, class B>
    requires Bounded2d<A> && Bounded2d<B>
[[nodiscard]] inline bool boundingCirclesOverlap(const A& a, Isometry2D ia,
                                                 const B& b, Isometry2D ib) noexcept {
    return intersects(a.boundingCircle(ia), b.boundingCircle(ib));
}

}
