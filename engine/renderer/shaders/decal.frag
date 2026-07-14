#version 450

// Deferred decal projection.
//
// For each pixel the decal box covers, read the depth the G-buffer already wrote, rebuild
// that surface's world position, and push it into the box's own space. If it lands outside
// the unit cube, this pixel's surface is not inside the decal volume — discard. If it
// lands inside, the box's local XY *is* the decal's UV.
//
// The result blends into the G-buffer's albedo, so the decal is lit by the scene exactly
// like the surface it sits on: it goes down before any lighting happens.
//
//   set 0 = the decal's texture, set 1 = frame data, set 2 = scene (the G-buffer depth).

layout(set = 0, binding = 0) uniform texture2D uDecal;
layout(set = 0, binding = 1) uniform sampler   uDecalSampler;

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

layout(set = 2, binding = 0) uniform textureCube uIrradiance;
layout(set = 2, binding = 1) uniform textureCube uEnv;
layout(set = 2, binding = 2) uniform sampler     uIblSampler;
layout(set = 2, binding = 3) uniform texture2D   uShadowMap;
layout(set = 2, binding = 4) uniform sampler     uShadowSampler;
layout(set = 2, binding = 5) uniform texture2D   uSceneColor;
layout(set = 2, binding = 6) uniform texture2D   uSceneDepth;

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat3x4 model;
    mat3x4 invModel;
    vec4   params;   // x = opacity, y = angle cutoff (cos)
} uPush;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 screen = vec2(textureSize(sampler2D(uSceneDepth, uShadowSampler), 0));
    vec2 uv     = gl_FragCoord.xy / screen;

    float depth = texture(sampler2D(uSceneDepth, uShadowSampler), uv).r;
    if (depth >= 1.0) discard;   // sky: no surface to project onto

    vec4 world = uFrame.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    world /= world.w;

    // Into the decal box's own space, where the box is the unit cube about the origin.
    vec4 h     = vec4(world.xyz, 1.0);
    vec3 local = vec3(dot(uPush.invModel[0], h),
                      dot(uPush.invModel[1], h),
                      dot(uPush.invModel[2], h));

    if (any(greaterThan(abs(local), vec3(0.5)))) discard;   // surface is outside the box

    // A decal projects DOWN its box's local -Y. A surface facing away from that direction
    // would receive the texture stretched across it, which is the classic decal smear —
    // so it is dropped. The surface normal comes from the depth buffer's own gradient,
    // which costs nothing and needs no normal target bound here.
    vec3 N = normalize(cross(dFdx(world.xyz), dFdy(world.xyz)));
    vec3 projectAxis = normalize(vec3(uPush.model[0].y, uPush.model[1].y, uPush.model[2].y));
    if (abs(dot(N, projectAxis)) < uPush.params.y) discard;

    vec4 decal = texture(sampler2D(uDecal, uDecalSampler), local.xz + 0.5);

    // Fade toward the box's edges, so a decal does not end on a hard rectangular cut.
    float edge = 1.0 - max(abs(local.x), abs(local.z)) * 2.0;
    float fade = smoothstep(0.0, 0.15, edge);

    outColor = vec4(decal.rgb, decal.a * uPush.params.x * fade);
}
