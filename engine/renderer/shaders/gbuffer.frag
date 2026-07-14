#version 450

// Deferred fill. Reuses mesh.vert, so the sets match the forward pipeline:
//   set 0 = PBR material maps, set 1 = frame data, set 3 = push constants.
// The scene set is absent here — nothing is lit yet, it is only written down.
//
// Layout (matched by deferred.frag):
//   target 0  RGBA8   albedo.rgb, metallic
//   target 1  RGBA16F normal.xyz (world, signed), roughness
//   target 2  RGBA16F emissive.rgb, occlusion
// World position is not stored: the lighting pass reconstructs it from the depth
// buffer, which costs one matrix multiply and saves a whole render target.

layout(set = 0, binding = 0) uniform texture2D uAlbedoTex;
layout(set = 0, binding = 1) uniform texture2D uNormalTex;
layout(set = 0, binding = 2) uniform texture2D uMetalRoughTex;
layout(set = 0, binding = 3) uniform texture2D uEmissiveTex;
layout(set = 0, binding = 4) uniform texture2D uOcclusionTex;
layout(set = 0, binding = 5) uniform sampler   uMatSampler;

// Set 1 (the frame data) is bound for the vertex stage, which needs viewProj; this
// stage never reads it.

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 model;
    vec4 baseColor;
    vec4 material;   // x = metallic, y = roughness, z = normal scale, w = occlusion strength
    vec4 emissive;   // rgb = colour, w = strength
    vec4 params;     // x = alpha cutoff, y = UV scale, z = unlit, w = receives shadow
} uPush;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vLightPos;
layout(location = 4) in vec3 vTangent;
layout(location = 5) in vec3 vBitangent;
layout(location = 6) in vec4 vColor;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outEmissive;

void main() {
    vec4 base = uPush.baseColor * vColor * texture(sampler2D(uAlbedoTex, uMatSampler), vUV);
    if (uPush.params.x > 0.0 && base.a < uPush.params.x) discard;

    vec3  mrTex     = texture(sampler2D(uMetalRoughTex, uMatSampler), vUV).rgb;
    float metallic  = clamp(uPush.material.x * mrTex.b, 0.0, 1.0);
    float roughness = clamp(uPush.material.y * mrTex.g, 0.04, 1.0);
    float occlusion = mix(1.0, texture(sampler2D(uOcclusionTex, uMatSampler), vUV).r,
                          uPush.material.w);

    vec3 nTex = texture(sampler2D(uNormalTex, uMatSampler), vUV).xyz * 2.0 - 1.0;
    nTex.xy  *= uPush.material.z;
    mat3 TBN  = mat3(normalize(vTangent), normalize(vBitangent), normalize(vNormal));
    vec3 N    = normalize(TBN * nTex);
    if (!gl_FrontFacing) N = -N;

    vec3 emissive = uPush.emissive.rgb * uPush.emissive.w *
                    texture(sampler2D(uEmissiveTex, uMatSampler), vUV).rgb;

    outAlbedo   = vec4(base.rgb, metallic);
    // A signed float target holds the normal directly — no octahedral packing needed,
    // and the alpha channel carries the surface's shadow opt-out along with roughness.
    outNormal   = vec4(N, roughness);
    outEmissive = vec4(emissive, occlusion);
}
