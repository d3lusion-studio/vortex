#version 450

// A shader driven by dynamic data: the elapsed time. The output changes every frame,
// which is what makes readback verification possible — two different times must
// produce two different images.
layout(location = 0) in vec2  vUV;
layout(location = 1) in float vTime;

layout(location = 0) out vec4 outColor;

void main() {
    // Travelling bands plus a slow hue sweep.
    float wave = 0.5 + 0.5 * sin(vTime * 2.0 + vUV.x * 12.0 + vUV.y * 7.0);
    float glow = 0.5 + 0.5 * sin(vTime * 0.7);
    vec3  a = vec3(0.10, 0.18, 0.45);
    vec3  b = vec3(0.95, 0.45 + 0.3 * glow, 0.20);
    outColor = vec4(mix(a, b, wave), 1.0);
}
