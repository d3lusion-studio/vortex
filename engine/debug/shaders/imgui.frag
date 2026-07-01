#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(set = 0, binding = 0) uniform texture2D uTexture;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor * texture(sampler2D(uTexture, uSampler), vUV);
}
