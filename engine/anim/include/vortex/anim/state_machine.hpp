#pragma once
#include "vortex/anim/clip.hpp"
#include "vortex/anim/pose.hpp"
#include "vortex/anim/skeleton.hpp"
#include "vortex/core/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace vortex::anim {

// The layer gameplay actually talks to.
//
// Gameplay does not think in clips and fade weights; it thinks "the character IS running,
// and when the jump button is pressed it jumps". A state machine is that sentence made
// executable: named states, each playing a clip, and transitions that fire when conditions
// on a small set of parameters hold. Gameplay's entire job shrinks to keeping the
// parameters honest — `setFloat("speed", length(velocity))`, `trigger("jump")` — and the
// machine decides what plays and crossfades to it.
//
// Deliberately two poses wide: the outgoing state and the incoming one, blended by the fade.
// A transition that interrupts a fade demotes the incoming state to outgoing mid-stride —
// the third pose is dropped, exactly the CrossFade trade-off, and it never snaps. Anything
// wider (a wave layered on top, an aim offset) belongs to BlendTree/JointMask, layered on
// the pose this produces — not built into the machine.
class StateMachine {
public:
    using StateId = u32;
    static constexpr StateId kInvalid = 0xFFFFFFFFu;
    // A transition from `kAny` is checked whatever state is current: the "get hit", "die",
    // "jump" kind, which would otherwise have to be copied onto every state and would go
    // stale the first time a state is added.
    static constexpr StateId kAny     = 0xFFFFFFFEu;

    // The first state added is the one the machine starts in.
    [[nodiscard]] StateId addState(std::string name, const Clip* clip, f32 speed = 1.0f);

    // Index of a state by name, or kInvalid. Linear — for setup, not per-frame use.
    [[nodiscard]] StateId find(std::string_view name) const;

    // One requirement on one parameter. A transition's conditions are ANDed: "speed above
    // 0.5 AND grounded". Alternatives are two transitions.
    struct Condition {
        enum class Op : u8 {
            Greater,     // float parameter > threshold
            Less,        // float parameter < threshold
            IsTrue,      // bool parameter
            IsFalse,
            Triggered,   // trigger parameter was raised (and is consumed when this fires)
        };
        std::string name;
        Op          op        = Op::Triggered;
        f32         threshold = 0.0f;
    };

    struct Transition {
        StateId to       = kInvalid;
        f32     fadeTime = 0.25f;
        // Normalized time (0..1 of the clip) the state must have reached before this may
        // fire; negative means "any time". 1.0 on a non-looping state means "when it
        // finishes" — the jump-lands-so-idle-resumes transition. On a looping state the gate
        // reopens every cycle, which is what lets a walk hand off to a run only at the foot
        // plant instead of mid-stride.
        f32     exitTime = -1.0f;
        // All must hold. Empty means the exit time alone decides — an unconditional
        // transition with no exit time either would fire the instant the state is entered.
        std::vector<Condition> conditions;
    };

    // Transitions are checked in the order added, kAny ones first; the first that passes
    // wins. A kAny transition never re-enters the state it is already in. A from-specific
    // transition MAY target its own state (an attack that restarts on a fresh trigger) —
    // gate it with a Triggered condition, or it will restart every frame its conditions
    // hold.
    void addTransition(StateId from, Transition t);

    // --- Parameters — the only surface gameplay drives per-frame -------------------------
    void setFloat(std::string_view name, f32 value);
    void setBool(std::string_view name, bool value);
    // Raised until a transition whose conditions reference it fires, then consumed. Set and
    // forget: gameplay does not need to know which frame the machine reacts.
    void trigger(std::string_view name);
    void resetTrigger(std::string_view name);

    [[nodiscard]] f32  getFloat(std::string_view name) const;
    [[nodiscard]] bool getBool(std::string_view name) const;

    // Force a state, ignoring transitions — cutscenes, respawns, debug. fadeTime 0 snaps.
    void play(StateId, f32 fadeTime = 0.0f);

    // Advance clocks and the fade, collect events and root motion, then evaluate
    // transitions. Events fired this frame belong to this frame even when a transition
    // also fires.
    void update(f32 dt);

    // The blended pose of the machine as it stands. Feed it to the skeleton's skinning
    // matrices, or under a BlendTree mask for layering.
    void evaluate(const Skeleton&, Pose& out) const;

    // --- Introspection — what the debug overlay wants to print ---------------------------
    [[nodiscard]] StateId current() const { return m_current; }
    [[nodiscard]] StateId previous() const { return m_previous; }
    [[nodiscard]] const std::string& stateName(StateId) const;
    [[nodiscard]] bool fading() const { return m_fading; }
    [[nodiscard]] f32  fadeProgress() const { return m_weight; }
    // 0..1 through the current state's clip; a finished non-looping clip reads 1.
    [[nodiscard]] f32  normalizedTime() const;

    // Events crossed by the last update(), from whichever of the two states was audible
    // enough — same threshold logic, and the same reason, as BlendTree.
    struct Fired {
        const Event* event  = nullptr;
        StateId      state  = kInvalid;
        f32          weight = 0.0f;
    };
    [[nodiscard]] const std::vector<Fired>& firedEvents() const { return m_fired; }

    // Root motion extraction for every state's clip, or -1 for none. The two active states'
    // deltas are blended by the fade weight, so a walk fading into a run speeds up over the
    // fade instead of stepping.
    void setRootMotionJoint(i32 joint);
    [[nodiscard]] const RootMotion& rootMotion() const { return m_rootMotion; }

    f32 eventWeightThreshold = 0.5f;

private:
    struct State {
        std::string name;
        const Clip* clip  = nullptr;
        f32         speed = 1.0f;
    };
    struct TransitionFrom {
        StateId    from = kAny;
        Transition t;
    };
    struct FloatParam { std::string name; f32 value = 0.0f; };
    struct BoolParam  { std::string name; bool value = false; };

    [[nodiscard]] bool passes(const TransitionFrom&) const;
    void consumeTriggers(const Transition&);
    void beginFade(StateId to, f32 fadeTime);
    void checkTransitions();

    std::vector<State>          m_states;
    std::vector<TransitionFrom> m_transitions;

    std::vector<FloatParam>  m_floats;
    std::vector<BoolParam>   m_bools;
    std::vector<std::string> m_triggers;   // currently raised

    // Two players, same shape as CrossFade: `m_to` is the current state, `m_from` is what
    // it is fading out of.
    Player  m_from;
    Player  m_to;
    StateId m_current  = kInvalid;
    StateId m_previous = kInvalid;
    f32     m_weight   = 1.0f;   // 1 = fully on m_to
    f32     m_fadeTime = 0.0f;
    bool    m_fading   = false;

    i32                m_rootJoint = -1;
    RootMotion         m_rootMotion;
    std::vector<Fired> m_fired;
};

}
