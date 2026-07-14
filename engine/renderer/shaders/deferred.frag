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
layout(set = 0, binding = 5) uniform texture2D gUnused;
layout(set = 0, binding = 6) uniform sampler   gSampler;

struct Light {
    vec4 position;    // xyz = world position, w = type (0 dir, 1 point, 2 spot)
    vec4 direction;   // xyz = direction of travel, w = range
    vec4 color;       // rgb = colour, w = intensity
    vec4 params;      // x = cos(inner), y = cos(outer), z = source radius, w = unused
};

layout(set = 1, binding = 0) uniform Frame {
    mat4  viewProj;
    mat4  lightViewProj;    // cascade 0; the vertex stage uses it for the forward path
    vec4  cameraPos;
    vec4  ambient;          // rgb = IBL tint, w = IBL intensity
    vec4  fogColor;         // rgb, w = density
    vec4  fogParams;        // x = start, y = end, z = mode, w = height falloff
    vec4  shadowParams;     // x = depth bias, y = normal bias, z = PCF radius, w = enabled
    vec4  misc;             // x = light count, y = cascade count
    vec4  contactParams;    // x = enabled, y = distance, z = steps, w = thickness
    vec4  cascadeSplits;    // view distance at which each cascade ends
    vec4  cascadeTexelWorld;  // world size of one shadow texel, per cascade
    mat4  cascadeViewProj[4];
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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Pixel -> world, using the depth the G-buffer pass left behind.
vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 clip  = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = pc.invViewProj * clip;
    return world.xyz / world.w;
}

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

// PCF over a (2r+1)^2 kernel, against the cascade this fragment falls in.
//
// The cascades share one depth texture, tiled 2x2, so the lookup is scaled and offset
// into the right quadrant — and clamped to it, or a PCF tap at a cascade's edge would
// read its neighbour's depth and stamp a hard seam across the ground.
// Returns 1.0 = fully lit, 0.0 = fully shadowed.
float shadowFactor(vec3 worldPos, vec3 N, float NdotL) {
    if (uFrame.shadowParams.w < 0.5) return 1.0;

    int   count    = max(int(uFrame.misc.y), 1);
    float viewDist = distance(uFrame.cameraPos.xyz, worldPos);

    int c = count - 1;
    for (int i = 0; i < count; ++i)
        if (viewDist < uFrame.cascadeSplits[i]) { c = i; break; }

    // Normal-offset bias. A far cascade's texel covers metres of ground, so the one
    // depth it stores is wrong by up to that much across the texel — which is what
    // stripes a flat floor with acne. Offsetting the lookup along the surface normal by
    // a texel's own world width sidesteps it, and unlike a bigger depth bias it does not
    // detach the shadow from its caster.
    float texelWorld = uFrame.cascadeTexelWorld[c];
    vec3  offsetPos  = worldPos + N * (texelWorld * 1.5 * (2.0 - NdotL));

    vec4 lp   = uFrame.cascadeViewProj[c] * vec4(offsetPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0)
        return 1.0;   // outside this cascade's frustum: treat as lit

    // One cascade gets the whole map; several share it as quadrants.
    float tileScale = (count > 1) ? 0.5 : 1.0;
    vec2  tile      = (count > 1) ? vec2(float(c % 2), float(c / 2)) * 0.5 : vec2(0.0);

    float bias  = max(uFrame.shadowParams.x * (1.0 - NdotL), uFrame.shadowParams.y);
    vec2  texel = 1.0 / vec2(textureSize(sampler2D(uShadowMap, uShadowSampler), 0));
    int   r     = int(uFrame.shadowParams.z);

    vec2 lo = tile + texel;
    vec2 hi = tile + vec2(tileScale) - texel;

    float sum   = 0.0;
    float count2 = 0.0;
    for (int x = -r; x <= r; ++x)
        for (int y = -r; y <= r; ++y) {
            vec2 auv = clamp(uv * tileScale + tile + vec2(x, y) * texel, lo, hi);
            float d  = texture(sampler2D(uShadowMap, uShadowSampler), auv).r;
            sum    += (proj.z - bias > d) ? 0.0 : 1.0;
            count2 += 1.0;
        }
    return sum / count2;
}

// March the depth buffer from the surface toward the light for a short distance. A
// shadow-map texel spans centimetres of world at best; the dark line where an object
// meets the floor is finer than that, and this is where it comes from.
//
// It can only see what the depth buffer holds, so a blocker off-screen or hidden behind
// something else does not exist. That is why the distance is kept short: it is a detail
// pass layered on the shadow map, not a second shadowing system.
float contactShadow(vec3 worldPos, vec3 N, vec3 L, vec2 uv) {
    if (uFrame.contactParams.x < 0.5) return 1.0;

    int   steps = int(uFrame.contactParams.z);
    float step  = uFrame.contactParams.y / float(steps);

    // Start the ray off the surface, along the normal. Marched from the surface itself,
    // a ray over a floor seen at a grazing angle keeps landing on pixels holding that
    // same floor, reads them as blockers, and rings the whole ground with false shadow.
    vec3 origin = worldPos + N * (uFrame.contactParams.y * 0.1);

    // Dither the phase, or the fixed step size prints its own arcs across the floor.
    float jitter = hash(uv * 1024.0);

    for (int i = 0; i < steps; ++i) {
        vec3 p = origin + L * (step * (float(i) + jitter));

        vec4 clip = uFrame.viewProj * vec4(p, 1.0);
        if (clip.w <= 0.0) break;
        vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) break;

        float sceneDepth = texture(sampler2D(gDepth, gSampler), suv).r;
        if (sceneDepth >= 1.0) continue;

        vec3  scenePos = worldFromDepth(suv, sceneDepth);
        float dRay     = distance(uFrame.cameraPos.xyz, p);
        float dScene   = distance(uFrame.cameraPos.xyz, scenePos);

        // The scene is in front of the ray: something blocks the light. `thickness` is
        // how far behind a surface the ray may be and still count as blocked by it — it
        // has to be roomy, because the moment the ray crosses an object's silhouette the
        // depth gap jumps to however far in front that object is, not to zero. Too tight
        // a value and the test never fires at all.
        float diff = dRay - dScene;
        if (diff > 0.0 && diff < uFrame.contactParams.w) return 0.0;
    }
    return 1.0;
}

float distanceAttenuation(float dist, float range) {
    if (range <= 0.0) return 1.0 / max(dist * dist, 0.0001);
    float f = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    return (f * f) / max(dist * dist, 0.0001);
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

        float shadow = (i == 0 && type == 0) ? shadowFactor(worldPos, N, NdotL) : 1.0;
        // Contact shadows apply to every light, not just the one with a shadow map:
        // the depth buffer is the same regardless of which way the light is coming from.
        shadow *= contactShadow(worldPos, N, L, vUV);

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
