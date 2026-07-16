#version 450

// One quad, drawn many times in a single draw call. The four corners come from
// gl_VertexIndex (a triangle strip), while the per-instance offset and colour come
// from a per-instance vertex buffer indexed automatically by gl_InstanceIndex — so
// the whole grid is one draw, one buffer, no push constants.
layout(location = 0) in vec2 inOffset;   // clip-space centre of this instance
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

void main() {
    const vec2 kCorners[4] = vec2[4](
        vec2(-1.0,  1.0), vec2(1.0,  1.0),
        vec2(-1.0, -1.0), vec2(1.0, -1.0));
    const vec2 corner = kCorners[gl_VertexIndex] * 0.06;   // small quad
    gl_Position = vec4(inOffset + corner, 0.0, 1.0);
    vColor = inColor;
}
