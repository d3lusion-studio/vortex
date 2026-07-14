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

// One named animation over one skeleton: a track per joint, indexed the same way.
struct Clip {
    std::string             name;
    std::vector<JointTrack> tracks;   // one per joint, may be empty
    f32                     duration = 0.0f;
    bool                    loop     = true;

    // Sample the whole clip at `time`, into `out` (one Transform per joint). Joints the clip
    // does not key keep whatever `out` already holds — so `out` should start as the bind
    // pose, or as the result of the animation being blended out of.
    void sample(f32 time, std::vector<Transform>& out) const;
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

    f32  speed = 1.0f;

private:
    const Clip* m_clip     = nullptr;
    f32         m_time     = 0.0f;
    bool        m_finished = false;
};

}
