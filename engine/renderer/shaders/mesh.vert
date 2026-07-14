#version 450

// Set layout for the lit mesh pipeline:
//   set 0 = PBR material maps, set 1 = frame data, set 2 = scene (IBL + shadow map),
//   set 3 = push constants (only on WebGPU, which has none).

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;   // xyz = tangent, w = handedness (+1/-1)
layout(location = 4) in vec4 inColor;     // per-vertex tint
layout(location = 5) in vec2 inUV1;      // the lightmap's non-overlapping unwrap
layout(location = 6) in uvec4 inJoints;  // up to four bones move this vertex
layout(location = 7) in vec4  inWeights;

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
    mat4  prevViewProj;
} uFrame;

// WebGPU (and thus the browser) has no push constants. The WebGPU shader variant is
// compiled with VORTEX_NO_PUSH_CONSTANTS and this block becomes a uniform buffer in the
// reserved set 3, which the RHI fills from a ring buffer on every pushConstants() call.
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

// The model matrix is packed as three rows; these unpack what each stage needs of it.
vec3 modelPoint(vec3 p) {
    vec4 h = vec4(p, 1.0);
    return vec3(dot(uPush.model[0], h), dot(uPush.model[1], h), dot(uPush.model[2], h));
}

mat3 modelLinear() {   // the upper-left 3x3, rebuilt column by column
    return mat3(vec3(uPush.model[0].x, uPush.model[1].x, uPush.model[2].x),
                vec3(uPush.model[0].y, uPush.model[1].y, uPush.model[2].y),
                vec3(uPush.model[0].z, uPush.model[1].z, uPush.model[2].z));
}


layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vLightPos;
layout(location = 4) out vec3 vTangent;
layout(location = 5) out vec3 vBitangent;
layout(location = 6) out vec4 vColor;
// Tangent-space view direction — parallax needs it, and computing it here keeps the
// G-buffer's fragment stage from needing the frame uniforms at all.
layout(location = 7) out vec3 vViewTS;
// Where this fragment is now, and where it was last frame — in clip space. The G-buffer
// turns the difference into a velocity, which is what lets motion blur smear an object
// that moved on its own while the camera held still.
layout(location = 8) out vec4 vCurClip;
layout(location = 9) out vec4 vPrevClip;
// The lightmap UV is NOT scaled by uvScale: a tiling factor is meaningful for a repeating
// texture and meaningless for an unwrap where each texel is one point on the surface.
layout(location = 10) out vec2 vUV1;
layout(location = 11) out float vLightmap;

// Per-instance data, indexed by gl_InstanceIndex. The draw passes the instance's index
// as firstInstance, so this costs no push-constant bytes — of which there are none left.
// Per-instance data, indexed by gl_InstanceIndex. Carries what the 128-byte push block
// cannot: the previous model matrix, and this instance's lightmap intensity.
struct Instance { mat4 prevModel; vec4 params; };
layout(set = 1, binding = 1) readonly buffer Instances { Instance uInstances[]; };

// Skinning matrices for every skinned instance in the frame, end to end. This instance's
// begin at params.y and there are params.z of them.
layout(set = 1, binding = 2) readonly buffer Bones { mat4 uBones[]; };

// The one matrix that moves this vertex: its joints' poses, weighted. A vertex with no
// weights at all (every static mesh in the scene) gets the identity and is left alone.
mat4 skinMatrix(uvec4 joints, vec4 weights, float boneOffset, float boneCount) {
    if (boneCount < 0.5) return mat4(1.0);

    float total = weights.x + weights.y + weights.z + weights.w;
    if (total < 0.0001) return mat4(1.0);

    int base = int(boneOffset);
    mat4 m = uBones[base + int(joints.x)] * weights.x;
    m     += uBones[base + int(joints.y)] * weights.y;
    m     += uBones[base + int(joints.z)] * weights.z;
    m     += uBones[base + int(joints.w)] * weights.w;
    return m;
}


void main() {
    Instance self = uInstances[gl_InstanceIndex];

    // Skin FIRST, in the mesh's own space, then apply the model matrix. The bones were built
    // in model space, so posing after the model transform would apply it twice.
    mat4 skin = skinMatrix(inJoints, inWeights, self.params.y, self.params.z);
    vec3 skinnedPos    = (skin * vec4(inPos, 1.0)).xyz;
    mat3 skinLinear    = mat3(skin);
    vec3 skinnedNormal = skinLinear * inNormal;
    vec3 skinnedTan    = skinLinear * inTangent.xyz;

    vec4 world = vec4(modelPoint(skinnedPos), 1.0);
    vWorldPos  = world.xyz;

    // Normal matrix = transpose(inverse(model3x3)); correct under non-uniform scale.
    mat3 nrm   = transpose(inverse(modelLinear()));
    vNormal    = normalize(nrm * skinnedNormal);
    vTangent   = normalize(nrm * skinnedTan);
    vBitangent = cross(vNormal, vTangent) * inTangent.w;

    vUV       = inUV * uPush.params.y;
    vColor    = inColor;
    vUV1      = inUV1;
    vLightmap = self.params.x;
    vLightPos = uFrame.lightViewProj * world;

    mat3 TBN = mat3(vTangent, vBitangent, vNormal);
    vViewTS  = transpose(TBN) * (uFrame.cameraPos.xyz - vWorldPos);

    // NOTE: skinned by THIS frame's pose, not last frame's — the previous pose is not
    // kept. So a limb swinging in place reports no velocity; the body's motion still does.
    vec4 prevWorld = self.prevModel * vec4(skinnedPos, 1.0);
    vPrevClip = uFrame.prevViewProj * prevWorld;
    vCurClip  = uFrame.viewProj * world;

    gl_Position = vCurClip;
}
