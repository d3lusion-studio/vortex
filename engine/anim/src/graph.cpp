#include "vortex/anim/graph.hpp"

#include <algorithm>

namespace vortex::anim {

// ---------------------------------------------------------------------------
// BlendTree
// ---------------------------------------------------------------------------

BlendTree::NodeId BlendTree::addClip(const Clip* clip) {
    Node n;
    n.kind = Kind::Clip;
    n.player.play(clip);
    m_nodes.push_back(std::move(n));

    const NodeId id = static_cast<NodeId>(m_nodes.size() - 1);
    if (m_root == kInvalid) m_root = id;   // a one-node tree is a perfectly good tree
    return id;
}

BlendTree::NodeId BlendTree::addBlend(NodeId a, NodeId b, f32 weight) {
    Node n;
    n.kind   = Kind::Blend;
    n.a      = a;
    n.b      = b;
    n.weight = std::clamp(weight, 0.0f, 1.0f);
    m_nodes.push_back(std::move(n));
    return static_cast<NodeId>(m_nodes.size() - 1);
}

BlendTree::NodeId BlendTree::addMask(NodeId base, NodeId overlay, JointMask mask, f32 weight) {
    Node n;
    n.kind   = Kind::Mask;
    n.a      = base;
    n.b      = overlay;
    n.mask   = std::move(mask);
    n.weight = std::clamp(weight, 0.0f, 1.0f);
    m_nodes.push_back(std::move(n));
    return static_cast<NodeId>(m_nodes.size() - 1);
}

void BlendTree::setWeight(NodeId id, f32 weight) {
    if (id >= m_nodes.size()) return;
    m_nodes[id].weight = std::clamp(weight, 0.0f, 1.0f);
}

f32 BlendTree::weight(NodeId id) const {
    return id < m_nodes.size() ? m_nodes[id].weight : 0.0f;
}

void BlendTree::setClip(NodeId id, const Clip* clip, bool restart) {
    if (id >= m_nodes.size() || m_nodes[id].kind != Kind::Clip) return;
    m_nodes[id].player.play(clip, restart);
}

const Clip* BlendTree::clip(NodeId id) const {
    if (id >= m_nodes.size() || m_nodes[id].kind != Kind::Clip) return nullptr;
    return m_nodes[id].player.clip();
}

void BlendTree::setSpeed(NodeId id, f32 speed) {
    if (id >= m_nodes.size() || m_nodes[id].kind != Kind::Clip) return;
    m_nodes[id].player.speed = speed;
}

f32 BlendTree::time(NodeId id) const {
    if (id >= m_nodes.size() || m_nodes[id].kind != Kind::Clip) return 0.0f;
    return m_nodes[id].player.time();
}

void BlendTree::accumulateWeights(NodeId id, f32 inherited, std::vector<f32>& out) const {
    if (id >= m_nodes.size() || inherited <= 0.0f) return;
    const Node& n = m_nodes[id];

    switch (n.kind) {
        case Kind::Clip:
            out[id] += inherited;
            break;

        case Kind::Blend:
            accumulateWeights(n.a, inherited * (1.0f - n.weight), out);
            accumulateWeights(n.b, inherited * n.weight, out);
            break;

        case Kind::Mask: {
            // A mask does not split the pose in two, it splits it per JOINT — so there is no
            // single number for "how much of the result is the overlay". Its average weight is
            // the honest summary, and this is only used to decide whether a leaf is audible
            // enough to fire events.
            f32 average = 0.0f;
            if (!n.mask.weights.empty()) {
                for (const f32 w : n.mask.weights) average += w;
                average /= static_cast<f32>(n.mask.weights.size());
            }
            average *= n.weight;
            accumulateWeights(n.a, inherited * (1.0f - average), out);
            accumulateWeights(n.b, inherited * average, out);
            break;
        }
    }
}

void BlendTree::update(f32 dt) {
    m_fired.clear();

    for (Node& n : m_nodes)
        if (n.kind == Kind::Clip) n.player.update(dt);

    if (m_root == kInvalid) return;

    // Which leaves actually reach the root, and by how much. A clip faded out to nothing has
    // still been ticking — it must, or it would jump when faded back in — but it should not be
    // ringing footsteps at the player.
    std::vector<f32> effective(m_nodes.size(), 0.0f);
    accumulateWeights(m_root, 1.0f, effective);

    for (usize i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].kind != Kind::Clip) continue;
        if (effective[i] < eventWeightThreshold) continue;

