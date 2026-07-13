#version 450

// One sprite is one instance. The quad's four corners come from gl_VertexIndex
// rather than from a vertex buffer, so a sprite costs a single 56-byte record
// instead of four 32-byte vertices — and the CPU never transforms a corner.
//
// WebGPU (and thus the browser) has no push constants. The WebGPU shader variant is
// compiled with VORTEX_NO_PUSH_CONSTANTS and this block becomes a uniform buffer in the
// reserved set 3, which the RHI fills from a ring buffer on every pushConstants() call.
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 uViewProjection;
};

// The sprite's 2D affine transform, unrolled. inAxes holds the two basis columns
// (rotation and scale) and inTranslate the position; the third row is implicit.
layout(location = 0) in vec4 inAxes;        // m00, m10, m01, m11
layout(location = 1) in vec2 inTranslate;   // m03, m13
layout(location = 2) in vec4 inUV;          // x, y, width, height  (negative w/h flips)
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    // Triangle-strip corner order: top-left, top-right, bottom-left, bottom-right.
    // The unit quad spans +/-0.5, matching the transform every RenderItem carries.
    const vec2 kCorners[4] = vec2[4](
        vec2(-0.5,  0.5),
        vec2( 0.5,  0.5),
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5));
    const vec2 kUVs[4] = vec2[4](
        vec2(0.0, 0.0),
        vec2(1.0, 0.0),
        vec2(0.0, 1.0),
        vec2(1.0, 1.0));

    const vec2 corner = kCorners[gl_VertexIndex];
    const vec2 world  = inAxes.xy * corner.x + inAxes.zw * corner.y + inTranslate;

    gl_Position = uViewProjection * vec4(world, 0.0, 1.0);
    vUV    = inUV.xy + kUVs[gl_VertexIndex] * inUV.zw;
    vColor = inColor;
}
