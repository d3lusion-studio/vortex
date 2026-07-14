#pragma once
#include "vortex/anim/skeleton.hpp"
#include "vortex/core/types.hpp"

#include <string>
#include <vector>

namespace vortex::anim {

// How a sampler gets from one keyframe to the next.
//
// Step is not a degenerate case to be optimised away — it is what an animator uses when a
// value must NOT be interpolated (a visibility flag, a hard cut). glTF distinguishes them,
// so we do.
enum class Interpolation : u8 { Linear, Step };

// One animated property of one joint. Each is sampled independently: an animation may key a
// joint's rotation on twenty frames and its translation on two, and forcing them onto a
// shared timeline would either lose keys or invent them.
template <typename T>
struct Channel {
    std::vector<f32> times;    // seconds, ascending
    std::vector<T>   values;   // one per time
    Interpolation    interp = Interpolation::Linear;

    [[nodiscard]] bool empty() const { return times.empty(); }
};

struct JointTrack {
    Channel<Vec3> translation;
    Channel<Quat> rotation;
    Channel<Vec3> scale;

    [[nodiscard]] bool empty() const {
        return translation.empty() && rotation.empty() && scale.empty();
    }
};

// A moment in a clip worth telling the game about: the frame a foot lands, a sword connects,
// a footstep sound should play.
//
// It belongs to the CLIP, not to the code that plays it, because the answer to "when does the
// foot land" is a property of the animation and nothing else. Put it in the gameplay code and
// it goes wrong the moment an animator retimes the walk.
struct Event {
    f32         time = 0.0f;
    std::string name;
};

// The movement an animation wants the CHARACTER to make: how far the root joint travelled
// and how far it turned over some span of the timeline.
//
// A walk cycle authored in place slides the feet; a walk cycle authored moving has the truth
// of how fast the character walks baked into it, by the animator, who is the one who knows.
// Root motion is how that truth reaches gameplay: the motion is stripped OUT of the pose (or
// the mesh would run away from its entity) and handed to whoever owns the entity's transform
// to apply.
//
// `translation` is expressed in the root joint's frame at the START of the span — rotate it
// by the entity's current facing and add. That is what makes deltas composable: a character
// mid-turn keeps curving instead of sliding along its original heading.
struct RootMotion {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation{};

    // This span, then `next`. Needed when a looping clip wraps inside one step: the frame's
    // motion is tail-of-timeline then head-of-timeline, and the head's translation happens
    // in the orientation the tail ended at.
    [[nodiscard]] RootMotion then(const RootMotion& next) const {
        return {translation + rotation.rotate(next.translation),
                (rotation * next.rotation).normalized()};
    }
};

// One named animation over one skeleton: a track per joint, indexed the same way.
struct Clip {
    std::string             name;
    std::vector<JointTrack> tracks;   // one per joint, may be empty
    std::vector<Event>      events;   // must be sorted by time; addEvent keeps them so
    f32                     duration = 0.0f;
    bool                    loop     = true;

    // Sample the whole clip at `time`, into `out` (one Transform per joint). Joints the clip
    // does not key keep whatever `out` already holds — so `out` should start as the bind
    // pose, or as the result of the animation being blended out of.
    void sample(f32 time, std::vector<Transform>& out) const;

    void addEvent(f32 time, std::string name);

    // Every event whose time falls in (from, to]. Half-open at the start so that stepping
    // twice across the same instant does not fire it twice.
    void eventsIn(f32 from, f32 to, std::vector<const Event*>& out) const;

    // How `joint` moved between two points on the timeline. `from` after `to` simply yields
    // the reverse delta, which is what playing backwards means. A joint the clip does not
    // key does not move: identity.
    [[nodiscard]] RootMotion rootMotion(i32 joint, f32 from, f32 to) const;
};

// Plays one clip. Deliberately the smallest thing that can be correct: it advances a clock
// and samples. Blending between clips, masking a set of joints and firing events off a
// timeline are all built on top of this, not into it.
class Player {
public:
    void play(const Clip* clip, bool restart = true);
    void stop() { m_clip = nullptr; }

    void update(f32 dt);

    // The pose for the current time. `skeleton` supplies the rest pose for joints the clip
    // does not touch.
    void pose(const Skeleton& skeleton, std::vector<Transform>& out) const;

    [[nodiscard]] const Clip* clip() const { return m_clip; }
    [[nodiscard]] f32  time() const { return m_time; }
    [[nodiscard]] bool playing() const { return m_clip != nullptr && !m_finished; }
    [[nodiscard]] bool finished() const { return m_finished; }

    void setTime(f32 t) { m_time = t; }

    // Events crossed by the last update(). Refilled every update, so read it before the next.
    //
    // A looping clip that wraps within one step fires the tail of the timeline AND the head:
    // dropping either would silently swallow a footstep whenever a frame happened to land on
    // the loop point, which is a bug that only shows up as "sometimes the sound doesn't play".
    [[nodiscard]] const std::vector<const Event*>& firedEvents() const { return m_fired; }

    // The root joint's movement covered by the last update(), loop wrap included. Only
    // meaningful when `rootMotionJoint` is set; identity otherwise.
    [[nodiscard]] const RootMotion& rootMotion() const { return m_rootMotion; }

    f32  speed = 1.0f;

    // Set to the root joint's index to extract root motion. While set, pose() pins that
    // joint's translation and rotation to the bind pose — the movement has been handed to
    // gameplay, and leaving it in the pose as well would apply it twice.
    i32  rootMotionJoint = -1;

private:
    const Clip*               m_clip     = nullptr;
    f32                       m_time     = 0.0f;
    bool                      m_finished = false;
    std::vector<const Event*> m_fired;
    RootMotion                m_rootMotion;
};

}
