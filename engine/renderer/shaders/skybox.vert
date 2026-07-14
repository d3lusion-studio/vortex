#version 450

// A fullscreen triangle whose fragments carry a world-space view ray, so the sky can
// be sampled from the environment cubemap without drawing any geometry. Depth is
// written at the far plane (w == z), so the sky loses to every real surface.
//
// Sets: 0 = frame uniform (this pipeline has no material set), 1 = scene textures.

layout(set = 0, binding = 0) uniform Sky {
    mat4 invViewProj;   // clip -> world
    vec4 cameraPos;     // xyz = eye position
    vec4 params;        // x = intensity, y = unused, z = unused, w = unused
} uSky;

layout(location = 0) out vec3 vDir;

void main() {
    // 3 vertices covering the screen: (-1,-1), (3,-1), (-1,3) in clip space.
    vec2 xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 1.0;

    // Unproject the near-plane point and aim from the eye through it.
    vec4 world = uSky.invViewProj * vec4(xy, 0.0, 1.0);
    vDir = world.xyz / world.w - uSky.cameraPos.xyz;

    // z = w puts the fragment exactly on the far plane after the perspective divide.
    gl_Position = vec4(xy, 1.0, 1.0);
}
