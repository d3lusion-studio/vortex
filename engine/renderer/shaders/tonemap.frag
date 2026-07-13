#version 450

// Final resolve: ACES filmic tone map of the HDR scene.
//
// The linear -> sRGB encode is NOT done here. The target is an sRGB-format view, so the
// hardware does it on write — the same encode the no-post path gets for free. Doing it here
// as well would apply the curve twice and wash the image out.
layout(set = 0, binding = 0) uniform texture2D uScene;
layout(set = 0, binding = 1) uniform sampler   uSampler;

// WebGPU (and thus the browser) has no push constants. The WebGPU shader variant is
// compiled with VORTEX_NO_PUSH_CONSTANTS and this block becomes a uniform buffer in the
// reserved set 3, which the RHI fills from a ring buffer on every pushConstants() call.
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    vec4 params;   // x: exposure
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(sampler2D(uScene, uSampler), vUV).rgb * pc.params.x;
    outColor = vec4(aces(hdr), 1.0);
}
