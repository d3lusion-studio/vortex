#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <vector>

namespace vortex::renderer {

struct AnimationClipTag {};
using AnimationHandle = Handle<AnimationClipTag>;

// A flipbook: UV windows played back at a fixed rate, all on one texture page.
struct AnimationClip {
    std::vector<Rect>  frames;              // UV windows, in play order
    rhi::TextureHandle texture;             // page the frames live on
    f32                fps  = 12.0f;
    bool               loop = true;

    [[nodiscard]] f32 duration() const noexcept {
        if (frames.empty() || fps <= 0.0f) return 0.0f;
        return static_cast<f32>(frames.size()) / fps;
    }
};

// Clips are immutable once added, so animator components can hold plain handles
// and the system can resolve them without touching a map. Clips are never
// removed; a library lives as long as the scene that plays from it.
class AnimationLibrary {
public:
    AnimationHandle add(AnimationClip clip);

    // Frames [firstFrame, firstFrame + count) of a sheet, row-major.
    AnimationHandle addFromSheet(const SpriteSheet& sheet, u32 firstFrame, u32 count,
                                 f32 fps = 12.0f, bool loop = true);

    // Every frame of the sheet, in order.
    AnimationHandle addFromSheet(const SpriteSheet& sheet, f32 fps = 12.0f, bool loop = true);

    [[nodiscard]] const AnimationClip* get(AnimationHandle handle) const;

    [[nodiscard]] usize size() const { return m_clips.size(); }

    // Clips are append-only and a handle is just its index, so replaying this list
    // in order through add() reconstructs every handle a saved scene refers to.
    [[nodiscard]] const std::vector<AnimationClip>& clips() const { return m_clips; }

private:
    std::vector<AnimationClip> m_clips;
};

}
