#version 450

// Bloom bright-pass: keep only the part of each pixel above a luminance threshold.
layout(set = 0, binding = 0) uniform texture2D uScene;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(push_constant) uniform Push {
    vec4 params;   // x: threshold
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3  c   = texture(sampler2D(uScene, uSampler), vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    vec3  bright = c * max(lum - pc.params.x, 0.0) / max(lum, 1e-4);
    outColor = vec4(bright, 1.0);
}
