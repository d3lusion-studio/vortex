#version 450

// Depth-only shadow pass. The vertex buffer is the full MeshVertex layout, but
// only the position is consumed; normal/uv attributes are ignored.
// WebGPU (and thus the browser) has no push constants. The WebGPU shader variant is
// compiled with VORTEX_NO_PUSH_CONSTANTS and this block becomes a uniform buffer in the
// reserved set 3, which the RHI fills from a ring buffer on every pushConstants() call.
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 lightMVP;   // lightViewProj * model
} uPush;

layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = uPush.lightMVP * vec4(inPos, 1.0);
}
