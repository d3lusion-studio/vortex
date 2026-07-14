#version 450

// Volumetric fog / light shafts.
//
// Ordinary fog tints a surface by how far away it is. This does the other half: it asks
// how much light is scattering toward the camera from the *air* along the way. Marching
// the view ray and testing each step against the shadow map is what separates a lit
// volume from a shadowed one — which is exactly what draws a god ray through a gap.
//
// Additive: the pass blends onto the lit scene rather than replacing it.
//
//   set 0 = G-buffer (only the depth is read), set 1 = frame data,
//   set 2 = scene (the shadow map), set 3 = push constants.

layout(set = 0, binding = 3) uniform texture2D gDepth;
layout(set = 0, binding = 5) uniform sampler   gSampler;

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
    mat4 invViewProj;
    vec4 params;   // x = density, y = steps, z = max distance, w = anisotropy
    vec4 color;    // rgb = scattering tint
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 world = pc.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return world.xyz / world.w;
}

// Henyey-Greenstein: how much light travelling one way scatters toward another. g > 0
// throws it forward, so looking into the beam is bright and looking across it is not.
float henyeyGreenstein(float cosTheta, float g) {
    float g2 = g * g;
    float d  = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(d, 0.0001), 1.5));
}

// The same cascade lookup the lit pass does, minus the PCF — a single tap is enough
// when the result is being averaged over dozens of steps anyway.
float shadowAt(vec3 worldPos) {
    if (uFrame.shadowParams.w < 0.5) return 1.0;

    int   count    = max(int(uFrame.misc.y), 1);
    float viewDist = distance(uFrame.cameraPos.xyz, worldPos);

    int c = count - 1;
    for (int i = 0; i < count; ++i)
        if (viewDist < uFrame.cascadeSplits[i]) { c = i; break; }

    vec4 lp   = uFrame.cascadeViewProj[c] * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    vec2 uv   = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z > 1.0) return 1.0;

    float tileScale = (count > 1) ? 0.5 : 1.0;
    vec2  tile      = (count > 1) ? vec2(float(c % 2), float(c / 2)) * 0.5 : vec2(0.0);

    float d = texture(sampler2D(uShadowMap, uShadowSampler), uv * tileScale + tile).r;
    return (proj.z - uFrame.shadowParams.y > d) ? 0.0 : 1.0;
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    float depth = texture(sampler2D(gDepth, gSampler), vUV).r;

    // How far to march: to the surface, or to the fog's own reach if the pixel is sky.
    vec3  target   = worldFromDepth(vUV, depth);
    vec3  toTarget = target - uFrame.cameraPos.xyz;
    float dist     = min(length(toTarget), pc.params.z);
    vec3  rayDir   = normalize(toTarget);

    int   steps = int(pc.params.y);
    float step  = dist / float(steps);

    // Dither the start of the ray. Marching from the same offset every pixel turns the
    // step size into visible banding; scattering the offset turns it into noise instead.
    float offset = hash(vUV * 1024.0);

    vec3  sun      = normalize(-uFrame.lights[0].direction.xyz);
    vec3  sunColor = uFrame.lights[0].color.rgb * uFrame.lights[0].color.w;
    float phase    = henyeyGreenstein(dot(rayDir, sun), pc.params.w);

    vec3  scattered   = vec3(0.0);
    float transmitted = 1.0;

    for (int i = 0; i < steps; ++i) {
        vec3 p = uFrame.cameraPos.xyz + rayDir * (step * (float(i) + offset));

        float lit      = shadowAt(p);
        float scatter  = pc.params.x * step;

        scattered   += transmitted * scatter * lit * phase * sunColor * pc.color.rgb;
        transmitted *= 1.0 - scatter;      // the air ahead is dimmed by the air behind
        if (transmitted < 0.01) break;
    }

    outColor = vec4(scattered, 1.0);
}
