#version 450

// World-space debug lines. Each vertex carries its own colour; the view-projection
// arrives as a push constant (a uniform buffer in set 3 on WebGPU, which has no
// push constants).
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 uViewProjection;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

void main() {
    gl_Position = uViewProjection * vec4(inPos, 1.0);
    vColor = inColor;
}
