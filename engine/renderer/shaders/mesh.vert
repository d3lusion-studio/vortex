#version 450

// Frame data lives in set 1 (the material/shadow texture set is 0), matching the
// pipeline layout order: material set first, uniform set second.
layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 lightViewProj;  // world -> light clip space, for the shadow lookup
    vec4 lightDir;       // xyz: direction the light travels (world space)
    vec4 lightColor;     // rgb: colour, w: intensity
    vec4 ambient;        // rgb: ambient term
    vec4 cameraPos;      // xyz: eye position (world space)
} uFrame;

// WebGPU (and thus the browser) has no push constants. The WebGPU shader variant is
// compiled with VORTEX_NO_PUSH_CONSTANTS and this block becomes a uniform buffer in the
// reserved set 3, which the RHI fills from a ring buffer on every pushConstants() call.
#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 model;
    vec4 baseColor;
    vec4 material;   // x: metallic, y: roughness
} uPush;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vLightPos;

void main() {
    vec4 world = uPush.model * vec4(inPos, 1.0);
    vWorldPos  = world.xyz;
    // Normal matrix = transpose(inverse(model3x3)); correct under non-uniform scale.
    vNormal    = transpose(inverse(mat3(uPush.model))) * inNormal;
    vUV        = inUV;
    vLightPos  = uFrame.lightViewProj * world;
    gl_Position = uFrame.viewProj * world;
}
