#pragma once
#include "vortex/anim/skeleton.hpp"
#include "vortex/core/types.hpp"

#include <vector>

namespace vortex::anim {

// A pose is one local Transform per joint — the thing clips produce and skeletons consume.
// Blending happens HERE, on local TRS, and never on the skinning matrices: those are global
// and already have the inverse bind folded in, so averaging two of them averages two points
// in space rather than two rotations, and the mesh shears and shrinks.
using Pose = std::vector<Transform>;

// Linear blend, joint by joint. `t` = 0 is all `a`, 1 is all `b`. Rotations slerp — see
// Quat::slerp for why lerping them is not merely less accurate but wrong.
void blend(const Pose& a, const Pose& b, f32 t, Pose& out);

// Per-joint weights, 0..1. This is what lets a character wave while it walks: the upper body
// takes its pose from one clip, the legs from another, and the two are the same character.
struct JointMask {
    std::vector<f32> weights;

    [[nodiscard]] f32 at(usize joint) const {
        return joint < weights.size() ? weights[joint] : 0.0f;
    }

    // Every joint at `weight`.
    [[nodiscard]] static JointMask all(const Skeleton&, f32 weight = 1.0f);

    // `root` and everything below it at `weight`, the rest at 0. This is how an upper-body
    // mask is actually built: name the spine, get the arms and head for free — because a
    // skeleton already knows what hangs off what, and listing the joints by hand is a list
    // that goes stale the moment the rig changes.
    [[nodiscard]] static JointMask subtree(const Skeleton&, i32 root, f32 weight = 1.0f);
    [[nodiscard]] static JointMask subtree(const Skeleton&, std::string_view rootName,
                                           f32 weight = 1.0f);
};

// Blend, but scaled per joint by the mask: joint i is blended by `t * mask[i]`. A joint the
// mask zeroes keeps `a` exactly.
void blendMasked(const Pose& a, const Pose& b, f32 t, const JointMask& mask, Pose& out);

}
