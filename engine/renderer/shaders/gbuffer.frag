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
layout(set = 0, binding = 5) uniform texture2D uLightmapTex;   // baked indirect light
layout(set = 0, binding = 6) uniform sampler   uMatSampler;

// Set 1 (the frame data) is bound for the vertex stage, which needs viewProj; this
// stage never reads it.

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    // The model matrix, as its first three ROWS. The fourth row of an affine transform
    // is always (0,0,0,1); not storing it is what freed the `extra` slot below, and the
    // push block is at Vulkan's guaranteed 128-byte ceiling with no room to spare.
    mat3x4 model;
    vec4 baseColor;
    vec4 material;   // x = metallic, y = roughness, z = normal scale, w = occlusion strength
    vec4 emissive;   // rgb = colour, w = strength
    vec4 params;     // x = alpha cutoff, y = UV scale, z = unlit, w = receives shadow
    vec4 extra;      // x = parallax scale, y = parallax layers, z = transmission, w = IOR
} uPush;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vLightPos;
layout(location = 4) in vec3 vTangent;
layout(location = 5) in vec3 vBitangent;
layout(location = 6) in vec4 vColor;
layout(location = 7) in vec3 vViewTS;
layout(location = 10) in vec2 vUV1;
layout(location = 11) in float vLightmap;
layout(location = 8) in vec4 vCurClip;
layout(location = 9) in vec4 vPrevClip;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outEmissive;
layout(location = 3) out vec4 outVelocity;   // xy = screen-space motion since last frame

// Parallax occlusion mapping. The height field lives in the normal map's alpha: 1 is the
// top of the surface, 0 the bottom of the grooves.
//
// March the view ray down through the height field in even steps until it passes below
// the surface, then interpolate between the last step above and the first below. That
// interpolation is the difference between clean relief and visible stair-steps, and it
// costs one extra compare.
vec2 parallaxUV(vec2 uv, vec3 viewTS) {
    float scale = uPush.extra.x;
    if (scale <= 0.0) return uv;

    // A ray coming in almost flat needs more steps to find the surface than one coming
    // straight down, so spend them where they are needed.
    float layers = mix(uPush.extra.y * 2.0, uPush.extra.y,
                       clamp(abs(viewTS.z), 0.0, 1.0));
    int   steps  = int(clamp(layers, 4.0, 64.0));

    float layerDepth = 1.0 / float(steps);
    vec2  shift      = (viewTS.xy / max(abs(viewTS.z), 0.15)) * scale;
    vec2  deltaUV    = shift * layerDepth;

    vec2  curUV      = uv;
    float curDepth   = 0.0;                                        // 0 at the top
    float mapDepth   = 1.0 - texture(sampler2D(uNormalTex, uMatSampler), curUV).a;

    for (int i = 0; i < steps && curDepth < mapDepth; ++i) {
        curUV    -= deltaUV;
        mapDepth  = 1.0 - texture(sampler2D(uNormalTex, uMatSampler), curUV).a;
        curDepth += layerDepth;
    }

    vec2  prevUV = curUV + deltaUV;
    float after  = mapDepth - curDepth;
    float before = (1.0 - texture(sampler2D(uNormalTex, uMatSampler), prevUV).a)
                 - (curDepth - layerDepth);

    float w = after / (after - before);
    return mix(curUV, prevUV, clamp(w, 0.0, 1.0));
}

void main() {
    // Parallax runs before anything is sampled: it decides *which* texel this fragment
    // actually shows, so every later lookup must use the shifted UV.
    vec2 uv = parallaxUV(vUV, normalize(vViewTS));
    vec4 base = uPush.baseColor * vColor * texture(sampler2D(uAlbedoTex, uMatSampler), uv);
    if (uPush.params.x > 0.0 && base.a < uPush.params.x) discard;

    vec3  mrTex     = texture(sampler2D(uMetalRoughTex, uMatSampler), uv).rgb;
    float metallic  = clamp(uPush.material.x * mrTex.b, 0.0, 1.0);
    float roughness = clamp(uPush.material.y * mrTex.g, 0.04, 1.0);
    float occlusion = mix(1.0, texture(sampler2D(uOcclusionTex, uMatSampler), uv).r,
                          uPush.material.w);

    vec3 nTex = texture(sampler2D(uNormalTex, uMatSampler), uv).xyz * 2.0 - 1.0;
    nTex.xy  *= uPush.material.z;
    mat3 TBN  = mat3(normalize(vTangent), normalize(vBitangent), normalize(vNormal));
    vec3 N    = normalize(TBN * nTex);
    if (!gl_FrontFacing) N = -N;

    vec3 emissive = uPush.emissive.rgb * uPush.emissive.w *
                    texture(sampler2D(uEmissiveTex, uMatSampler), uv).rgb;

    // Where this fragment was on screen last frame, versus where it is now. Storing the
    // difference is what separates this from the camera-only reprojection trick: an object
    // that moved under a still camera has a velocity here, and none in a depth reprojection.
    vec2 curUV  = (vCurClip.xy  / vCurClip.w)  * 0.5 + 0.5;
    vec2 prevUV = (vPrevClip.xy / vPrevClip.w) * 0.5 + 0.5;
    outVelocity = vec4(curUV - prevUV, 0.0, 1.0);

    // A lightmapped surface carries its baked light in the emissive target, and zeroes the
    // occlusion channel — which is what the lighting pass multiplies its image-based ambient
    // by, so this is how a G-buffer says "my ambient is already accounted for".
    if (vLightmap > 0.0) {
        vec3 baked = texture(sampler2D(uLightmapTex, uMatSampler), vUV1).rgb * vLightmap;
        emissive += baked * base.rgb;
        occlusion = 0.0;
    }

    outAlbedo   = vec4(base.rgb, metallic);
    // A signed float target holds the normal directly — no octahedral packing needed,
    // and the alpha channel carries the surface's shadow opt-out along with roughness.
    outNormal   = vec4(N, roughness);
    outEmissive = vec4(emissive, occlusion);
}
