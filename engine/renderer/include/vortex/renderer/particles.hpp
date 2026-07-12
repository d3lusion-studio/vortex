#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/color.hpp"
#include "vortex/core/math/easing.hpp"
#include "vortex/core/math/random.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <memory>
#include <vector>

namespace vortex::jobs { class JobSystem; }

namespace vortex::renderer {

// Inclusive range a spawn draws from. Collapse it (min == max) for a constant.
struct Range {
    f32 min = 0.0f;
    f32 max = 0.0f;

    constexpr Range() noexcept = default;
    constexpr Range(f32 value) noexcept : min(value), max(value) {}          // NOLINT(*-explicit-*)
    constexpr Range(f32 lo, f32 hi) noexcept : min(lo), max(hi) {}

    [[nodiscard]] f32 pick(Random& rng) const noexcept {
        return min == max ? min : rng.range(min, max);
    }
};

// Everything a particle's whole life is decided by at spawn. One desc drives one
// emitter; vary the effect by varying the desc, not by branching per particle.
struct ParticleEmitterDesc {
    // Required: the batcher binds a texture per draw. Point it at a 1x1 white pixel
    // (App::whiteTexture()) for flat-coloured particles.
    rhi::TextureHandle texture;
    Rect               uv    = kFullUV;
    i32                layer = 0;

    // Spawn shape, relative to the emitter position.
    Vec2 spawnHalfExtents{0.0f, 0.0f};   // box; zero spawns at a point
    f32  spawnRadius = 0.0f;             // disc; added on top of the box

    // Emission. `rate` is a steady stream; emit() adds bursts on top.
    f32 rate = 0.0f;   // particles per second, 0 = burst-only

    Range lifetime{1.0f};
    Range speed{100.0f, 200.0f};
    Range direction{0.0f, kTwoPi};   // radians; collapse for a directed jet
    Range size{8.0f};                // world units, at scale 1
    Range rotation{0.0f};
    Range angularVelocity{0.0f};     // radians/sec

    // Forces, applied every step.
    Vec2 gravity{0.0f, 0.0f};
    f32  drag = 0.0f;   // velocity *= 1 / (1 + drag * dt)

    // Life curves, evaluated on normalised age in [0, 1].
    Color        startColor = Color::white();
    Color        endColor   = Color::transparent();
    f32          startScale = 1.0f;
    f32          endScale   = 1.0f;
    easing::Ease colorCurve = easing::Ease::Linear;
    easing::Ease scaleCurve = easing::Ease::Linear;

    // Hard ceiling. The pool is allocated once at this size and never grows, so a
    // runaway emitter drops new particles rather than the frame budget.
    u32 capacity = 1024;
};

// One effect: a fixed-capacity, structure-of-arrays pool. Nothing allocates after
// construction, dead particles are swap-and-popped, and every per-particle field
// lives in its own contiguous array so update() walks memory linearly.
class ParticleEmitter {
public:
    explicit ParticleEmitter(const ParticleEmitterDesc& desc, u64 seed = 0x9e3779b9u);

    // Spawn `count` particles now, on top of whatever `rate` is producing.
    void emit(u32 count);
    void emitAt(Vec2 origin, u32 count);   // moves the emitter, then bursts

    // Integrate one step: age, kill, force, move. Steady-rate emission happens here.
    void update(f32 dt);

    // Same, but the integration is spread across the job system. Worth it once a
    // single emitter carries thousands of live particles; below that the sync
    // costs more than the work.
    void update(f32 dt, jobs::JobSystem& jobs);

    // Append the live particles as draw items. Culls against `visibleBounds` when
    // given (see Camera2D::visibleBounds).
    void extract(std::vector<RenderItem>& out, const Rect* visibleBounds = nullptr) const;

    void clear();   // kill every live particle; keeps the pool

    [[nodiscard]] usize aliveCount() const noexcept { return m_count; }
    [[nodiscard]] u32   capacity()   const noexcept { return m_desc.capacity; }
    [[nodiscard]] const ParticleEmitterDesc& desc() const noexcept { return m_desc; }

    Vec2 position{0.0f, 0.0f};
    bool emitting = true;    // gate the steady rate; bursts ignore this

private:
    void spawnOne();
    void integrate(usize first, usize last, f32 dt);
    void compact();          // swap-and-pop everything whose age has run out

    ParticleEmitterDesc m_desc;
    Random              m_rng;

    // SoA. Index i in every array is the same particle; [0, m_count) is live.
    std::vector<Vec2> m_position;
    std::vector<Vec2> m_velocity;
    std::vector<f32>  m_rotation;
    std::vector<f32>  m_angularVelocity;
    std::vector<f32>  m_age;         // seconds lived
    std::vector<f32>  m_invLifetime; // 1 / lifetime, so the hot path never divides
    std::vector<f32>  m_size;

    usize m_count     = 0;
    f32   m_emitDebt  = 0.0f;   // fractional particles carried between frames
};

struct ParticleEmitterTag {};
using EmitterHandle = Handle<ParticleEmitterTag>;

// Owns every emitter in a scene and drives them together, so gameplay code holds
// plain handles and the frame loop makes exactly two calls.
class ParticleWorld {
public:
    EmitterHandle add(const ParticleEmitterDesc& desc, u64 seed = 0x9e3779b9u);
    void          remove(EmitterHandle handle);

    [[nodiscard]] ParticleEmitter*       get(EmitterHandle handle);
    [[nodiscard]] const ParticleEmitter* get(EmitterHandle handle) const;

    void update(f32 dt);
    void update(f32 dt, jobs::JobSystem& jobs);

    void extract(std::vector<RenderItem>& out, const Rect* visibleBounds = nullptr) const;

    [[nodiscard]] usize emitterCount() const noexcept { return m_slots.size() - m_free.size(); }
    [[nodiscard]] usize aliveParticles() const noexcept;

private:
    struct Slot {
        std::unique_ptr<ParticleEmitter> emitter;
        u32                              generation = 0;
        bool                             alive      = false;
    };

    [[nodiscard]] Slot* resolve(EmitterHandle handle);

    std::vector<Slot> m_slots;
    std::vector<u32>  m_free;
};

}
