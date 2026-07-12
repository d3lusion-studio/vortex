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

    // Point a sprite at one frame of an atlas.
    void setRegion(const renderer::TextureRegion& region) {
        texture = region.texture;
        uv      = region.uv;
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

    void play(renderer::AnimationHandle next) {
        if (clip == next) return;
        clip     = next;
        time     = 0.0f;
        frame    = 0;
        playing  = true;
        finished = false;
    }

    void restart() {
        time     = 0.0f;
        frame    = 0;
        playing  = true;
        finished = false;
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
    f32                  metallic  = 0.0f;
    f32                  roughness = 0.5f;
};

}
