#version 450

// Deferred lighting: one fullscreen pass that shades every pixel the G-buffer filled,
// and paints the sky into the ones it did not. Lighting cost stops depending on how
// much geometry overlapped — which is the entire point of the deferred split.
//
//   set 0 = G-buffer (reusing the PBR material set layout, see below)
//   set 1 = frame data (lights, fog, shadow params)
//   set 2 = scene (IBL cubemaps + shadow map)
//   set 3 = push constants
//
// The G-buffer occupies the PBR material set layout, so the binding names below read
// oddly against it: binding 0..3 are the three G-buffer targets plus the depth buffer.
layout(set = 0, binding = 0) uniform texture2D gAlbedo;     // rgb = albedo, a = metallic
layout(set = 0, binding = 1) uniform texture2D gNormal;     // rgb = world normal, a = roughness
layout(set = 0, binding = 2) uniform texture2D gEmissive;   // rgb = emissive, a = occlusion
layout(set = 0, binding = 3) uniform texture2D gDepth;
layout(set = 0, binding = 4) uniform texture2D gAO;         // screen-space AO, or 1x1 white
layout(set = 0, binding = 5) uniform sampler   gSampler;

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
    vec4  ambient;
    vec4  fogColor;
    vec4  fogParams;
    vec4  shadowParams;
    vec4  misc;
    Light lights[16];
} uFrame;

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
    mat4 invViewProj;   // clip -> world, to rebuild position from depth
    vec4 params;        // x = skybox intensity, y = material AO strength, z = SSAO on
} pc;

layout(location = 0) in  vec2 vUV;
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

vec2 envBRDFApprox(float NoV, float rough) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.04);
    vec4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

float shadowFactor(vec3 worldPos, float NdotL) {
    if (uFrame.shadowParams.w < 0.5) return 1.0;

    vec4 lightPos = uFrame.lightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lightPos.xyz / lightPos.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0) return 1.0;

    float bias  = max(uFrame.shadowParams.x * (1.0 - NdotL), uFrame.shadowParams.y);
    vec2  texel = 1.0 / vec2(textureSize(sampler2D(uShadowMap, uShadowSampler), 0));
    int   r     = int(uFrame.shadowParams.z);

    float sum = 0.0, count = 0.0;
    for (int x = -r; x <= r; ++x)
        for (int y = -r; y <= r; ++y) {
            float d = texture(sampler2D(uShadowMap, uShadowSampler), uv + vec2(x, y) * texel).r;
            sum   += (proj.z - bias > d) ? 0.0 : 1.0;
            count += 1.0;
        }
    return sum / count;
}

float distanceAttenuation(float dist, float range) {
    if (range <= 0.0) return 1.0 / max(dist * dist, 0.0001);
    float f = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    return (f * f) / max(dist * dist, 0.0001);
}

// Pixel -> world, using the depth the G-buffer pass left behind.
vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 clip  = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = pc.invViewProj * clip;
    return world.xyz / world.w;
}

void main() {
    float depth = texture(sampler2D(gDepth, gSampler), vUV).r;

    vec3 worldPos = worldFromDepth(vUV, depth);
    vec3 V        = normalize(uFrame.cameraPos.xyz - worldPos);

    // Nothing was drawn here: the pixel is sky. Sampling the environment costs one
    // cubemap fetch and saves a separate skybox pass entirely.
    if (depth >= 1.0) {
        vec3 dir = normalize(worldPos - uFrame.cameraPos.xyz);
        outColor = vec4(texture(samplerCube(uEnv, uIblSampler), dir).rgb * pc.params.x, 1.0);
        return;
    }

    vec4  g0 = texture(sampler2D(gAlbedo,   gSampler), vUV);
    vec4  g1 = texture(sampler2D(gNormal,   gSampler), vUV);
    vec4  g2 = texture(sampler2D(gEmissive, gSampler), vUV);

    vec3  albedo    = g0.rgb;
    float metallic  = g0.a;
    vec3  N         = normalize(g1.xyz);
    float roughness = clamp(g1.a, 0.04, 1.0);
    vec3  emissive  = g2.rgb;
    float occlusion = mix(1.0, g2.a, pc.params.y);

    // The material's own baked occlusion and the screen-space one both darken ambient,
    // and they describe different things (surface detail vs. scene geometry), so they
    // multiply rather than replace one another.
    if (pc.params.z > 0.5)
        occlusion *= texture(sampler2D(gAO, gSampler), vUV).r;

    float NdotV = max(dot(N, V), 0.0001);
    vec3  F0    = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    int lightCount = int(uFrame.misc.x);
    for (int i = 0; i < lightCount; ++i) {
        Light lt   = uFrame.lights[i];
        int   type = int(lt.position.w);

        vec3  L;
        float atten = 1.0;
        if (type == 0) {
            L = normalize(-lt.direction.xyz);
        } else {
            vec3  toLight = lt.position.xyz - worldPos;
            float dist    = length(toLight);
            L     = toLight / max(dist, 0.0001);
            atten = distanceAttenuation(dist, lt.direction.w);
            if (type == 2) {
                float cosAngle = dot(normalize(lt.direction.xyz), -L);
                float t = clamp((cosAngle - lt.params.y) /
                                max(lt.params.x - lt.params.y, 0.0001), 0.0, 1.0);
                atten *= t * t;
            }
        }
        if (atten <= 0.0) continue;

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        vec3  Ls    = L;
        float rough = roughness;
        if (lt.params.z > 0.0 && type != 0) {
            vec3 R           = reflect(-V, N);
            vec3 toLight     = lt.position.xyz - worldPos;
            vec3 centreToRay = dot(toLight, R) * R - toLight;
            vec3 closest     = toLight + centreToRay *
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

        float shadow = (i == 0 && type == 0) ? shadowFactor(worldPos, NdotL) : 1.0;

        Lo += (kD * albedo / PI + specular) * lt.color.rgb * lt.color.w * atten * NdotL * shadow;
    }

    vec3 R     = reflect(-V, N);
    vec3 Fr    = fresnelSchlickRoughness(NdotV, F0, roughness);
    vec3 kDamb = (1.0 - Fr) * (1.0 - metallic);

    vec3 irradiance  = texture(samplerCube(uIrradiance, uIblSampler), N).rgb;
    vec3 reflection  = texture(samplerCube(uEnv, uIblSampler), R).rgb;
    vec3 prefiltered = mix(reflection, irradiance, roughness);
    vec2 envBRDF     = envBRDFApprox(NdotV, roughness);
    vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient = (kDamb * irradiance * albedo + specularIBL) *
                   uFrame.ambient.rgb * uFrame.ambient.w * occlusion;

    vec3 color = ambient + Lo + emissive;

    int fogMode = int(uFrame.fogParams.z);
    if (fogMode > 0) {
        float dist = length(uFrame.cameraPos.xyz - worldPos);
        float f    = 0.0;
        if (fogMode == 1) {
            f = clamp((dist - uFrame.fogParams.x) /
                      max(uFrame.fogParams.y - uFrame.fogParams.x, 0.0001), 0.0, 1.0);
        } else {
            float d = uFrame.fogColor.w * dist;
            f = (fogMode == 2) ? 1.0 - exp(-d) : 1.0 - exp(-d * d);
        }
        if (uFrame.fogParams.w > 0.0)
            f *= exp(-max(worldPos.y, 0.0) * uFrame.fogParams.w);
        color = mix(color, uFrame.fogColor.rgb, clamp(f, 0.0, 1.0));
    }

    outColor = vec4(color, 1.0);
}
