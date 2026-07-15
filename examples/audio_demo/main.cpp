// Audio, as a guided tour: every capability of IAudioEngine, one stage at a time, with the
// console narrating what the speakers should be doing. No window — audio needs time, not
// pixels. On a machine with no audio device it says so and exits cleanly, so it is safe in CI.
//
//   1. load + play        — a synthesized WAV from disk (the example writes its own asset)
//   2. control            — pause/resume, seek, pitch, pan on a running loop
//   3. tone               — createWaveform: play a pitch, sweep its frequency live
//   4. procedural         — createProcedural: the callback IS the sound (FM synthesis)
//   5. soundtrack         — two loops crossfaded by "game state" (fade/stopFaded)
//   6. spatial            — an emitter circling the listener's head (2D is z = 0)

#include "vortex/audio/audio.hpp"
#include "vortex/core/log.hpp"
#include "vortex/core/math/scalar.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

using namespace vortex;

namespace {

void rest(f32 seconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(seconds * 1000)));
}

void stage(const char* what) { std::printf("\n== %s\n", what); }

// A coin-style blip: two sine partials with an exponential decay, written as a minimal
// PCM16 mono WAV. The example fabricates its own asset so `load()` can be demonstrated
// without shipping a binary file in the repo.
bool writeCoinWav(const char* path) {
    constexpr u32 kRate   = 44100;
    constexpr f32 kLength = 0.35f;
    const u32 frames = static_cast<u32>(kRate * kLength);

    std::vector<i16> pcm(frames);
    for (u32 i = 0; i < frames; ++i) {
        const f32 t   = static_cast<f32>(i) / kRate;
        const f32 env = std::exp(-t * 12.0f);
        const f32 s   = 0.6f * std::sin(kTwoPi * 988.0f * t) +
                        0.4f * std::sin(kTwoPi * 1319.0f * t * (t < 0.08f ? 1.0f : 1.5f));
        pcm[i] = static_cast<i16>(clamp(s * env, -1.0f, 1.0f) * 32767.0f);
    }

    const u32 dataBytes = frames * 2;
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const auto u32le = [&](u32 v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16le = [&](u16 v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    out.write("RIFF", 4); u32le(36 + dataBytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32le(16);
    u16le(1); u16le(1);                    // PCM, mono
    u32le(kRate); u32le(kRate * 2);        // sample rate, byte rate
    u16le(2); u16le(16);                   // block align, bits
    out.write("data", 4); u32le(dataBytes);
    out.write(reinterpret_cast<const char*>(pcm.data()), dataBytes);
    return out.good();
}

} // namespace

int main() {
    auto audio = audio::createAudioEngine();
    if (audio == nullptr) {
        std::printf("no audio device — nothing to demonstrate, and that is fine\n");
        return 0;
    }
    audio->setMasterVolume(0.5f);

    // -------------------------------------------------------------------------------
    stage("load + play: a WAV from disk (freshly written by this very example)");
    const char* wavPath = "audio_demo_coin.wav";
    audio::SoundHandle coin;
    if (writeCoinWav(wavPath)) {
        coin = audio->load(wavPath);
        std::printf("   three coins, fire-and-forget — play() always restarts from the top\n");
        for (int i = 0; i < 3; ++i) {
            audio->play(coin);
            rest(0.5f);
        }
    } else {
        std::printf("   could not write %s — skipping\n", wavPath);
    }

    // -------------------------------------------------------------------------------
    stage("control: pause / resume / seek / pitch / pan on a running loop");
    const audio::SoundHandle hum = audio->createWaveform(audio::Waveform::Triangle, 220.0f, 0.2f);
    audio->play(hum, /*loop=*/true);

    std::printf("   playing... "); rest(1.0f);
    std::printf("paused... ");
    audio->pause(hum);
    rest(0.8f);
    std::printf("resumed (same spot — that is the difference from play())\n");
    audio->resume(hum);
    rest(1.0f);

    std::printf("   pitch: an octave up, then back\n");
    audio->setPitch(hum, 2.0f); rest(0.8f);
    audio->setPitch(hum, 1.0f); rest(0.8f);

    std::printf("   pan: left... right... centre\n");
    audio->setPan(hum, -1.0f); rest(0.8f);
    audio->setPan(hum, 1.0f);  rest(0.8f);
    audio->setPan(hum, 0.0f);  rest(0.4f);
    audio->stop(hum);

    // -------------------------------------------------------------------------------
    stage("tone: createWaveform + a live frequency sweep");
    const audio::SoundHandle tone = audio->createWaveform(audio::Waveform::Sine, 440.0f, 0.25f);
    audio->play(tone, true);
    std::printf("   A4 gliding up one octave\n");
    for (int i = 0; i <= 40; ++i) {
        audio->setWaveformFrequency(tone, 440.0f * std::pow(2.0f, static_cast<f32>(i) / 40.0f));
        rest(0.04f);
    }
    rest(0.4f);
    audio->stop(tone);

    // -------------------------------------------------------------------------------
    stage("procedural: the callback IS the sound (FM bell)");
    // All synthesis state lives in the lambda; it runs on the audio thread and touches
    // nothing else. This is the `Decodable` pattern: any code that can fill frames is a
    // source.
    const audio::SoundHandle bell = audio->createProcedural(
        [frame = 0u](f32* frames, u32 frameCount, u32 channels, u32 sampleRate) mutable {
            for (u32 i = 0; i < frameCount; ++i) {
                const f32 t   = static_cast<f32>(frame++) / static_cast<f32>(sampleRate);
                const f32 mod = std::sin(kTwoPi * 280.0f * t) * 6.0f * std::exp(-t * 1.5f);
                const f32 s   = 0.3f * std::sin(kTwoPi * 220.0f * t + mod) * std::exp(-t * 0.8f);
                for (u32 c = 0; c < channels; ++c) frames[i * channels + c] = s;
            }
        });
    audio->play(bell);
    rest(2.0f);
    audio->stop(bell);

    // -------------------------------------------------------------------------------
    stage("soundtrack: crossfade on a game-state change");
    const audio::SoundHandle calm   = audio->createWaveform(audio::Waveform::Sine, 261.6f, 0.22f);
    const audio::SoundHandle combat = audio->createWaveform(audio::Waveform::Sawtooth, 130.8f, 0.14f);

    std::printf("   exploring (calm theme)...\n");
    audio->play(calm, true);
    audio->fade(calm, 0.0f, 1.0f, 0.5f);
    rest(2.0f);

    std::printf("   AMBUSH — crossfade to combat\n");
    audio->play(combat, true);
    audio->fade(combat, 0.0f, 1.0f, 1.5f);
    audio->fade(calm, -1.0f, 0.0f, 1.5f);
    rest(2.5f);

    std::printf("   fight over — stopFaded ends it without a click\n");
    audio->stopFaded(combat, 1.0f);
    audio->fade(calm, -1.0f, 1.0f, 1.0f);
    rest(2.0f);
    audio->stopFaded(calm, 0.5f);
    rest(0.7f);

    // -------------------------------------------------------------------------------
    stage("spatial: an emitter circling the listener (drop z for the 2D case)");
    audio->setListener({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f});

    const audio::SoundHandle drone = audio->createWaveform(audio::Waveform::Triangle, 330.0f, 0.3f);
    audio->setSpatialization(drone, true);
    audio->setAttenuation(drone, 1.0f, 30.0f);
    audio->play(drone, true);

    std::printf("   two laps, radius swelling 2 -> 8 (pan from direction, volume from distance)\n");
    constexpr int kSteps = 160;
    for (int i = 0; i < kSteps; ++i) {
        const f32 a = kTwoPi * 2.0f * static_cast<f32>(i) / kSteps;
        const f32 r = 2.0f + 6.0f * static_cast<f32>(i) / kSteps;
        audio->setPosition(drone, {std::sin(a) * r, 0.0f, -std::cos(a) * r});
        rest(0.04f);
    }
    audio->stopFaded(drone, 0.5f);
    rest(0.7f);

    audio->unload(coin);
    std::printf("\ndone — every IAudioEngine capability exercised\n");
    return 0;
}
