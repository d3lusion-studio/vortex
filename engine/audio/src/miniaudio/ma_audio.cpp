#include "vortex/audio/audio.hpp"

#include "vortex/core/log.hpp"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include <miniaudio.h>

#include <memory>
#include <vector>

namespace vortex::audio {

namespace {

struct Slot {
    ma_sound sound{};
    u32      generation = 0;
    bool     alive      = false;
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
            if (slot->alive) ma_sound_uninit(&slot->sound);
        ma_engine_uninit(&m_engine);
    }

    SoundHandle load(const char* path) override {
        auto slot = std::make_unique<Slot>();
        const ma_result r = ma_sound_init_from_file(&m_engine, path, 0, nullptr, nullptr,
                                                    &slot->sound);
        if (r != MA_SUCCESS) {
            VORTEX_ERROR("Audio", "Failed to load '%s' (ma_result=%d)", path, r);
            return {};
        }

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
        VORTEX_INFO("Audio", "Loaded '%s'", path);
        return SoundHandle{index, m_slots[index]->generation};
    }

    void unload(SoundHandle h) override {
        Slot* s = resolve(h);
        if (!s) return;
        ma_sound_uninit(&s->sound);
        s->alive = false;
        ++s->generation;             // invalidate outstanding handles
        m_free.push_back(h.index);
    }

    void play(SoundHandle h, bool loop) override {
        Slot* s = resolve(h);
        if (!s) return;
        ma_sound_set_looping(&s->sound, loop ? MA_TRUE : MA_FALSE);
        ma_sound_seek_to_pcm_frame(&s->sound, 0);   // restart on replay
        ma_sound_start(&s->sound);
    }

    void stop(SoundHandle h) override {
        if (Slot* s = resolve(h)) ma_sound_stop(&s->sound);
    }

    void setVolume(SoundHandle h, f32 volume) override {
        if (Slot* s = resolve(h)) ma_sound_set_volume(&s->sound, volume);
    }

    void setMasterVolume(f32 volume) override { ma_engine_set_volume(&m_engine, volume); }

private:
    Slot* resolve(SoundHandle h) {
        if (h.index >= m_slots.size()) return nullptr;
        Slot* s = m_slots[h.index].get();
        if (!s->alive || s->generation != h.generation) return nullptr;
        return s;
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
