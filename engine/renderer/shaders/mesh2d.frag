#version 450

// An untextured mesh binds the renderer's 1x1 white pixel, so there is one pipeline
// rather than two and a shape costs exactly what a textured mesh costs.
layout(set = 0, binding = 0) uniform texture2D uTexture;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(sampler2D(uTexture, uSampler), vUV) * vColor;
}
