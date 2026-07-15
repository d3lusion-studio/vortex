#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/types.hpp"

#include <functional>
#include <memory>

namespace vortex::audio {

struct SoundTag {};
using SoundHandle = Handle<SoundTag>;

enum class Waveform : u8 { Sine, Square, Triangle, Sawtooth };

// Fill `frames` with `frameCount * channels` interleaved f32 samples in [-1, 1], at
// `sampleRate` (passed in so synthesis code never guesses the device rate). Called from
// the AUDIO thread: no allocation, no locks shared with the game loop, no touching engine
// state. The callback owns whatever synthesis state it needs (a phase, an oscillator
// bank, a ring buffer the game feeds).
using ProceduralFn =
    std::function<void(f32* frames, u32 frameCount, u32 channels, u32 sampleRate)>;

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    // --- Sources -----------------------------------------------------------------------
    // `stream = true` decodes from disk as it plays instead of up front — music and long
    // ambience; a streamed sound costs no load hitch and little memory, but seeking is
    // coarser. SFX should stay fully decoded.
    [[nodiscard]] virtual SoundHandle load(const char* path, bool stream = false) = 0;

    // A generated oscillator — the "just play a 440 Hz tone" call, no asset involved.
    // Frequency can be changed live (a siren is one waveform and one moving frequency).
    [[nodiscard]] virtual SoundHandle createWaveform(Waveform, f32 frequencyHz,
                                                     f32 amplitude = 0.25f) = 0;
    virtual void setWaveformFrequency(SoundHandle, f32 frequencyHz) = 0;

    // A fully custom source: the callback IS the sound. Synthesis, voice chat buffers,
    // tracker playback — anything that can fill sample frames on demand.
    [[nodiscard]] virtual SoundHandle createProcedural(ProceduralFn) = 0;

    virtual void unload(SoundHandle) = 0;

    // --- Playback ----------------------------------------------------------------------
    // play() restarts from the top — the fire-a-sound-effect call. pause()/resume() hold
    // and release the cursor — the music call. The two are different verbs on purpose.
    virtual void play(SoundHandle, bool loop = false) = 0;
    virtual void stop(SoundHandle) = 0;
    virtual void pause(SoundHandle) = 0;
    virtual void resume(SoundHandle) = 0;
    [[nodiscard]] virtual bool playing(SoundHandle) const = 0;

    virtual void seek(SoundHandle, f32 seconds) = 0;

    // --- Mixing ------------------------------------------------------------------------
    virtual void setVolume(SoundHandle, f32 volume) = 0;
    virtual void setMasterVolume(f32 volume) = 0;
    // Playback-rate pitch: 2 is an octave up and twice as fast, like a record.
    virtual void setPitch(SoundHandle, f32 pitch) = 0;
    // -1 full left .. +1 full right. Ignored while a sound is spatialized — position IS
    // its pan.
    virtual void setPan(SoundHandle, f32 pan) = 0;

    // Ramp the sound's fade multiplier (independent of setVolume) to `to` over `seconds`;
    // `from` < 0 means "from wherever it is now". A soundtrack crossfade is two of these:
    //   play(next); fade(next, 0, 1, 2); fade(current, -1, 0, 2);
    virtual void fade(SoundHandle, f32 from, f32 to, f32 seconds) = 0;
    // Fade to silence and then actually stop — fading to zero and playing on is how a
    // "stopped" loop quietly burns a voice forever.
    virtual void stopFaded(SoundHandle, f32 seconds) = 0;

    // --- Spatial -----------------------------------------------------------------------
    // One listener (the camera or the player's head). For 2D, put everything on the z = 0
    // plane and spatial audio IS 2D — pan from x, attenuation from distance; there is no
    // separate 2D path to keep in sync.
    virtual void setListener(Vec3 position, Vec3 forward, Vec3 up = {0, 1, 0}) = 0;

    // Off by default: a UI click must not get quieter because the camera walked away.
    virtual void setSpatialization(SoundHandle, bool enabled) = 0;
    virtual void setPosition(SoundHandle, Vec3 worldPosition) = 0;
    // Full volume inside `minDistance`, attenuating toward `maxDistance` (inverse model).
    virtual void setAttenuation(SoundHandle, f32 minDistance, f32 maxDistance) = 0;
};

[[nodiscard]] std::unique_ptr<IAudioEngine> createAudioEngine();

}
