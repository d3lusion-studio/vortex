#version 450

// Cook-Torrance PBR (GGX): textured metallic-roughness material, up to 16 punctual
// lights (directional / point / spot, with spherical-area specular), a PCF shadow map
// for the sun, image-based ambient, and distance/height fog.
// Output is HDR linear colour; tone mapping and gamma happen in the post pass.

// set 0 — the material's five maps. All five are always bound; a material that has
// no map for a slot gets a 1x1 neutral texture, so the shader never branches.
layout(set = 0, binding = 0) uniform texture2D uAlbedoTex;
layout(set = 0, binding = 1) uniform texture2D uNormalTex;
layout(set = 0, binding = 2) uniform texture2D uMetalRoughTex;   // G = roughness, B = metallic
layout(set = 0, binding = 3) uniform texture2D uEmissiveTex;
layout(set = 0, binding = 4) uniform texture2D uOcclusionTex;    // R = ambient occlusion
layout(set = 0, binding = 5) uniform sampler   uMatSampler;

struct Light {
    vec4 position;    // xyz = world position, w = type (0 dir, 1 point, 2 spot)
    vec4 direction;   // xyz = direction of travel, w = range
    vec4 color;       // rgb = colour, w = intensity
    vec4 params;      // x = cos(inner), y = cos(outer), z = source radius, w = unused
};

layout(set = 1, binding = 0) uniform Frame {
    mat4  viewProj;
    mat4  lightViewProj;
    vec4  cameraPos;
    vec4  ambient;        // rgb = IBL tint, w = IBL intensity
    vec4  fogColor;       // rgb, w = density
    vec4  fogParams;      // x = start, y = end, z = mode, w = height falloff
    vec4  shadowParams;   // x = depth bias, y = normal bias, z = PCF radius, w = enabled
    vec4  misc;           // x = light count
    Light lights[16];
} uFrame;

