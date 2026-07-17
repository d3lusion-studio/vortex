#pragma once
#include "vortex/core/handle.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/types.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <string>
#include <vector>

namespace vortex::renderer {

struct AnimationClipTag {};
using AnimationHandle = Handle<AnimationClipTag>;

// "On this frame, the sword is where it hurts."
//
// The 3D path has had these since anim::Clip; the flipbook did not, and every 2D game that
// needed one re-invented it — usually as "apply the effect halfway through the swing",
// which is a guess about art expressed in gameplay code. It goes wrong the moment an
// artist retimes the clip, and it goes wrong silently.
//
// The frame is a property of the ANIMATION, so it belongs to the clip.
struct AnimationEvent {
    u32         frame = 0;   // fires when playback reaches this frame
    std::string name;        // what it means is the game's business: "hit", "footstep"
};

// A flipbook: UV windows played back at a fixed rate, all on one texture page.
struct AnimationClip {
    std::vector<Rect>  frames;              // UV windows, in play order
    rhi::TextureHandle texture;             // page the frames live on
    f32                fps  = 12.0f;
    bool               loop = true;

    // Fire once each time playback ENTERS the named frame — including on every lap of a
    // looping clip, which is what makes a walk cycle's footsteps work.
    std::vector<AnimationEvent> events;

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

    // Attach an event to a clip already added. Clips are immutable to PLAYBACK — handles
    // are indices and the system resolves them without a map — but the library still owns
    // them, so authoring an event after the clip exists is a library operation, not a
    // reason to build the clip twice.
    void addEvent(AnimationHandle clip, u32 frame, std::string name);

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
