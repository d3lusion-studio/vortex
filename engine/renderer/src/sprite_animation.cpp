#include "vortex/renderer/sprite_animation.hpp"

namespace vortex::renderer {

AnimationHandle AnimationLibrary::add(AnimationClip clip) {
    m_clips.push_back(std::move(clip));
    return {static_cast<u32>(m_clips.size() - 1), 0u};
}

AnimationHandle AnimationLibrary::addFromSheet(const SpriteSheet& sheet, u32 firstFrame, u32 count,
                                               f32 fps, bool loop) {
    AnimationClip clip;
    clip.texture = sheet.texture;
    clip.fps     = fps;
    clip.loop    = loop;

    const u32 total = sheet.frameCount();
    const u32 last  = firstFrame < total ? (firstFrame + count < total ? firstFrame + count : total)
                                         : firstFrame;
    clip.frames.reserve(last - firstFrame);
    for (u32 i = firstFrame; i < last; ++i)
        clip.frames.push_back(sheet.frameUV(i));

    return add(std::move(clip));
}

AnimationHandle AnimationLibrary::addFromSheet(const SpriteSheet& sheet, f32 fps, bool loop) {
    return addFromSheet(sheet, 0u, sheet.frameCount(), fps, loop);
}

const AnimationClip* AnimationLibrary::get(AnimationHandle handle) const {
    if (!handle.valid() || handle.index >= m_clips.size()) return nullptr;
    return &m_clips[handle.index];
}

}
