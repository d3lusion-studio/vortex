#version 450

// Depth-only shadow pass. The vertex buffer is the full MeshVertex layout, but
// only the position is consumed; normal/uv attributes are ignored.
layout(push_constant) uniform Push {
    mat4 lightMVP;   // lightViewProj * model
} uPush;

layout(location = 0) in vec3 inPos;

void main() {
    gl_Position = uPush.lightMVP * vec4(inPos, 1.0);
}
