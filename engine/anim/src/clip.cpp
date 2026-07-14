#include "vortex/anim/clip.hpp"

#include <algorithm>
#include <cmath>

namespace vortex::anim {

namespace {

// Where `time` falls in an ascending key list: the index of the key at or before it, and how
// far between that key and the next. Binary search, because a clip can carry hundreds of
// keys per channel and this runs once per channel per joint per frame.
struct Cursor {
    usize index = 0;
    f32   t     = 0.0f;   // 0 at keys[index], 1 at keys[index + 1]
    bool  last  = false;  // at or past the final key: no interpolation to do
};

Cursor locate(const std::vector<f32>& times, f32 time) {
    Cursor c;
    if (times.size() < 2) { c.last = true; return c; }

    if (time <= times.front()) return c;
    if (time >= times.back()) { c.index = times.size() - 1; c.last = true; return c; }

    const auto it = std::upper_bound(times.begin(), times.end(), time);
    c.index = static_cast<usize>(it - times.begin()) - 1;

    const f32 t0 = times[c.index], t1 = times[c.index + 1];
    const f32 span = t1 - t0;
    c.t = span > 0.0f ? (time - t0) / span : 0.0f;
    return c;
}

Vec3 lerp(Vec3 a, Vec3 b, f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

template <typename T, typename Blend>
bool sampleChannel(const Channel<T>& ch, f32 time, T& out, Blend blend) {
    if (ch.empty()) return false;

    const Cursor c = locate(ch.times, time);
    if (c.last || ch.interp == Interpolation::Step) {
        out = ch.values[std::min(c.index, ch.values.size() - 1)];
        return true;
    }
    out = blend(ch.values[c.index], ch.values[c.index + 1], c.t);
    return true;
}

} // namespace

void Clip::sample(f32 time, std::vector<Transform>& out) const {
    const usize n = std::min(tracks.size(), out.size());
    for (usize i = 0; i < n; ++i) {
        const JointTrack& track = tracks[i];
        if (track.empty()) continue;   // not keyed: keep whatever the caller put there

        Transform& t = out[i];
        sampleChannel(track.translation, time, t.translation, lerp);
        sampleChannel(track.rotation, time, t.rotation,
                      [](Quat a, Quat b, f32 u) { return Quat::slerp(a, b, u); });
        sampleChannel(track.scale, time, t.scale, lerp);
    }
}

void Clip::addEvent(f32 time, std::string name) {
    events.push_back({time, std::move(name)});
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) { return a.time < b.time; });
}

void Clip::eventsIn(f32 from, f32 to, std::vector<const Event*>& out) const {
    for (const Event& e : events)
        if (e.time > from && e.time <= to) out.push_back(&e);
}

RootMotion Clip::rootMotion(i32 joint, f32 from, f32 to) const {
    RootMotion rm;
    if (joint < 0 || static_cast<usize>(joint) >= tracks.size()) return rm;
    const JointTrack& track = tracks[static_cast<usize>(joint)];

    // The joint's orientation at the start of the span. Everything is expressed relative to
    // it — an unkeyed rotation channel leaves it identity, which degrades gracefully to
    // "delta in parent space".
    Quat q0{}, q1{};
    const bool hasRotation = sampleChannel(track.rotation, from, q0,
                                 [](Quat a, Quat b, f32 u) { return Quat::slerp(a, b, u); }) &&
                             sampleChannel(track.rotation, to, q1,
                                 [](Quat a, Quat b, f32 u) { return Quat::slerp(a, b, u); });
    if (hasRotation) rm.rotation = (q0.conjugate() * q1).normalized();

    Vec3 p0{}, p1{};
    if (sampleChannel(track.translation, from, p0, lerp) &&
        sampleChannel(track.translation, to, p1, lerp))
        rm.translation = q0.conjugate().rotate(p1 - p0);

    return rm;
}

void Player::play(const Clip* clip, bool restart) {
    if (m_clip == clip && !restart) return;
    m_clip     = clip;
    m_time     = 0.0f;
    m_finished = false;
}

void Player::update(f32 dt) {
    m_fired.clear();
    m_rootMotion = {};
    if (m_clip == nullptr || m_finished) return;

    const f32 previous = m_time;
    m_time += dt * speed;

    const f32 duration = m_clip->duration;
    if (duration <= 0.0f) { m_time = 0.0f; return; }

    const bool forward = dt * speed >= 0.0f;

    if (m_clip->loop) {
        const f32 raw = m_time;
        m_time = std::fmod(m_time, duration);
        if (m_time < 0.0f) m_time += duration;   // playing backwards wraps too

        // Events and root motion have the same wrap problem: a step that crosses the loop
        // point covers the tail of the timeline AND its head, and dropping either half
        // surfaces as "sometimes the footstep doesn't play" / the character hitching one
        // frame's distance backwards, once per loop.
        const bool wrapped = forward ? raw >= duration : raw < 0.0f;

        if (forward) {
            if (wrapped) {
                m_clip->eventsIn(previous, duration, m_fired);
                m_clip->eventsIn(0.0f, m_time, m_fired);
            } else {
                m_clip->eventsIn(previous, m_time, m_fired);
            }
        }

        if (rootMotionJoint >= 0) {
            if (wrapped) {
                const f32 mid = forward ? duration : 0.0f;
                const f32 re  = forward ? 0.0f : duration;
                m_rootMotion = m_clip->rootMotion(rootMotionJoint, previous, mid)
                                   .then(m_clip->rootMotion(rootMotionJoint, re, m_time));
            } else {
                m_rootMotion = m_clip->rootMotion(rootMotionJoint, previous, m_time);
            }
        }
        return;
    }

    if (m_time >= duration) {
        m_time     = duration;
        m_finished = true;
    } else if (m_time < 0.0f) {
        m_time     = 0.0f;
        m_finished = true;
    }
    if (forward) m_clip->eventsIn(previous, m_time, m_fired);
    if (rootMotionJoint >= 0)
        m_rootMotion = m_clip->rootMotion(rootMotionJoint, previous, m_time);
}

void Player::pose(const Skeleton& skeleton, std::vector<Transform>& out) const {
    // Start from the rest pose every frame, so a joint the clip does not key holds still
    // instead of keeping whatever the last clip left in it.
    out = skeleton.bindPose();
    if (m_clip != nullptr) m_clip->sample(m_time, out);

    // Root motion was handed to gameplay; the pose must not walk off on its own as well.
    // Scale stays sampled — squash-and-stretch is not movement.
    if (rootMotionJoint >= 0 && static_cast<usize>(rootMotionJoint) < out.size() &&
        static_cast<usize>(rootMotionJoint) < skeleton.joints.size()) {
        const Transform& bind = skeleton.joints[static_cast<usize>(rootMotionJoint)].bindPose;
        out[static_cast<usize>(rootMotionJoint)].translation = bind.translation;
        out[static_cast<usize>(rootMotionJoint)].rotation    = bind.rotation;
    }
}

}
