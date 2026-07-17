#pragma once
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/quat.hpp"
#include "vortex/core/math/rect.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/math/vec3.hpp"
#include "vortex/core/math/vec4.hpp"
#include "vortex/core/types.hpp"
#include "vortex/ecs/entity.hpp"
#include "vortex/renderer/mesh.hpp"
#include "vortex/renderer/render_item.hpp"
#include "vortex/renderer/sprite_animation.hpp"
#include "vortex/renderer/sprite_atlas.hpp"
#include "vortex/rhi/rhi_handle.hpp"

namespace vortex::ecs {


struct Transform2D {
    Vec2 position{0.0f, 0.0f};
    f32  rotation = 0.0f;   // radians, +Z (counter-clockwise)
    Vec2 scale{1.0f, 1.0f};
};

struct WorldTransform2D {
    Mat4 matrix = Mat4::identity();
};

struct Parent {
    Entity value;
};

struct SpriteComp {
    rhi::TextureHandle texture;
    Vec4               color{1.0f, 1.0f, 1.0f, 1.0f};
    Rect               uv = kFullUV;
    Vec2               size{1.0f, 1.0f};   // local quad size before transform
    i32                layer = 0;          // painter order

    // Depth-sort by world position instead of by hand: with this on, the drawn order is
    // `layer` MINUS the entity's world y, so a sprite lower down the screen covers one
    // further up. It is what makes a top-down scene look solid — the player walks behind
    // a tree whose trunk is below them and in front of one above — and every such game
    // otherwise re-writes `sprite.layer = -position.y` in its own update loop.
    //
    // `layer` keeps working as a band while this is on: leave it at 0 for anything
    // standing on the ground, and give a whole class of sprites (a flying thing, a
    // ground decal) a constant to lift it clear of the sort.
    bool ySort = false;

    // Where the sprite's "feet" are, relative to its origin. A tree drawn from its base
    // sorts on its base; one drawn from its centre wants -halfHeight here.
    f32 ySortOffset = 0.0f;

    // Which point of the quad sits on the entity's position, in unit coordinates:
    // (0,0) is the bottom-left corner, (1,1) the top-right, (0.5,0.5) the centre.
    // It is also what the sprite rotates and scales about, so a character standing
    // on the ground wants (0.5, 0) and a health bar filling rightwards wants (0, 0.5).
    Vec2 anchor{0.5f, 0.5f};

    // Mirror the drawn image without touching the transform — the usual way to face
    // a character left. Flipping via a negative scale would also mirror the anchor
    // and any child entity, which is almost never what a game wants.
    bool flipX = false;
    bool flipY = false;

    // Nearest for pixel art, Repeat to tile the texture across the quad by taking
    // `uv` past 1.0. Sprites sharing a texture but not a sampler cost two draw calls,
    // so a scene normally settles on one.
    renderer::SpriteSampler sampler = renderer::SpriteSampler::LinearClamp;

    // Point a sprite at one frame of an atlas.
    void setRegion(const renderer::TextureRegion& region) {
        texture = region.texture;
        uv      = region.uv;
    }

    // The offset from the entity's origin to the quad's centre, in the sprite's own
    // (pre-transform) space. The draw path and the culler must agree on this, so it
    // lives here rather than being open-coded in both.
    [[nodiscard]] Vec2 anchorOffset() const noexcept {
        return {(0.5f - anchor.x) * size.x, (0.5f - anchor.y) * size.y};
    }
};

// Plays an AnimationClip onto the entity's SpriteComp. The system owns `frame`
// and `finished`; gameplay owns the rest. Swapping `clip` without resetting
// `time` cross-fades badly, so use play() when changing clip.
struct SpriteAnimator {
    renderer::AnimationHandle clip;
    f32  time     = 0.0f;    // seconds elapsed into the clip
    f32  speed    = 1.0f;    // negative plays the clip backwards
    u32  frame    = 0;       // resolved frame index, written each update
    bool playing  = true;
    bool finished = false;   // latched on the last frame of a non-looping clip

    // True until the animation system has shown a frame of the current clip.
    //
    // Without it, "entered a new frame" is `frame != lastFrame` — and a clip that starts on
    // frame 0 never enters frame 0, so an event authored there (the footfall a walk cycle
    // begins on) is silently skipped on the first lap and every restart.
    bool freshClip = true;

    void play(renderer::AnimationHandle next) {
        if (clip == next) return;
        clip      = next;
        time      = 0.0f;
        frame     = 0;
        playing   = true;
        finished  = false;
        freshClip = true;
    }

    void restart() {
        time      = 0.0f;
        frame     = 0;
        playing   = true;
        finished  = false;
        freshClip = true;
    }
};

struct Velocity {
    Vec2 value{0.0f, 0.0f};
};

struct Transform3D {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct MeshComp {
    renderer::MeshHandle mesh;
    Vec4                 color{1.0f, 1.0f, 1.0f, 1.0f};
    // Used only when `material` is unset — the quick path for an untextured surface.
    f32                  metallic  = 0.0f;
    f32                  roughness = 0.5f;

    renderer::MaterialHandle material;
    bool                     castsShadow    = true;
    bool                     receivesShadow = true;

    // Filled by extractMeshes with last frame's world matrix, so motion blur can tell
    // that this entity moved under a still camera. Not for the game to set.
    Mat4 prevModel     = Mat4::identity();
    bool hasPrevModel  = false;
};

}
