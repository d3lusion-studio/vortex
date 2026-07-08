#version 450

// Cook-Torrance PBR (GGX) with a single directional light and a PCF shadow map.
// Output is HDR linear colour; tone mapping and gamma happen in the post pass.

layout(set = 0, binding = 0) uniform texture2D uShadowMap;
layout(set = 0, binding = 1) uniform sampler   uShadowSampler;

// Image-based lighting (set 2): pre-integrated irradiance + environment cubemaps.
layout(set = 2, binding = 0) uniform textureCube uIrradiance;
layout(set = 2, binding = 1) uniform textureCube uEnv;
layout(set = 2, binding = 2) uniform sampler     uIblSampler;

layout(set = 1, binding = 0) uniform Frame {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambient;
    vec4 cameraPos;
} uFrame;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 baseColor;
    vec4 material;   // x: metallic, y: roughness
} uPush;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vLightPos;

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

// 3x3 PCF. Returns 1.0 = fully lit, 0.0 = fully shadowed.
float shadowFactor(vec4 lightPos, float NdotL) {
    vec3 proj = lightPos.xyz / lightPos.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;   // outside the light frustum: treat as lit

    float bias  = max(0.0015 * (1.0 - NdotL), 0.0004);
    vec2  texel = 1.0 / vec2(textureSize(sampler2D(uShadowMap, uShadowSampler), 0));
    float sum   = 0.0;
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float d = texture(sampler2D(uShadowMap, uShadowSampler),
                              uv + vec2(x, y) * texel).r;
            sum += (proj.z - bias > d) ? 0.0 : 1.0;
        }
    return sum / 9.0;
}

void main() {
    vec3  albedo    = uPush.baseColor.rgb;
    float metallic  = clamp(uPush.material.x, 0.0, 1.0);
    float roughness = clamp(uPush.material.y, 0.04, 1.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uFrame.cameraPos.xyz - vWorldPos);
    vec3 L = normalize(-uFrame.lightDir.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    vec3 F0       = mix(vec3(0.04), albedo, metallic);
    vec3 radiance = uFrame.lightColor.rgb * uFrame.lightColor.w;

    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  specular = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001);
    vec3  kD       = (vec3(1.0) - F) * (1.0 - metallic);

    float shadow = shadowFactor(vLightPos, NdotL);
    vec3  Lo     = (kD * albedo / PI + specular) * radiance * NdotL * shadow;

    // --- Image-based ambient (IBL) ---
    float NdotV = max(dot(N, V), 0.0);
    vec3  R     = reflect(-V, N);
    vec3  Fr    = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3  kDamb = (1.0 - Fr) * (1.0 - metallic);

    vec3 irradiance = texture(samplerCube(uIrradiance, uIblSampler), N).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    // No prefiltered mip chain: fake roughness by blending the sharp reflection
    // toward the diffuse irradiance as roughness rises.
    vec3 reflection = texture(samplerCube(uEnv, uIblSampler), R).rgb;
    vec3 prefiltered = mix(reflection, irradiance, roughness);
    vec2 envBRDF     = envBRDFApprox(NdotV, roughness);
    vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient = (kDamb * diffuseIBL + specularIBL) * uFrame.ambient.rgb;
    outColor = vec4(ambient + Lo, uPush.baseColor.a);
}