// set 2 — the scene: image-based lighting cubemaps plus the sun's shadow map.
layout(set = 2, binding = 0) uniform textureCube uIrradiance;
layout(set = 2, binding = 1) uniform textureCube uEnv;
layout(set = 2, binding = 2) uniform sampler     uIblSampler;
layout(set = 2, binding = 3) uniform texture2D   uShadowMap;
layout(set = 2, binding = 4) uniform sampler     uShadowSampler;

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

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vLightPos;
layout(location = 4) in vec3 vTangent;
layout(location = 5) in vec3 vBitangent;
layout(location = 6) in vec4 vColor;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float distributionGGX(vec3 N, vec3 H, float rough) {
    float a2    = rough * rough * rough * rough;
    float NdotH = max(dot(N, H), 0.0);
    float d     = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdotV, float rough) {
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    return geometrySchlickGGX(max(dot(N, V), 0.0), rough) *
           geometrySchlickGGX(max(dot(N, L), 0.0), rough);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float rough) {
    return F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Karis' analytic environment BRDF approximation (avoids a BRDF LUT texture).
vec2 envBRDFApprox(float NoV, float rough) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// PCF over a (2r+1)^2 kernel. Returns 1.0 = fully lit, 0.0 = fully shadowed.
float shadowFactor(vec4 lightPos, float NdotL) {
    if (uFrame.shadowParams.w < 0.5) return 1.0;

    vec3 proj = lightPos.xyz / lightPos.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;   // outside the light frustum: treat as lit

    float bias  = max(uFrame.shadowParams.x * (1.0 - NdotL), uFrame.shadowParams.y);
    vec2  texel = 1.0 / vec2(textureSize(sampler2D(uShadowMap, uShadowSampler), 0));
    int   r     = int(uFrame.shadowParams.z);

    float sum   = 0.0;
    float count = 0.0;
    for (int x = -r; x <= r; ++x)
        for (int y = -r; y <= r; ++y) {
            float d = texture(sampler2D(uShadowMap, uShadowSampler),
                              uv + vec2(x, y) * texel).r;
            sum   += (proj.z - bias > d) ? 0.0 : 1.0;
            count += 1.0;
        }
    return sum / count;
}

// Smooth windowed inverse-square falloff (Frostbite). Zero at and past `range`,
// so a light's cost can be culled and its influence never pops at the boundary.
float distanceAttenuation(float dist, float range) {
    if (range <= 0.0) return 1.0 / max(dist * dist, 0.0001);
    float f  = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    return (f * f) / max(dist * dist, 0.0001);
}

void main() {
    vec4 albedoTex = texture(sampler2D(uAlbedoTex, uMatSampler), vUV);
    vec4 base      = uPush.baseColor * vColor * albedoTex;

    if (uPush.params.x > 0.0 && base.a < uPush.params.x) discard;

    vec3  albedo = base.rgb;
    vec3  mrTex  = texture(sampler2D(uMetalRoughTex, uMatSampler), vUV).rgb;
    float metallic  = clamp(uPush.material.x * mrTex.b, 0.0, 1.0);
    float roughness = clamp(uPush.material.y * mrTex.g, 0.04, 1.0);

    // Unlit: emit the base colour straight through (3D lines, gizmos, flat shapes).
    if (uPush.params.z > 0.5) {
        outColor = vec4(albedo, base.a);
        return;
    }

    float occlusion = mix(1.0, texture(sampler2D(uOcclusionTex, uMatSampler), vUV).r,
                          uPush.material.w);

    // Tangent-space normal map -> world space.
    vec3 nTex = texture(sampler2D(uNormalTex, uMatSampler), vUV).xyz * 2.0 - 1.0;
    nTex.xy  *= uPush.material.z;
    mat3 TBN  = mat3(normalize(vTangent), normalize(vBitangent), normalize(vNormal));
    vec3 N    = normalize(TBN * nTex);
    if (!gl_FrontFacing) N = -N;   // double-sided materials shade their back face too

    vec3  V     = normalize(uFrame.cameraPos.xyz - vWorldPos);
    float NdotV = max(dot(N, V), 0.0001);
    vec3  F0    = mix(vec3(0.04), albedo, metallic);

    // --- Punctual lights ---
    vec3 Lo = vec3(0.0);
    int  lightCount = int(uFrame.misc.x);
    for (int i = 0; i < lightCount; ++i) {
        Light lt   = uFrame.lights[i];
        int   type = int(lt.position.w);

        vec3  L;
        float atten = 1.0;
        if (type == 0) {                       // directional
            L = normalize(-lt.direction.xyz);
        } else {
            vec3  toLight = lt.position.xyz - vWorldPos;
            float dist    = length(toLight);
            L     = toLight / max(dist, 0.0001);
            atten = distanceAttenuation(dist, lt.direction.w);

            if (type == 2) {                   // spot: cone falloff
                float cosAngle = dot(normalize(lt.direction.xyz), -L);
                float t = clamp((cosAngle - lt.params.y) /
                                max(lt.params.x - lt.params.y, 0.0001), 0.0, 1.0);
                atten *= t * t;
            }
        }
        if (atten <= 0.0) continue;

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Spherical area light: shift the light vector to the point on the sphere
        // closest to the mirror ray, and widen roughness to keep energy sane. A
        // point light with radius 0 falls through unchanged.
        vec3  Ls    = L;
        float rough = roughness;
        if (lt.params.z > 0.0 && type != 0) {
            vec3  R          = reflect(-V, N);
            vec3  toLight    = lt.position.xyz - vWorldPos;
            vec3  centreToRay = dot(toLight, R) * R - toLight;
            vec3  closest    = toLight + centreToRay *
                               clamp(lt.params.z / max(length(centreToRay), 0.0001), 0.0, 1.0);
            Ls    = normalize(closest);
            rough = clamp(rough + lt.params.z / (2.0 * max(length(toLight), 0.0001)), 0.04, 1.0);
        }

        vec3  H   = normalize(V + Ls);
        float NDF = distributionGGX(N, H, rough);
        float G   = geometrySmith(N, V, Ls, rough);
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 specular = (NDF * G * F) / (4.0 * NdotV * max(dot(N, Ls), 0.0001) + 0.0001);
        vec3 kD       = (vec3(1.0) - F) * (1.0 - metallic);

        // Only light 0 casts; it is the sun the shadow map was rendered from. A
        // surface can also opt out of receiving (params.w), for flat/unlit props.
        float shadow = (i == 0 && type == 0)
                     ? mix(1.0, shadowFactor(vLightPos, NdotL), uPush.params.w)
                     : 1.0;

        vec3 radiance = lt.color.rgb * lt.color.w * atten;
        Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadow;
    }

    // --- Image-based ambient (IBL) ---
    vec3 R     = reflect(-V, N);
    vec3 Fr    = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kDamb = (1.0 - Fr) * (1.0 - metallic);

    vec3 irradiance = texture(samplerCube(uIrradiance, uIblSampler), N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // No prefiltered mip chain: fake roughness by blending the sharp reflection
    // toward the diffuse irradiance as roughness rises.
    vec3 reflection  = texture(samplerCube(uEnv, uIblSampler), R).rgb;
    vec3 prefiltered = mix(reflection, irradiance, roughness);
    vec2 envBRDF     = envBRDFApprox(NdotV, roughness);
    vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient  = (kDamb * diffuseIBL + specularIBL) * uFrame.ambient.rgb *
                    uFrame.ambient.w * occlusion;
    vec3 emissive = uPush.emissive.rgb * uPush.emissive.w *
                    texture(sampler2D(uEmissiveTex, uMatSampler), vUV).rgb;

    vec3 color = ambient + Lo + emissive;

    // --- Fog ---
    int fogMode = int(uFrame.fogParams.z);
    if (fogMode > 0) {
        float dist = length(uFrame.cameraPos.xyz - vWorldPos);
        float f    = 0.0;
        if (fogMode == 1) {                    // linear: start -> end
            f = clamp((dist - uFrame.fogParams.x) /
                      max(uFrame.fogParams.y - uFrame.fogParams.x, 0.0001), 0.0, 1.0);
        } else {
            float d = uFrame.fogColor.w * dist;
            f = (fogMode == 2) ? 1.0 - exp(-d)          // exponential
                               : 1.0 - exp(-d * d);     // exponential squared
        }
        // Height falloff: thin the fog out as the fragment rises, which is what
        // makes it read as atmosphere sitting in a valley rather than a grey wash.
        if (uFrame.fogParams.w > 0.0)
            f *= exp(-max(vWorldPos.y, 0.0) * uFrame.fogParams.w);

        color = mix(color, uFrame.fogColor.rgb, clamp(f, 0.0, 1.0));
    }

    outColor = vec4(color, base.a);
}
