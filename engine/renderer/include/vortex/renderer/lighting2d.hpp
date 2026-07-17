// 2D lighting: the look that a torch in a Terraria cave, a lamp in a Stardew night and a
// hard rim on a Skul sprite all share.
//
// The method is the standard one, and it is worth stating because it explains the whole
// class. Lights are not applied per sprite — nothing here knows what a sprite is. Instead:
//
//   1. Fill an offscreen buffer with the AMBIENT colour. This is "how lit is the world
//      where nothing is shining on it" — midnight blue, cave black, noon white.
//   2. Draw each light into it ADDITIVELY as a radial falloff. Overlapping lights add up,
//      which is what light does.
//   3. Draw that buffer over the scene with MULTIPLY. Ambient 1 leaves the world alone;
//      ambient 0 blacks it out; a light restores what it reaches.
//
// So a "light" costs one quad, the darkness costs one full-screen quad, and neither the
// sprite shader nor the tilemap ever hears about any of it. It is also why the ambient
// colour is a colour and not a scalar: multiply by a warm colour and the whole scene warms.
//
// The buffer is at a FRACTION of the framebuffer by default. Light is low frequency —
// there is nothing in a falloff that needs a pixel — and a quarter-resolution buffer is
// four times less fill, softer for free, and indistinguishable at this art scale.
#pragma once

#include "vortex/core/math/color.hpp"
#include "vortex/core/math/mat4.hpp"
#include "vortex/core/math/vec2.hpp"
#include "vortex/core/types.hpp"
#include "vortex/rhi/rhi_enums.hpp"
#include "vortex/rhi/rhi_handle.hpp"

#include <memory>
#include <vector>

namespace vortex::rhi { class IGraphicsDevice; class ICommandList; }

namespace vortex::renderer {

class SpriteBatch;

// The SHAPE of the falloff is not a per-light choice: it is baked once into a gradient
// texture that every light stretches and tints, which is what makes a light cost one quad
// and no state change. Radius and intensity move it; a hard-edged spotlight or a custom
// curve would need a shader, and that is a different feature.
struct Light2D {
    Vec2  position;              // world units
    f32   radius    = 100.0f;    // world units to full darkness
    Color color     = Color::white();
    f32   intensity = 1.0f;      // scales the colour; over 1 blows out to white
};

class Lighting2D {
public:
    // `targetFormat` is the format of the surface the composite will be drawn onto — the
    // pipeline is built for it, like every pipeline.
    Lighting2D(rhi::IGraphicsDevice& device, rhi::Format targetFormat, u32 maxLights = 1024);
    ~Lighting2D();

    Lighting2D(const Lighting2D&)            = delete;
    Lighting2D& operator=(const Lighting2D&) = delete;

    // Ambient light: what the world looks like where no light reaches.
    //
    // White with no lights submitted costs nothing — both phases skip, because the result
    // would be a multiply by 1. That is the cheap way to leave lighting on for a game whose
    // outdoor scenes are at noon and whose caves are not.
    Color ambient = Color::white();

    // Clear the light list. Lights are submitted every frame like sprites — there is no
    // retained light, because a torch that moves is the normal case.
    void begin();
    void add(const Light2D& light);
    [[nodiscard]] usize count() const;

    // --- Two phases, because a render pass cannot be nested ---------------------------
    //
    // The buffer is a render target of its own, so filling it is its own pass; the
    // composite has to happen inside the pass that drew the world, on top of it. There is
    // no way to express that as one call, and pretending otherwise would just move the
    // constraint somewhere it is harder to see.

    // Fill the light buffer. Call OUTSIDE any render pass.
    //
    // `viewProjection` must be the one the world was drawn with, or the lights land
    // somewhere other than the things they are supposed to be shining from.
    void buildBuffer(rhi::ICommandList& cmd, const Mat4& viewProjection, u32 targetWidth,
                     u32 targetHeight);

    // Multiply the buffer over whatever is already drawn. Call INSIDE the pass that drew
    // the world, after the world and before the UI: a HUD composited under this goes dark
    // at midnight along with everything else, which is never what a HUD wants.
    void composite(rhi::ICommandList& cmd, u32 targetWidth, u32 targetHeight);

    // Fraction of the target the light buffer is rendered at. Lower is cheaper and softer.
    void setResolutionScale(f32 scale);

    // True when this frame would multiply the world by 1 — ambient white and no lights.
    // Both phases check it; exposed so a caller can skip its own light-gathering too.
    [[nodiscard]] bool isNoop() const;

private:
    void ensureBuffer(u32 width, u32 height);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