        for (const Event* e : m_nodes[i].player.firedEvents())
            m_fired.push_back({e, static_cast<NodeId>(i), effective[i]});
    }
}

void BlendTree::evalNode(NodeId id, const Skeleton& skeleton, Pose& out) const {
    if (id >= m_nodes.size()) {
        out = skeleton.bindPose();
        return;
    }
    const Node& n = m_nodes[id];

    switch (n.kind) {
        case Kind::Clip:
            n.player.pose(skeleton, out);
            return;

        case Kind::Blend: {
            // Short-circuit the ends. It is not only faster: a fully-faded blend then produces
            // its input bit for bit, so a crossfade that has finished is indistinguishable from
            // never having happened.
            if (n.weight <= 0.0f) { evalNode(n.a, skeleton, out); return; }
            if (n.weight >= 1.0f) { evalNode(n.b, skeleton, out); return; }

            Pose a, b;
            evalNode(n.a, skeleton, a);
            evalNode(n.b, skeleton, b);
            blend(a, b, n.weight, out);
            return;
        }

        case Kind::Mask: {
            if (n.weight <= 0.0f) { evalNode(n.a, skeleton, out); return; }

            Pose base, overlay;
            evalNode(n.a, skeleton, base);
            evalNode(n.b, skeleton, overlay);
            blendMasked(base, overlay, n.weight, n.mask, out);
            return;
        }
    }
}

void BlendTree::evaluate(const Skeleton& skeleton, Pose& out) const {
    if (m_root == kInvalid) {
        out = skeleton.bindPose();
        return;
    }
    evalNode(m_root, skeleton, out);
}

// ---------------------------------------------------------------------------
// CrossFade
// ---------------------------------------------------------------------------

void CrossFade::ensureBuilt() {
    if (m_built) return;
    m_from  = m_tree.addClip(nullptr);
    m_to    = m_tree.addClip(nullptr);
    m_blend = m_tree.addBlend(m_from, m_to, 1.0f);
    m_tree.setRoot(m_blend);
    m_built = true;
}

void CrossFade::play(const Clip* clip, f32 fadeTime) {
    ensureBuilt();
    if (clip == nullptr || clip == m_target) return;   // already going there: do not restart it

    // Whatever is playing becomes the thing we fade FROM: the `to` clip is demoted to `from`.
    // It is NOT restarted — the outgoing animation must carry on from where it was, or it
    // visibly rewinds as it leaves, which is exactly as ugly as the snap this is here to avoid.
    //
    // Interrupting a fade with another fade drops the oldest of the three poses. That is the
    // price of two leaves rather than an unbounded stack of them, and it never snaps.
    const Clip* previous = m_tree.clip(m_to);
    m_tree.setClip(m_from, previous, /*restart=*/false);

    m_tree.setClip(m_to, clip, /*restart=*/true);
    m_target = clip;

    if (previous == nullptr || fadeTime <= 0.0f) {
        m_t      = 1.0f;    // nothing to fade from
        m_fading = false;
    } else {
        m_t        = 0.0f;
        m_fadeTime = fadeTime;
        m_fading   = true;
    }
    m_tree.setWeight(m_blend, m_t);
}

void CrossFade::update(f32 dt) {
    ensureBuilt();

    if (m_fading) {
        m_t += (m_fadeTime > 0.0f) ? dt / m_fadeTime : 1.0f;
        if (m_t >= 1.0f) {
            m_t      = 1.0f;
            m_fading = false;
            // The outgoing clip is done contributing. Let it go, so it stops being ticked and
            // stops being a leaf the event walk has to consider.
            m_tree.setClip(m_from, nullptr, true);
        }
        m_tree.setWeight(m_blend, m_t);
    }

    m_tree.update(dt);
}

void CrossFade::pose(const Skeleton& skeleton, Pose& out) const {
    m_tree.evaluate(skeleton, out);
}

const Clip* CrossFade::current() const { return m_target; }

}
