#include "vortex/audio/audio.hpp"

#include "vortex/core/log.hpp"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include <miniaudio.h>

#include <memory>
#include <vector>

namespace vortex::audio {

namespace {

// --- Procedural data source ------------------------------------------------
//
// miniaudio pulls; the callback fills. The struct layout matters: ma_data_source_base must
// be the FIRST member, because miniaudio casts the ma_data_source* back to this.

struct ProceduralSource {
    ma_data_source_base base{};
    ProceduralFn        fn;
    ma_uint32           channels   = 0;
    ma_uint32           sampleRate = 0;
};

ma_result proceduralRead(ma_data_source* ds, void* out, ma_uint64 frameCount,
                         ma_uint64* framesRead) {
    auto* src = reinterpret_cast<ProceduralSource*>(ds);
    src->fn(static_cast<f32*>(out), static_cast<u32>(frameCount), src->channels,
            src->sampleRate);
    if (framesRead != nullptr) *framesRead = frameCount;
    return MA_SUCCESS;
}

// A procedural source has no timeline; "seeking" it is a no-op, not an error — play()
// rewinds sounds as a matter of course and must not fail on the ones with nothing to
// rewind.
ma_result proceduralSeek(ma_data_source*, ma_uint64) { return MA_SUCCESS; }

ma_result proceduralGetFormat(ma_data_source* ds, ma_format* format, ma_uint32* channels,
                              ma_uint32* sampleRate, ma_channel* channelMap,
                              size_t channelMapCap) {
    auto* src = reinterpret_cast<ProceduralSource*>(ds);
    if (format != nullptr) *format = ma_format_f32;
    if (channels != nullptr) *channels = src->channels;
    if (sampleRate != nullptr) *sampleRate = src->sampleRate;
    if (channelMap != nullptr)
        ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap,
                                     channelMapCap, src->channels);
    return MA_SUCCESS;
}

const ma_data_source_vtable kProceduralVtable = {
    proceduralRead,
    proceduralSeek,
    proceduralGetFormat,
    nullptr,   // no cursor
    nullptr,   // no length: it plays until stopped
    nullptr,   // looping is meaningless for an endless source
    0,
};

// ---------------------------------------------------------------------------

struct Slot {
    ma_sound sound{};
    // Generated sounds own their generator; it must outlive the sound reading from it.
    std::unique_ptr<ma_waveform>      wave;
    std::unique_ptr<ProceduralSource> proc;
    u32  generation = 0;
    bool alive      = false;
};

class MiniAudioEngine final : public IAudioEngine {
public:
    bool init() {
        if (ma_engine_init(nullptr, &m_engine) != MA_SUCCESS) return false;
        m_ok = true;
        return true;
    }

    ~MiniAudioEngine() override {
        if (!m_ok) return;
        for (auto& slot : m_slots)
            if (slot->alive) destroySlot(*slot);
        ma_engine_uninit(&m_engine);
    }

    // --- Sources -------------------------------------------------------------------

    SoundHandle load(const char* path, bool stream) override {
        auto slot = std::make_unique<Slot>();

        const ma_uint32 flags = stream ? MA_SOUND_FLAG_STREAM : 0;
        const ma_result r = ma_sound_init_from_file(&m_engine, path, flags, nullptr,
                                                    nullptr, &slot->sound);
        if (r != MA_SUCCESS) {
            VORTEX_ERROR("Audio", "Failed to load '%s' (ma_result=%d)", path, r);
            return {};
        }

        const SoundHandle h = adopt(std::move(slot));
        VORTEX_INFO("Audio", "Loaded '%s'%s", path, stream ? " (streaming)" : "");
        return h;
    }

