#version 450

layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 lightDir;    // xyz: direction the light travels (world space)
    vec4 lightColor;  // rgb: colour, w: intensity
    vec4 ambient;     // rgb: ambient term
    vec4 cameraPos;   // xyz: eye position (world space)
} uFrame;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 baseColor;
} uPush;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;

void main() {
    vec4 world = uPush.model * vec4(inPos, 1.0);
    vWorldPos  = world.xyz;
    // Upper 3x3 of the model matrix; correct for rotation + uniform scale.
    vNormal    = mat3(uPush.model) * inNormal;
    vUV        = inUV;
    gl_Position = uFrame.viewProj * world;
}
