// Animation state machine, headless: the Đợt B slice proven without a GPU.
//
// A synthetic two-joint rig and three synthetic clips (Idle, Walk 1.2 m/s with footstep
// events, a non-looping Jump) drive a StateMachine the way gameplay would:
//
//   speed > 0.5      ->  Idle fades into Walk
//   trigger("jump")  ->  anything fades into Jump
//   Jump finishes    ->  exit time 1.0 hands back to Idle
//
// Every frame the pose is evaluated and folded into skinning matrices, so the whole
// pipeline runs — there is just no mesh on the end of it. Each stage prints what it
// verified and the process exits non-zero on the first lie, which makes this file both
// the example of how the API is meant to be used and a regression test that runs in CI
// with no display attached.

#include "vortex/anim/clip.hpp"
#include "vortex/anim/pose.hpp"
#include "vortex/anim/skeleton.hpp"
#include "vortex/anim/state_machine.hpp"
#include "vortex/core/log.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vortex;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// root (moves the character) -> spine (waves about, so blending has something to blend).
anim::Skeleton makeRig() {
    anim::Skeleton s;
    anim::Joint root;
    root.name = "root";
    anim::Joint spine;
    spine.name   = "spine";
    spine.parent = 0;
    spine.bindPose.translation = {0.0f, 1.0f, 0.0f};
    s.joints = {root, spine};
    return s;
}

anim::Clip makeIdle() {
    anim::Clip c;
    c.name     = "Idle";
    c.duration = 1.0f;
    c.tracks.resize(2);
    c.tracks[1].rotation.times  = {0.0f, 0.5f, 1.0f};
    c.tracks[1].rotation.values = {Quat::identity(),
                                   Quat::fromAxisAngle({1, 0, 0}, 0.05f),
                                   Quat::identity()};
    return c;
}

anim::Clip makeWalk() {
    anim::Clip c;
    c.name     = "Walk";
    c.duration = 1.0f;
    c.tracks.resize(2);
    // The authored truth this example exists to extract: 1.2 units forward per cycle.
    c.tracks[0].translation.times  = {0.0f, 1.0f};
    c.tracks[0].translation.values = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.2f}};
    c.tracks[1].rotation.times  = {0.0f, 0.5f, 1.0f};
    c.tracks[1].rotation.values = {Quat::fromAxisAngle({0, 0, 1}, 0.1f),
                                   Quat::fromAxisAngle({0, 0, 1}, -0.1f),
                                   Quat::fromAxisAngle({0, 0, 1}, 0.1f)};
    c.addEvent(0.3f, "footstep");
    c.addEvent(0.8f, "footstep");
    return c;
}

anim::Clip makeJump() {
    anim::Clip c;
    c.name     = "Jump";
    c.duration = 0.5f;
    c.loop     = false;
    c.tracks.resize(2);
    c.tracks[0].translation.times  = {0.0f, 0.25f, 0.5f};
    c.tracks[0].translation.values = {{0, 0, 0}, {0.0f, 0.8f, 0.0f}, {0, 0, 0}};
    c.addEvent(0.45f, "land");
    return c;
}

} // namespace

int main() {
    const anim::Skeleton rig  = makeRig();
    const anim::Clip     idle = makeIdle();
    const anim::Clip     walk = makeWalk();
    const anim::Clip     jump = makeJump();

    anim::StateMachine sm;
    const auto sIdle = sm.addState("Idle", &idle);
    const auto sWalk = sm.addState("Walk", &walk);
    const auto sJump = sm.addState("Jump", &jump);
    sm.setRootMotionJoint(0);

    using Cond = anim::StateMachine::Condition;
    sm.addTransition(sIdle, {.to = sWalk, .fadeTime = 0.2f,
                             .conditions = {{"speed", Cond::Op::Greater, 0.5f}}});
    sm.addTransition(sWalk, {.to = sIdle, .fadeTime = 0.2f,
                             .conditions = {{"speed", Cond::Op::Less, 0.5f}}});
    sm.addTransition(anim::StateMachine::kAny,
                     {.to = sJump, .fadeTime = 0.05f,
                      .conditions = {{"jump", Cond::Op::Triggered}}});
    sm.addTransition(sJump, {.to = sIdle, .fadeTime = 0.1f, .exitTime = 1.0f});

    constexpr f32 kDt = 1.0f / 60.0f;

    // What the game loop does every frame, condensed: advance, harvest, pose, skin.
    Vec3              travelled{};
    int               footsteps = 0, landings = 0;
    anim::Pose        pose;
    std::vector<Mat4> skinning;
    auto step = [&](f32 seconds) {
        for (f32 t = 0.0f; t < seconds; t += kDt) {
            sm.update(kDt);
            travelled = travelled + sm.rootMotion().translation;
            for (const auto& fired : sm.firedEvents()) {
                if (fired.event->name == "footstep") ++footsteps;
                if (fired.event->name == "land") ++landings;
            }
            sm.evaluate(rig, pose);
            rig.computeSkinningMatrices(pose, skinning);
        }
    };

    std::printf("-- idle: nobody asked for anything --\n");
    step(0.5f);
    check(sm.current() == sIdle, "starts in the first state added");
    check(std::fabs(travelled.z) < 1e-4f, "an in-place clip produces no root motion");

    std::printf("-- speed rises: idle should hand off to walk --\n");
    sm.setFloat("speed", 2.0f);
    step(kDt * 2.0f);
    check(sm.current() == sWalk, "float condition drives the transition");
    check(sm.fading(), "and it fades rather than snaps");

    travelled = {};
    footsteps = 0;
    step(2.0f);
    // 2 s of a 1.2 units-per-cycle walk is 2.4 — minus the opening fade, where the walk
    // only owns part of the pose and so only part of the motion.
    check(travelled.z > 2.1f && travelled.z < 2.45f,
          "root motion accumulates at the clip's authored pace");
    check(footsteps >= 3 && footsteps <= 4, "footstep events fire once per plant");
    check(std::fabs(pose[0].translation.z) < 1e-4f,
          "the extracted motion is pinned OUT of the pose");

    std::printf("-- jump trigger: fires from any state, hands back when done --\n");
    sm.trigger("jump");
    step(kDt * 2.0f);
    check(sm.current() == sJump, "trigger condition fires from kAny");
    sm.setFloat("speed", 0.0f);   // land into stillness, so Jump -> Idle sticks
    step(0.7f);
    check(landings == 1, "non-looping clip fires its event exactly once");
    check(sm.current() == sIdle, "exit time 1.0 leaves when the clip finishes");
    check(!sm.getBool("nonexistent") && sm.getFloat("speed") == 0.0f,
          "unknown parameters read as zero, not as garbage");

    std::printf("-- verdict --\n");
    if (g_failures == 0) {
        std::printf("  all checks passed\n");
    } else {
        std::printf("  %d check(s) FAILED\n", g_failures);
    }
    return g_failures;
}