    SoundHandle createWaveform(Waveform type, f32 frequencyHz, f32 amplitude) override {
        auto slot  = std::make_unique<Slot>();
        slot->wave = std::make_unique<ma_waveform>();

        ma_waveform_type maType = ma_waveform_type_sine;
        switch (type) {
            case Waveform::Sine:     maType = ma_waveform_type_sine; break;
            case Waveform::Square:   maType = ma_waveform_type_square; break;
            case Waveform::Triangle: maType = ma_waveform_type_triangle; break;
            case Waveform::Sawtooth: maType = ma_waveform_type_sawtooth; break;
        }

        const ma_waveform_config cfg = ma_waveform_config_init(
            ma_format_f32, ma_engine_get_channels(&m_engine),
            ma_engine_get_sample_rate(&m_engine), maType,
            static_cast<f64>(amplitude), static_cast<f64>(frequencyHz));
        if (ma_waveform_init(&cfg, slot->wave.get()) != MA_SUCCESS) return {};

        if (ma_sound_init_from_data_source(&m_engine, slot->wave.get(), 0, nullptr,
                                           &slot->sound) != MA_SUCCESS) {
            ma_waveform_uninit(slot->wave.get());
            return {};
        }
        return adopt(std::move(slot));
    }

    void setWaveformFrequency(SoundHandle h, f32 frequencyHz) override {
        Slot* s = resolve(h);
        if (s != nullptr && s->wave != nullptr)
            ma_waveform_set_frequency(s->wave.get(), static_cast<f64>(frequencyHz));
    }

    SoundHandle createProcedural(ProceduralFn fn) override {
        if (!fn) return {};
        auto slot  = std::make_unique<Slot>();
        slot->proc = std::make_unique<ProceduralSource>();
        slot->proc->fn         = std::move(fn);
        slot->proc->channels   = ma_engine_get_channels(&m_engine);
        slot->proc->sampleRate = ma_engine_get_sample_rate(&m_engine);

        ma_data_source_config cfg = ma_data_source_config_init();
        cfg.vtable = &kProceduralVtable;
        if (ma_data_source_init(&cfg, &slot->proc->base) != MA_SUCCESS) return {};

        if (ma_sound_init_from_data_source(&m_engine, slot->proc.get(), 0, nullptr,
                                           &slot->sound) != MA_SUCCESS) {
            ma_data_source_uninit(&slot->proc->base);
            return {};
        }
        return adopt(std::move(slot));
    }

    void unload(SoundHandle h) override {
        Slot* s = resolve(h);
        if (s == nullptr) return;
        destroySlot(*s);
        s->alive = false;
        ++s->generation;             // invalidate outstanding handles
        m_free.push_back(h.index);
    }

    // --- Playback ------------------------------------------------------------------

