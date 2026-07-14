#pragma once
#include "vortex/anim/clip.hpp"
#include "vortex/anim/pose.hpp"
#include "vortex/anim/skeleton.hpp"
#include "vortex/core/types.hpp"

#include <string>
#include <vector>

namespace vortex::anim {

// A blend tree: the shape animation actually has.
//
// A character is rarely "playing an animation". It is playing a walk crossfading into a run,
// with a wave layered onto the upper body, while a hit reaction fades out underneath. Each of
// those is a two-input blend, and stacking them is a tree — so that is what this is, rather
// than a list of clips with a winner.
//
// Three node kinds, which is enough for all of the above:
//   Clip  — a leaf. Owns its own clock, so two nodes can play the same clip at different times.
//   Blend — lerp between two children by a weight. Crossfading is animating that weight.
//   Mask  — take `base`, and overwrite the masked joints with `overlay`. This is the one that
//           lets the legs walk while the arms do something else.
//
// Evaluation is one recursive walk producing a Pose. Events come from the clip leaves, but
// only from those actually contributing — a clip faded to nothing should not fire footsteps.
class BlendTree {
public:
    using NodeId = u32;
    static constexpr NodeId kInvalid = 0xFFFFFFFFu;

    // A leaf. `speed` and looping come from the clip; the node owns the clock.
    [[nodiscard]] NodeId addClip(const Clip* clip);

    // out = lerp(a, b, weight). weight 0 is all `a`.
    [[nodiscard]] NodeId addBlend(NodeId a, NodeId b, f32 weight = 0.0f);

    // out = base, with the mask's joints taken from `overlay` (scaled by `weight` and by the
    // mask's own per-joint value).
    [[nodiscard]] NodeId addMask(NodeId base, NodeId overlay, JointMask mask, f32 weight = 1.0f);

    void setRoot(NodeId id) { m_root = id; }
    [[nodiscard]] NodeId root() const { return m_root; }

    // Set a blend/mask node's weight. This is the knob a crossfade turns.
    void setWeight(NodeId, f32 weight);
    [[nodiscard]] f32 weight(NodeId) const;

    // Point a clip leaf at a different clip. Restarting its clock is usually what you want
    // when the clip changes; not restarting is how you swap a clip mid-stride.
    void setClip(NodeId, const Clip*, bool restart = true);
    [[nodiscard]] const Clip* clip(NodeId) const;

    void setSpeed(NodeId, f32 speed);
    [[nodiscard]] f32 time(NodeId) const;

    // Advance every clip leaf's clock, and collect the events they crossed.
    void update(f32 dt);

    // Evaluate the tree into `out`. Joints no clip keys keep the skeleton's rest pose.
    void evaluate(const Skeleton&, Pose& out) const;

    // Events crossed by the last update(), from leaves whose contribution to the root was at
    // least `eventWeightThreshold`. A clip blended down to 5% is background detail; it should
    // not be ringing footsteps.
    struct Fired {
        const Event* event  = nullptr;
        NodeId       node   = kInvalid;
        f32          weight = 0.0f;   // the leaf's effective contribution to the root
    };
    [[nodiscard]] const std::vector<Fired>& firedEvents() const { return m_fired; }

    f32 eventWeightThreshold = 0.5f;

private:
    enum class Kind : u8 { Clip, Blend, Mask };

    struct Node {
        Kind        kind = Kind::Clip;
        // Clip
        Player      player;
        // Blend / Mask
        NodeId      a = kInvalid;      // Blend: first input.  Mask: base.
        NodeId      b = kInvalid;      // Blend: second input. Mask: overlay.
        f32         weight = 0.0f;
        JointMask   mask;
    };

    void evalNode(NodeId, const Skeleton&, Pose& out) const;
    // Walk the tree accumulating how much of the root's pose each leaf actually accounts for.
    void accumulateWeights(NodeId, f32 inherited, std::vector<f32>& out) const;

    std::vector<Node>  m_nodes;
    NodeId             m_root = kInvalid;
    std::vector<Fired> m_fired;
};

// The common case, wrapped: play one clip, and when you play another, fade to it over
// `fadeTime` instead of snapping. That snap is the single most visible animation defect there
// is, and every game needs this, so it should not have to be rebuilt each time.
//
// It is a BlendTree with two leaves and one blend — nothing more. Reach for the tree directly
// when you need a third layer.
class CrossFade {
public:
    void setSkeleton(const Skeleton* skeleton) { m_skeleton = skeleton; }

    // Fade to `clip` over `fadeTime` seconds. Playing the clip already playing does nothing,
    // so calling this every frame while a key is held is safe.
    void play(const Clip* clip, f32 fadeTime = 0.25f);

    void update(f32 dt);
    void pose(const Skeleton&, Pose& out) const;

    [[nodiscard]] const Clip* current() const;
    [[nodiscard]] bool fading() const { return m_fading; }
    [[nodiscard]] f32  fadeProgress() const { return m_t; }
    [[nodiscard]] const std::vector<BlendTree::Fired>& firedEvents() const {
        return m_tree.firedEvents();
    }

    [[nodiscard]] BlendTree& tree() { return m_tree; }

private:
    void ensureBuilt();

    BlendTree        m_tree;
    BlendTree::NodeId m_from  = BlendTree::kInvalid;
    BlendTree::NodeId m_to    = BlendTree::kInvalid;
    BlendTree::NodeId m_blend = BlendTree::kInvalid;

    const Skeleton* m_skeleton = nullptr;
    const Clip*     m_target   = nullptr;
    f32             m_t        = 1.0f;   // blend weight: 1 means fully on `m_to`
    f32             m_fadeTime = 0.0f;
    bool            m_fading   = false;
    bool            m_built    = false;
};

}
