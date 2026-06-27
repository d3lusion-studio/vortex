#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/types.hpp"

#include <memory>

namespace vortex::audio {

struct SoundTag {};
using SoundHandle = Handle<SoundTag>;

class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    [[nodiscard]] virtual SoundHandle load(const char* path) = 0;
    virtual void unload(SoundHandle) = 0;

    virtual void play(SoundHandle, bool loop = false) = 0;
    virtual void stop(SoundHandle) = 0;

    virtual void setVolume(SoundHandle, f32 volume) = 0;
    virtual void setMasterVolume(f32 volume) = 0;
};

[[nodiscard]] std::unique_ptr<IAudioEngine> createAudioEngine();

}