    void play(SoundHandle h, bool loop) override {
        Slot* s = resolve(h);
        if (s == nullptr) return;
        ma_sound_set_looping(&s->sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_seek_to_pcm_frame(&s->sound, 0);   // restart on replay
        ma_sound_start(&s->sound);
    }

    void stop(SoundHandle h) override {
        if (Slot* s = resolve(h)) ma_sound_stop(&s->sound);
    }

    // Stop WITHOUT the rewind: that one difference is all pause/resume is.
    void pause(SoundHandle h) override {
        if (Slot* s = resolve(h)) ma_sound_stop(&s->sound);
    }

    void resume(SoundHandle h) override {
        if (Slot* s = resolve(h)) ma_sound_start(&s->sound);
    }

    bool playing(SoundHandle h) const override {
        const Slot* s = resolve(h);
        return s != nullptr && ma_sound_is_playing(&s->sound) == MA_TRUE;
    }

    void seek(SoundHandle h, f32 seconds) override {
        Slot* s = resolve(h);
        if (s == nullptr) return;
        // Frames are counted at the SOURCE's rate, which need not be the engine's — an
        // 8 kHz voice line seeked at 48 kHz lands six times too far in.
        ma_uint32 rate = 0;
        if (ma_sound_get_data_format(&s->sound, nullptr, nullptr, &rate, nullptr, 0) !=
                MA_SUCCESS || rate == 0)
            return;
        ma_sound_seek_to_pcm_frame(&s->sound,
                                   static_cast<ma_uint64>(seconds * static_cast<f32>(rate)));
    }

    // --- Mixing --------------------------------------------------------------------

    void setVolume(SoundHandle h, f32 volume) override {
        if (Slot* s = resolve(h)) ma_sound_set_volume(&s->sound, volume);
    }

    void setMasterVolume(f32 volume) override { ma_engine_set_volume(&m_engine, volume); }

    void setPitch(SoundHandle h, f32 pitch) override {
        if (Slot* s = resolve(h)) ma_sound_set_pitch(&s->sound, pitch);
    }

    void setPan(SoundHandle h, f32 pan) override {
        if (Slot* s = resolve(h)) ma_sound_set_pan(&s->sound, pan);
    }

    void fade(SoundHandle h, f32 from, f32 to, f32 seconds) override {
        if (Slot* s = resolve(h))
            ma_sound_set_fade_in_milliseconds(&s->sound, from, to,
                                              static_cast<ma_uint64>(seconds * 1000.0f));
    }

    void stopFaded(SoundHandle h, f32 seconds) override {
        if (Slot* s = resolve(h))
            ma_sound_stop_with_fade_in_milliseconds(
                &s->sound, static_cast<ma_uint64>(seconds * 1000.0f));
    }

    // --- Spatial -------------------------------------------------------------------

    void setListener(Vec3 position, Vec3 forward, Vec3 up) override {
        ma_engine_listener_set_position(&m_engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&m_engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&m_engine, 0, up.x, up.y, up.z);
    }

    void setSpatialization(SoundHandle h, bool enabled) override {
        if (Slot* s = resolve(h))
            ma_sound_set_spatialization_enabled(&s->sound, enabled ? MA_TRUE : MA_FALSE);
    }

    void setPosition(SoundHandle h, Vec3 p) override {
        if (Slot* s = resolve(h)) ma_sound_set_position(&s->sound, p.x, p.y, p.z);
    }

    void setAttenuation(SoundHandle h, f32 minDistance, f32 maxDistance) override {
        Slot* s = resolve(h);
        if (s == nullptr) return;
        ma_sound_set_attenuation_model(&s->sound, ma_attenuation_model_inverse);
        ma_sound_set_min_distance(&s->sound, minDistance);
        ma_sound_set_max_distance(&s->sound, maxDistance);
    }

private:
    // Every freshly initialised sound goes through here: registered in the handle table,
    // and spatialization switched OFF — miniaudio defaults it on, but positional audio is
    // the opt-in here (a UI click must not get quieter because the camera walked away).
    SoundHandle adopt(std::unique_ptr<Slot> slot) {
        ma_sound_set_spatialization_enabled(&slot->sound, MA_FALSE);

        u32 index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
            slot->generation = m_slots[index]->generation;   // preserve generation
            m_slots[index]   = std::move(slot);
        } else {
            index = static_cast<u32>(m_slots.size());
            m_slots.push_back(std::move(slot));
        }
        m_slots[index]->alive = true;
        return SoundHandle{index, m_slots[index]->generation};
    }

    // Sound first, then its generator: the sound is the reader.
    static void destroySlot(Slot& s) {
        ma_sound_uninit(&s.sound);
        if (s.wave != nullptr) ma_waveform_uninit(s.wave.get());
        if (s.proc != nullptr) ma_data_source_uninit(&s.proc->base);
        s.wave.reset();
        s.proc.reset();
    }

    Slot* resolve(SoundHandle h) {
        if (h.index >= m_slots.size()) return nullptr;
        Slot* s = m_slots[h.index].get();
        if (!s->alive || s->generation != h.generation) return nullptr;
        return s;
    }
    const Slot* resolve(SoundHandle h) const {
        return const_cast<MiniAudioEngine*>(this)->resolve(h);
    }

    ma_engine                          m_engine{};
    bool                               m_ok = false;
    std::vector<std::unique_ptr<Slot>> m_slots;
    std::vector<u32>                   m_free;
};

} // namespace

std::unique_ptr<IAudioEngine> createAudioEngine() {
    auto engine = std::make_unique<MiniAudioEngine>();
    if (!engine->init()) {
        VORTEX_WARN("Audio", "No audio device available — audio disabled");
        return nullptr;
    }
    VORTEX_INFO("Audio", "miniaudio engine initialised");
    return engine;
}

}
