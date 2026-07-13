#version 450

// Separable 9-tap Gaussian blur. Run twice (horizontal then vertical) with the
// step direction supplied in texels via the push constant.
layout(set = 0, binding = 0) uniform texture2D uTex;
layout(set = 0, binding = 1) uniform sampler   uSampler;

// WebGPU (and thus the browser) has no push constants. The WebGPU shader variant is
// compiled with VORTEX_NO_PUSH_CONSTANTS and this block becomes a uniform buffer in the
// reserved set 3, which the RHI fills from a ring buffer on every pushConstants() call.
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    vec4 dir;   // xy: texel step * blur direction
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 result = texture(sampler2D(uTex, uSampler), vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = pc.dir.xy * float(i);
        result += texture(sampler2D(uTex, uSampler), vUV + off).rgb * w[i];
        result += texture(sampler2D(uTex, uSampler), vUV - off).rgb * w[i];
    }
    outColor = vec4(result, 1.0);
}
