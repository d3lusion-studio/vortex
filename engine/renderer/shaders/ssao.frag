#version 450

// Screen-space ambient occlusion. Ambient light in this renderer arrives from an
// environment cubemap, which knows nothing about the geometry in front of it — so a
// crevice receives exactly as much sky as an open field. SSAO puts the contact
// darkening back by asking, per pixel, how much of the hemisphere above the surface
// is blocked by whatever else the depth buffer happens to hold.
//
// It reads the G-buffer through the same set the lighting pass uses, so it needs no
// bind group of its own — only the normal (binding 1) and the depth (binding 3).
//   set 0 = G-buffer, set 1 = frame data (viewProj, cameraPos), set 3 = push constants.

layout(set = 0, binding = 1) uniform texture2D gNormal;   // rgb = world normal
layout(set = 0, binding = 3) uniform texture2D gDepth;
layout(set = 0, binding = 5) uniform texture2D gUnused;
layout(set = 0, binding = 6) uniform sampler   gSampler;

struct Light {
    vec4 position;
    vec4 direction;
    vec4 color;
    vec4 params;
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

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 invViewProj;   // clip -> world, to rebuild position from depth
    vec4 params;        // x = radius, y = intensity, z = bias, w = power
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

const int kSamples = 16;

// A fixed hemisphere kernel (+Z up, in tangent space). The lengths are bunched toward
// the origin so that samples cluster near the surface, where occlusion actually reads.
const vec3 kKernel[kSamples] = vec3[](
    vec3( 0.0490,  0.0339,  0.0490), vec3(-0.0640,  0.0500,  0.1000),
    vec3( 0.0900, -0.0700,  0.1200), vec3(-0.1300, -0.1000,  0.1500),
    vec3( 0.1700,  0.1200,  0.1000), vec3(-0.1900,  0.1600,  0.2200),
    vec3( 0.2400, -0.1800,  0.1700), vec3(-0.2700, -0.2400,  0.3000),
    vec3( 0.3300,  0.2800,  0.2100), vec3(-0.3800,  0.3400,  0.4000),
    vec3( 0.4500, -0.3600,  0.3000), vec3(-0.5000, -0.4400,  0.5200),
    vec3( 0.5800,  0.5000,  0.3900), vec3(-0.6600,  0.6000,  0.6800),
    vec3( 0.7600, -0.6400,  0.5000), vec3(-0.8500, -0.7600,  0.8800)
);

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 worldFromDepth(vec2 uv, float depth) {
    vec4 world = pc.invViewProj * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return world.xyz / world.w;
}

void main() {
    float depth = texture(sampler2D(gDepth, gSampler), vUV).r;
    if (depth >= 1.0) {          // sky: nothing to occlude
        outColor = vec4(1.0);
        return;
    }

    vec3 P = worldFromDepth(vUV, depth);
    vec3 N = normalize(texture(sampler2D(gNormal, gSampler), vUV).xyz);

    // Build a tangent frame around N, then spin it by a per-pixel angle. Without the
    // spin the same 16 directions are reused everywhere and the result bands; with it
    // the error becomes noise, which the blur pass removes.
    //
    // The frame is built from a reference axis chosen to be far from N, NOT from a
    // fixed random world vector: on a sphere the normals point every which way, so any
    // fixed vector is parallel to N somewhere, and Gram-Schmidt against it collapses to
    // zero there — normalize(0) is NaN, and a NaN reads as "occluded" all the way
    // through, which paints exactly the black ball a sphere came out as.
    vec3 axis = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 T0   = normalize(cross(axis, N));
    vec3 B0   = cross(N, T0);

    float angle = hash(vUV * 1024.0) * 6.2831853;
    vec3  T     = T0 * cos(angle) + B0 * sin(angle);
    vec3  B     = cross(N, T);
    mat3  TBN   = mat3(T, B, N);

    float radius    = pc.params.x;
    float occlusion = 0.0;

    for (int i = 0; i < kSamples; ++i) {
        vec3 samplePos = P + (TBN * kKernel[i]) * radius;

        vec4 clip = uFrame.viewProj * vec4(samplePos, 1.0);
        if (clip.w <= 0.0) continue;
        vec2 suv = (clip.xy / clip.w) * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;

        float sceneDepth = texture(sampler2D(gDepth, gSampler), suv).r;
        if (sceneDepth >= 1.0) continue;

        // Whatever the depth buffer holds at that pixel: is it in front of our sample?
        vec3  scenePos = worldFromDepth(suv, sceneDepth);
        float dSample  = distance(uFrame.cameraPos.xyz, samplePos);
        float dScene   = distance(uFrame.cameraPos.xyz, scenePos);

        // Ignore blockers far outside the radius, or a distant wall behind a near
        // object darkens it along its whole silhouette.
        float rangeCheck = smoothstep(0.0, 1.0,
                                      radius / max(abs(dSample - dScene), 0.0001));
        occlusion += (dScene < dSample - pc.params.z ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / float(kSamples)) * pc.params.y;
    ao = pow(clamp(ao, 0.0, 1.0), pc.params.w);
    outColor = vec4(vec3(ao), 1.0);
}
