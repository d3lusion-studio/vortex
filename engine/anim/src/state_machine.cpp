#include "vortex/anim/state_machine.hpp"

#include <algorithm>

namespace vortex::anim {

namespace {

template <typename Params>
auto* findParam(Params& params, std::string_view name) {
    for (auto& p : params)
        if (p.name == name) return &p;
    return static_cast<decltype(&params.front())>(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

StateMachine::StateId StateMachine::addState(std::string name, const Clip* clip, f32 speed) {
    m_states.push_back({std::move(name), clip, speed});
    const StateId id = static_cast<StateId>(m_states.size() - 1);

    // The first state is the starting state — a machine with states should never be in
    // none of them, and making the caller remember a separate "set initial" call is how
    // T-poses ship.
    if (m_current == kInvalid) beginFade(id, 0.0f);
    return id;
}

StateMachine::StateId StateMachine::find(std::string_view name) const {
    for (usize i = 0; i < m_states.size(); ++i)
        if (m_states[i].name == name) return static_cast<StateId>(i);
    return kInvalid;
}

void StateMachine::addTransition(StateId from, Transition t) {
    if (t.to >= m_states.size()) return;
    if (from != kAny && from >= m_states.size()) return;
    m_transitions.push_back({from, std::move(t)});
}

const std::string& StateMachine::stateName(StateId id) const {
    static const std::string kNone = "<none>";
    return id < m_states.size() ? m_states[id].name : kNone;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

void StateMachine::setFloat(std::string_view name, f32 value) {
    if (auto* p = findParam(m_floats, name)) { p->value = value; return; }
    m_floats.push_back({std::string(name), value});
}

void StateMachine::setBool(std::string_view name, bool value) {
    if (auto* p = findParam(m_bools, name)) { p->value = value; return; }
    m_bools.push_back({std::string(name), value});
}

f32 StateMachine::getFloat(std::string_view name) const {
    const auto* p = findParam(m_floats, name);
    return p != nullptr ? p->value : 0.0f;
}

bool StateMachine::getBool(std::string_view name) const {
    const auto* p = findParam(m_bools, name);
    return p != nullptr && p->value;
}

void StateMachine::trigger(std::string_view name) {
    for (const std::string& t : m_triggers)
        if (t == name) return;   // raised is raised; there is no "raised twice"
    m_triggers.emplace_back(name);
}

void StateMachine::resetTrigger(std::string_view name) {
    std::erase_if(m_triggers, [&](const std::string& t) { return t == name; });
}

// ---------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------

f32 StateMachine::normalizedTime() const {
    const Clip* clip = m_to.clip();
    if (clip == nullptr || clip->duration <= 0.0f) return 0.0f;
    if (m_to.finished()) return 1.0f;
    return std::clamp(m_to.time() / clip->duration, 0.0f, 1.0f);
}

bool StateMachine::passes(const TransitionFrom& tf) const {
    // Exit time gates on the current state's clock — on a kAny transition that means "this
    // far through whatever is playing", which still reads sensibly.
    if (tf.t.exitTime >= 0.0f && normalizedTime() < tf.t.exitTime) return false;

    // An unconditional transition needs SOMETHING to wait for, or entering the state and
    // leaving it happen on the same frame.
    if (tf.t.conditions.empty() && tf.t.exitTime < 0.0f) return false;

    for (const Condition& c : tf.t.conditions) {
        switch (c.op) {
            case Condition::Op::Greater:
                if (!(getFloat(c.name) > c.threshold)) return false;
                break;
            case Condition::Op::Less:
                if (!(getFloat(c.name) < c.threshold)) return false;
                break;
            case Condition::Op::IsTrue:
                if (!getBool(c.name)) return false;
                break;
            case Condition::Op::IsFalse:
                if (getBool(c.name)) return false;
                break;
            case Condition::Op::Triggered: {
                bool raised = false;
                for (const std::string& t : m_triggers) raised = raised || t == c.name;
                if (!raised) return false;
                break;
            }
        }
    }
    return true;
}

void StateMachine::consumeTriggers(const Transition& t) {
    for (const Condition& c : t.conditions)
        if (c.op == Condition::Op::Triggered) resetTrigger(c.name);
}

void StateMachine::checkTransitions() {
    // kAny first: "get hit" outranks whatever the current state had planned. Within each
    // group, the order they were added is the priority — first match wins.
    for (const TransitionFrom& tf : m_transitions) {
        if (tf.from != kAny) continue;
        if (tf.t.to == m_current) continue;   // kAny must not re-enter its own state forever
        if (!passes(tf)) continue;
        consumeTriggers(tf.t);
        beginFade(tf.t.to, tf.t.fadeTime);
        return;
    }
    for (const TransitionFrom& tf : m_transitions) {
        if (tf.from != m_current) continue;
        if (!passes(tf)) continue;
        consumeTriggers(tf.t);
        beginFade(tf.t.to, tf.t.fadeTime);
        return;
    }
}

void StateMachine::beginFade(StateId to, f32 fadeTime) {
    if (to >= m_states.size()) return;

    // The current state becomes the thing we fade FROM, clock and all — the outgoing
    // animation must carry on from where it was, or it visibly rewinds as it leaves.
    // Interrupting a fade drops the oldest of the three poses; see the class comment.
    const Clip* previous = m_to.clip();
    m_from     = m_to;
    m_previous = m_current;
    m_current  = to;

    const State& s = m_states[to];
    m_to.play(s.clip, /*restart=*/true);
    m_to.speed           = s.speed;
    m_to.rootMotionJoint = m_rootJoint;

    if (previous == nullptr || fadeTime <= 0.0f) {
        m_weight = 1.0f;
        m_fading = false;
        m_from.stop();
    } else {
        m_weight   = 0.0f;
        m_fadeTime = fadeTime;
        m_fading   = true;
    }
}

void StateMachine::play(StateId id, f32 fadeTime) {
    if (id >= m_states.size() || id == m_current) return;
    beginFade(id, fadeTime);
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void StateMachine::setRootMotionJoint(i32 joint) {
    m_rootJoint            = joint;
    m_from.rootMotionJoint = joint;
    m_to.rootMotionJoint   = joint;
}

void StateMachine::update(f32 dt) {
    m_fired.clear();
    m_rootMotion = {};
    if (m_current == kInvalid) return;

    if (m_fading) {
        m_weight += (m_fadeTime > 0.0f) ? dt / m_fadeTime : 1.0f;
        if (m_weight >= 1.0f) {
            m_weight = 1.0f;
            m_fading = false;
            m_from.stop();   // done contributing: stop ticking it
        }
    }

    m_from.update(dt);
    m_to.update(dt);

    // Events, weighted the way BlendTree weights its leaves: a state faded to background
    // noise should not be ringing footsteps.
    const f32 fromWeight = m_fading ? 1.0f - m_weight : 0.0f;
    const f32 toWeight   = m_fading ? m_weight : 1.0f;
    if (fromWeight >= eventWeightThreshold)
        for (const Event* e : m_from.firedEvents()) m_fired.push_back({e, m_previous, fromWeight});
    if (toWeight >= eventWeightThreshold)
        for (const Event* e : m_to.firedEvents()) m_fired.push_back({e, m_current, toWeight});

    // Root motion blends by the same weight as the pose, so a walk fading into a run speeds
    // up smoothly across the fade rather than stepping on the frame the fade ends.
    if (m_rootJoint >= 0) {
        if (m_fading) {
            const RootMotion& a = m_from.rootMotion();
            const RootMotion& b = m_to.rootMotion();
            m_rootMotion.translation = a.translation + (b.translation - a.translation) * m_weight;
            m_rootMotion.rotation    = Quat::slerp(a.rotation, b.rotation, m_weight);
        } else {
            m_rootMotion = m_to.rootMotion();
        }
    }

    // Last, so exit times see this frame's clock, and this frame's events keep the state
    // they were fired from.
    checkTransitions();
}

void StateMachine::evaluate(const Skeleton& skeleton, Pose& out) const {
    if (!m_fading || m_weight >= 1.0f) {
        m_to.pose(skeleton, out);
        return;
    }
    if (m_weight <= 0.0f) {
        m_from.pose(skeleton, out);
        return;
    }
    Pose a, b;
    m_from.pose(skeleton, a);
    m_to.pose(skeleton, b);
    blend(a, b, m_weight, out);
}

}
