#version 450

// Set layout for the lit mesh pipeline:
//   set 0 = PBR material maps, set 1 = frame data, set 2 = scene (IBL + shadow map),
//   set 3 = push constants (only on WebGPU, which has none).

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;   // xyz = tangent, w = handedness (+1/-1)
layout(location = 4) in vec4 inColor;     // per-vertex tint

struct Light {
    vec4 position;    // xyz = world position, w = type (0 dir, 1 point, 2 spot)
    vec4 direction;   // xyz = direction of travel, w = range
    vec4 color;       // rgb = colour, w = intensity
    vec4 params;      // x = cos(inner), y = cos(outer), z = source radius, w = unused
};

layout(set = 1, binding = 0) uniform Frame {
    mat4  viewProj;
    mat4  lightViewProj;  // world -> light clip space, for the shadow lookup
    vec4  cameraPos;      // xyz = eye position (world space)
    vec4  ambient;        // rgb = IBL tint, w = IBL intensity
    vec4  fogColor;       // rgb, w = density
    vec4  fogParams;      // x = start, y = end, z = mode, w = height falloff
    vec4  shadowParams;   // x = depth bias, y = normal bias, z = PCF radius, w = enabled
    vec4  misc;           // x = light count
    Light lights[16];
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
    vec4 material;   // x = metallic, y = roughness, z = normal scale, w = occlusion strength
    vec4 emissive;   // rgb = colour, w = strength
    vec4 params;     // x = alpha cutoff, y = UV scale, z = unlit, w = unused
} uPush;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vLightPos;
layout(location = 4) out vec3 vTangent;
layout(location = 5) out vec3 vBitangent;
layout(location = 6) out vec4 vColor;

void main() {
    vec4 world = uPush.model * vec4(inPos, 1.0);
    vWorldPos  = world.xyz;

    // Normal matrix = transpose(inverse(model3x3)); correct under non-uniform scale.
    mat3 nrm   = transpose(inverse(mat3(uPush.model)));
    vNormal    = normalize(nrm * inNormal);
    vTangent   = normalize(nrm * inTangent.xyz);
    vBitangent = cross(vNormal, vTangent) * inTangent.w;

    vUV       = inUV * uPush.params.y;
    vColor    = inColor;
    vLightPos = uFrame.lightViewProj * world;

    gl_Position = uFrame.viewProj * world;
}
