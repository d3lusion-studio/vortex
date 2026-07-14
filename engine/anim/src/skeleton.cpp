#include "vortex/anim/skeleton.hpp"

namespace vortex::anim {

i32 Skeleton::find(std::string_view name) const {
    for (usize i = 0; i < joints.size(); ++i)
        if (joints[i].name == name) return static_cast<i32>(i);
    return -1;
}

std::vector<Transform> Skeleton::bindPose() const {
    std::vector<Transform> out(joints.size());
    for (usize i = 0; i < joints.size(); ++i) out[i] = joints[i].bindPose;
    return out;
}

void Skeleton::computeSkinningMatrices(const std::vector<Transform>& locals,
                                       std::vector<Mat4>& out) const {
    const usize n = joints.size();
    out.resize(n);

    // Globals are accumulated in place, then overwritten by the skinning matrices. Two
    // arrays would be clearer; one is enough only because a joint's parent is always
    // finalised before the joint itself, so the value being read is never a value that is
    // about to change.
    std::vector<Mat4> global(n);

    for (usize i = 0; i < n; ++i) {
        const Mat4 local = (i < locals.size() ? locals[i] : joints[i].bindPose).matrix();
        const i32  parent = joints[i].parent;
        global[i] = (parent >= 0) ? global[static_cast<usize>(parent)] * local : local;
    }

    // The skinning matrix: undo the bind pose, then apply the animated pose. A joint sitting
    // exactly where it was bound produces the identity here, which is why an unanimated mesh
    // comes out unmoved rather than collapsed to the origin.
    for (usize i = 0; i < n; ++i)
        out[i] = global[i] * joints[i].inverseBind;
}

}
