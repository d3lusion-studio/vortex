#version 450

// A full-screen triangle from gl_VertexIndex alone — no vertex buffer. Vertices
// 0,1,2 map to a triangle that covers the whole clip volume; the interpolated uv is
// (0,0) at the bottom-left of the screen and (1,1) at the top-right.
layout(location = 0) out vec2 vUV;

void main() {
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
