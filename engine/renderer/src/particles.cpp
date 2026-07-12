#include "vortex/renderer/particles.hpp"

#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/scalar.hpp"
#include "vortex/core/profiler.hpp"
#include "vortex/jobs/job_system.hpp"
#include "vortex/renderer/culling.hpp"

#include <algorithm>
#include <cmath>

namespace vortex::renderer {

namespace {

// Below this, splitting the integration across workers costs more than it saves.
constexpr usize kParallelThreshold = 2048;
constexpr usize kGrain             = 512;

}

ParticleEmitter::ParticleEmitter(const ParticleEmitterDesc& desc, u64 seed)
    : m_desc(desc), m_rng(seed) {
    const usize cap = m_desc.capacity;
    m_position.resize(cap);
    m_velocity.resize(cap);
    m_rotation.resize(cap);
    m_angularVelocity.resize(cap);
    m_age.resize(cap);
    m_invLifetime.resize(cap);
    m_size.resize(cap);
}

void ParticleEmitter::spawnOne() {
    if (m_count >= m_desc.capacity) return;   // full: drop rather than grow

    const usize i = m_count++;

    Vec2 offset{};
    if (m_desc.spawnHalfExtents.x != 0.0f || m_desc.spawnHalfExtents.y != 0.0f)
        offset += m_rng.insideRect(-m_desc.spawnHalfExtents, m_desc.spawnHalfExtents);
    if (m_desc.spawnRadius != 0.0f)
        offset += m_rng.insideUnitCircle() * m_desc.spawnRadius;

    const f32 angle    = m_desc.direction.pick(m_rng);
    const f32 speed    = m_desc.speed.pick(m_rng);
    const f32 lifetime = m_desc.lifetime.pick(m_rng);

    m_position[i]        = position + offset;
    m_velocity[i]        = Vec2::fromAngle(angle) * speed;
    m_rotation[i]        = m_desc.rotation.pick(m_rng);
    m_angularVelocity[i] = m_desc.angularVelocity.pick(m_rng);
    m_age[i]             = 0.0f;
    m_invLifetime[i]     = lifetime > 0.0f ? 1.0f / lifetime : 0.0f;
    m_size[i]            = m_desc.size.pick(m_rng);
}

void ParticleEmitter::emit(u32 count) {
    for (u32 i = 0; i < count; ++i) spawnOne();
}

void ParticleEmitter::emitAt(Vec2 origin, u32 count) {
    position = origin;
    emit(count);
}

void ParticleEmitter::integrate(usize first, usize last, f32 dt) {
    // Semi-implicit Euler: force the velocity, then move by the forced velocity.
    // Drag is applied as an exact-ish exponential rather than (1 - drag*dt), which
    // goes unstable once drag*dt exceeds 1.
    const Vec2 gravityStep = m_desc.gravity * dt;
    const f32  dragFactor  = 1.0f / (1.0f + m_desc.drag * dt);

    for (usize i = first; i < last; ++i) {
        m_age[i] += dt;
        m_velocity[i] = (m_velocity[i] + gravityStep) * dragFactor;
        m_position[i] += m_velocity[i] * dt;
        m_rotation[i] += m_angularVelocity[i] * dt;
    }
}

void ParticleEmitter::compact() {
    // Swap-and-pop from the back. Walking forward while pulling survivors in from
    // the tail keeps the live range contiguous with no second buffer.
    usize i = 0;
    while (i < m_count) {
        const bool dead = m_age[i] * m_invLifetime[i] >= 1.0f || m_invLifetime[i] == 0.0f;
        if (!dead) { ++i; continue; }

        const usize last = --m_count;
        if (i != last) {
            m_position[i]        = m_position[last];
            m_velocity[i]        = m_velocity[last];
            m_rotation[i]        = m_rotation[last];
            m_angularVelocity[i] = m_angularVelocity[last];
            m_age[i]             = m_age[last];
            m_invLifetime[i]     = m_invLifetime[last];
            m_size[i]            = m_size[last];
        }
        // Do not advance i: the particle just swapped in has not been tested yet.
    }
}

void ParticleEmitter::update(f32 dt) {
    VORTEX_PROFILE_ZONE("particles.update");
    if (dt <= 0.0f) return;

    integrate(0, m_count, dt);
    compact();

    if (emitting && m_desc.rate > 0.0f) {
        m_emitDebt += m_desc.rate * dt;
        const u32 spawn = static_cast<u32>(m_emitDebt);
        m_emitDebt -= static_cast<f32>(spawn);
        emit(spawn);
    }
}

void ParticleEmitter::update(f32 dt, jobs::JobSystem& jobs) {
    VORTEX_PROFILE_ZONE("particles.update.parallel");
    if (dt <= 0.0f) return;

    if (m_count < kParallelThreshold) {
        integrate(0, m_count, dt);
    } else {
        // Integration is per-particle independent, so it shards cleanly. Dispatch over
        // chunks rather than indices so each worker pays the per-step setup once, not
        // once per particle. Killing and spawning both reorder the pool, so they stay
        // on the calling thread.
        const usize chunks = (m_count + kGrain - 1) / kGrain;
        jobs.parallelFor(chunks, [&](usize c) {
            const usize first = c * kGrain;
            integrate(first, std::min(first + kGrain, m_count), dt);
        }, 1);
    }
    compact();

    if (emitting && m_desc.rate > 0.0f) {
        m_emitDebt += m_desc.rate * dt;
        const u32 spawn = static_cast<u32>(m_emitDebt);
        m_emitDebt -= static_cast<f32>(spawn);
        emit(spawn);
    }
}

void ParticleEmitter::extract(std::vector<RenderItem>& out, const Rect* visibleBounds) const {
    VORTEX_PROFILE_ZONE("particles.extract");
    if (m_count == 0) return;

    out.reserve(out.size() + m_count);

    const Vec4 startColor = m_desc.startColor;
    const Vec4 endColor   = m_desc.endColor;

    for (usize i = 0; i < m_count; ++i) {
        const f32 t = saturate(m_age[i] * m_invLifetime[i]);

        const f32  scale = lerp(m_desc.startScale, m_desc.endScale, easing::evaluate(m_desc.scaleCurve, t));
        const f32  extent = m_size[i] * scale;
        if (extent <= 0.0f) continue;

        const Vec2 half{extent * 0.5f, extent * 0.5f};
        if (visibleBounds != nullptr) {
            // Rotation only ever grows the AABB by sqrt(2), so testing the
            // axis-aligned square is a conservative reject that costs no trig.
            const Rect bounds{m_position[i].x - half.x, m_position[i].y - half.y, extent, extent};
            if (!visibleBounds->intersects(bounds)) continue;
        }

        out.push_back({
            .transform = Mat4::translation(m_position[i].x, m_position[i].y, 0.0f) *
                         Mat4::rotationZ(m_rotation[i]) *
                         Mat4::scaling(extent, extent, 1.0f),
            .color   = lerp(startColor, endColor, easing::evaluate(m_desc.colorCurve, t)),
            .uv      = m_desc.uv,
            .texture = m_desc.texture,
            .layer   = m_desc.layer,
        });
    }
}

void ParticleEmitter::clear() {
    m_count    = 0;
    m_emitDebt = 0.0f;
}

// ---------------------------------------------------------------- ParticleWorld

EmitterHandle ParticleWorld::add(const ParticleEmitterDesc& desc, u64 seed) {
    u32 index;
    if (!m_free.empty()) {
        index = m_free.back();
        m_free.pop_back();
    } else {
        index = static_cast<u32>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot& slot   = m_slots[index];
    slot.emitter = std::make_unique<ParticleEmitter>(desc, seed);
    slot.alive   = true;
    return EmitterHandle{index, slot.generation};
}

ParticleWorld::Slot* ParticleWorld::resolve(EmitterHandle handle) {
    if (!handle.valid() || handle.index >= m_slots.size()) return nullptr;
    Slot& slot = m_slots[handle.index];
    if (!slot.alive || slot.generation != handle.generation) return nullptr;
    return &slot;
}

void ParticleWorld::remove(EmitterHandle handle) {
    Slot* slot = resolve(handle);
    if (slot == nullptr) return;
    slot->emitter.reset();
    slot->alive = false;
    ++slot->generation;   // invalidate every outstanding handle
    m_free.push_back(handle.index);
}

ParticleEmitter* ParticleWorld::get(EmitterHandle handle) {
    Slot* slot = resolve(handle);
    return slot != nullptr ? slot->emitter.get() : nullptr;
}

const ParticleEmitter* ParticleWorld::get(EmitterHandle handle) const {
    return const_cast<ParticleWorld*>(this)->get(handle);
}

void ParticleWorld::update(f32 dt) {
    for (Slot& slot : m_slots)
        if (slot.alive) slot.emitter->update(dt);
}

void ParticleWorld::update(f32 dt, jobs::JobSystem& jobs) {
    for (Slot& slot : m_slots)
        if (slot.alive) slot.emitter->update(dt, jobs);
}

void ParticleWorld::extract(std::vector<RenderItem>& out, const Rect* visibleBounds) const {
    for (const Slot& slot : m_slots)
        if (slot.alive) slot.emitter->extract(out, visibleBounds);
}

usize ParticleWorld::aliveParticles() const noexcept {
    usize total = 0;
    for (const Slot& slot : m_slots)
        if (slot.alive) total += slot.emitter->aliveCount();
    return total;
}

}
