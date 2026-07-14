#include "vortex/anim/pose.hpp"

#include <algorithm>

namespace vortex::anim {

namespace {

Vec3 lerp3(Vec3 a, Vec3 b, f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

Transform mix(const Transform& a, const Transform& b, f32 t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    return {lerp3(a.translation, b.translation, t),
            Quat::slerp(a.rotation, b.rotation, t),
            lerp3(a.scale, b.scale, t)};
}

} // namespace

void blend(const Pose& a, const Pose& b, f32 t, Pose& out) {
    const usize n = std::min(a.size(), b.size());
    out.resize(n);
    for (usize i = 0; i < n; ++i) out[i] = mix(a[i], b[i], t);
}

void blendMasked(const Pose& a, const Pose& b, f32 t, const JointMask& mask, Pose& out) {
    const usize n = std::min(a.size(), b.size());
    out.resize(n);
    for (usize i = 0; i < n; ++i)
        out[i] = mix(a[i], b[i], t * mask.at(i));
}

JointMask JointMask::all(const Skeleton& skeleton, f32 weight) {
    JointMask m;
    m.weights.assign(skeleton.size(), weight);
    return m;
}

JointMask JointMask::subtree(const Skeleton& skeleton, i32 root, f32 weight) {
    JointMask m;
    m.weights.assign(skeleton.size(), 0.0f);
    if (root < 0 || static_cast<usize>(root) >= skeleton.size()) return m;

    m.weights[static_cast<usize>(root)] = weight;

    // Joints are stored parents-first, so one forward sweep is enough: by the time a joint is
    // reached, its parent's mark is final. No recursion, no second pass.
    for (usize i = static_cast<usize>(root) + 1; i < skeleton.size(); ++i) {
        const i32 parent = skeleton.joints[i].parent;
        if (parent >= 0 && m.weights[static_cast<usize>(parent)] > 0.0f)
            m.weights[i] = weight;
    }
    return m;
}

JointMask JointMask::subtree(const Skeleton& skeleton, std::string_view rootName, f32 weight) {
    return subtree(skeleton, skeleton.find(rootName), weight);
}

}
