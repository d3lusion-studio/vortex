#version 450

// Post-process: sample the offscreen scene colour, apply a tint and a vignette.
// Proves the second graph pass really consumes the first pass's output.
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Push {
    vec4 tint;   // rgb tint, a unused
} pc;

void main() {
    vec3 scene = texture(uScene, vUV).rgb;

    vec2  d        = vUV - vec2(0.5);
    float vignette = smoothstep(0.95, 0.25, dot(d, d) * 2.0);

    outColor = vec4(scene * pc.tint.rgb * vignette, 1.0);
}
