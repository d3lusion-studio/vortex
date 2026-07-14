#version 450

// Final resolve: colour grading, then a tone map of the HDR scene.
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
    vec4 params;   // x = exposure, y = tone mapper, z = contrast, w = saturation
    vec4 grade;    // xyz = colour filter, w = grading gamma
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 reinhard(vec3 x) { return x / (1.0 + x); }

// Uncharted 2 filmic curve: a softer shoulder than ACES, brighter mid-tones.
vec3 uncharted2(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    const float W = 11.2;
    vec3  c = ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
    float w = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
    return clamp(c / w, 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(sampler2D(uScene, uSampler), vUV).rgb * pc.params.x;

    // Grade in HDR, before the curve: the tone mapper then compresses the graded
    // image rather than the grade fighting an already-clipped one.
    hdr *= pc.grade.rgb;

    int mapper = int(pc.params.y);
    vec3 c;
    if      (mapper == 0) c = clamp(hdr, 0.0, 1.0);   // none / clamp
    else if (mapper == 1) c = reinhard(hdr);
    else if (mapper == 3) c = uncharted2(hdr);
    else                  c = aces(hdr);

    // Saturation and contrast, in the display range, about mid grey.
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, pc.params.w);
    c = clamp((c - 0.5) * pc.params.z + 0.5, 0.0, 1.0);

    if (pc.grade.w != 1.0) c = pow(max(c, 0.0), vec3(pc.grade.w));

    outColor = vec4(c, 1.0);
}
