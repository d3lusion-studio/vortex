#version 450

// The 2D mesh path: arbitrary triangles with per-vertex colour and UVs, as opposed to
// the sprite path's fixed quad. Shapes, custom geometry and vertex-coloured meshes all
// come through here.
//
// The model matrix is folded into the view-projection on the CPU, so one 64-byte mvp is
// pushed rather than two matrices. Tint multiplies into the vertex colour here, which
// keeps the push-constant block vertex-only and the fragment stage free of uniforms.
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 uMVP;
    vec4 uTint;
};

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    gl_Position = uMVP * vec4(inPos, 0.0, 1.0);
    vUV    = inUV;
    vColor = inColor * uTint;
}
