#version 450

// Straight copy. A pass cannot sample the target it is writing, so a transparent surface
// that wants to refract what is behind it needs the lit scene handed to it as a separate
// texture — this is what makes that copy.

layout(set = 0, binding = 0) uniform texture2D uSrc;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(sampler2D(uSrc, uSampler), vUV);
}
