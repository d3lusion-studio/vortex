#version 450

// Depth-only, but with one job: throw away the fragments the material would have thrown away
// in the lit pass. Without this, a cutout leaf casts the shadow of the quad it is painted on
// — and the depth buffer, being the only output, records that lie for everything that later
// reads it.
//
// The test is exactly the lit pass's: same texture, same UV, same cutoff. Anything else and a
// surface's shadow disagrees with the surface it belongs to.

layout(set = 0, binding = 0) uniform texture2D uAlbedoTex;
layout(set = 0, binding = 6) uniform sampler   uMatSampler;

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 lightMVP;
    vec4 params;   // x = alpha cutoff, y = UV scale
} uPush;

layout(location = 0) in vec2 vUV;

void main() {
    // A cutoff of 0 means the material is not a cutout, and the fetch is skipped entirely:
    // an opaque mesh should not pay for a texture lookup whose result it ignores.
    if (uPush.params.x > 0.0) {
        float alpha = texture(sampler2D(uAlbedoTex, uMatSampler), vUV).a;
        if (alpha < uPush.params.x) discard;
    }
}
