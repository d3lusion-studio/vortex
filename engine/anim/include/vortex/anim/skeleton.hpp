#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/quat.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"

#include <string>
#include <vector>

namespace vortex::anim {

// A joint's pose, as the three things an animation actually keys. Kept as TRS rather than
// a matrix because that is the only form you can interpolate correctly: blending two
// matrices halfway through a rotation shears the mesh, blending two quaternions does not.
struct Transform {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] Mat4 matrix() const {
        return Mat4::translation(translation.x, translation.y, translation.z) *
               rotation.toMat4() *
               Mat4::scaling(scale.x, scale.y, scale.z);
    }
};

struct Joint {
    std::string name;
    // Index into Skeleton::joints, or -1 for a root. Joints are stored parents-first, so a
    // single forward walk can compute every global transform without recursion.
    i32  parent = -1;
    // The transform that takes the mesh from its bind pose into this joint's space. It is
    // what makes skinning work: a vertex is moved by (joint's pose) * (inverse bind), so a
    // joint that has not moved from its bind pose leaves its vertices exactly where they
    // were.
    Mat4 inverseBind = Mat4::identity();
    // The joint's pose in the model's rest state, used when an animation does not key it.
    Transform bindPose;
};

class Skeleton {
public:
    std::vector<Joint> joints;

    [[nodiscard]] usize size() const { return joints.size(); }
    [[nodiscard]] bool  empty() const { return joints.empty(); }

    // Index of a joint by name, or -1. Linear — this is for setup, not for per-frame use.
    [[nodiscard]] i32 find(std::string_view name) const;

    // Local poses -> the skinning matrices the vertex shader multiplies by. `locals` is one
    // Transform per joint; `out` is resized to match.
    //
    // Two steps in one walk: accumulate each joint's global transform from its parent's
    // (possible in a single pass because parents come first), then fold in the inverse bind.
    void computeSkinningMatrices(const std::vector<Transform>& locals,
                                 std::vector<Mat4>& out) const;

    // The rest pose, as a starting point for an animation that keys only some joints.
    [[nodiscard]] std::vector<Transform> bindPose() const;
};

}
