#version 450

// Full-screen triangle. `time` arrives as a vertex-stage push constant (that is the
// stage the RHI exposes push constants to) and is forwarded, flat, to the fragment
// stage, which is where the animation is actually computed.
layout(push_constant) uniform Push { float time; } pc;

layout(location = 0) out vec2  vUV;
layout(location = 1) out float vTime;

void main() {
    vUV   = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vTime = pc.time;
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
