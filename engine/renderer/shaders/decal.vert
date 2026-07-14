#version 450

// A decal is a box of space. Whatever surface the G-buffer happens to hold inside that
// box gets the decal's texture projected onto it — no geometry is added, nothing has to
// be UV-unwrapped, and the decal lands correctly on a wall, a floor and the step between
// them all at once.
//
// This stage just draws the box. All the work is in the fragment stage.

layout(location = 0) in vec3 inPos;

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
};

layout(set = 1, binding = 0) uniform Frame {
    mat4  viewProj;
    mat4  lightViewProj;
    vec4  cameraPos;
    vec4  ambient;
    vec4  fogColor;
    vec4  fogParams;
    vec4  shadowParams;
    vec4  misc;
    vec4  contactParams;
    vec4  cascadeSplits;
    vec4  cascadeTexelWorld;
    mat4  cascadeViewProj[4];
    Light lights[16];
    mat4  prevViewProj;
    mat4  invViewProj;
} uFrame;

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    // Both matrices as their first three rows — affine, so the fourth row says nothing.
    mat3x4 model;      // unit cube -> world
    mat3x4 invModel;   // world -> unit cube
    vec4   params;     // x = opacity, y = angle cutoff (cos)
} uPush;

void main() {
    vec4 h = vec4(inPos, 1.0);
    vec3 world = vec3(dot(uPush.model[0], h), dot(uPush.model[1], h), dot(uPush.model[2], h));
    gl_Position = uFrame.viewProj * vec4(world, 1.0);
}
