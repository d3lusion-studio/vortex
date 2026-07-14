#version 450

// Depth-only shadow pass — but not material-blind. A cutout material (a leaf, a fence, a
// hair card) is a solid quad as far as geometry is concerned, and a shadow pass that looks
// only at geometry casts the shadow of the quad rather than of the leaf. So the UV comes
// through, and the fragment stage tests the alpha exactly as the lit pass does.
//
// Sets: 0 = the material's maps (for that alpha test), 1 = frame + instances + bones. The
// ordering is not a choice: the RHI assembles set layouts in a fixed order (material first,
// then uniform), so a pipeline that asks for both always gets them this way round.

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 lightMVP;   // lightViewProj * model
    vec4 params;     // x = alpha cutoff, y = UV scale
} uPush;

layout(location = 0) in vec3  inPos;
layout(location = 2) in vec2  inUV;
layout(location = 6) in uvec4 inJoints;
layout(location = 7) in vec4  inWeights;

struct Light { vec4 position; vec4 direction; vec4 color; vec4 params; };
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

struct Instance { mat4 prevModel; vec4 params; };
layout(set = 1, binding = 1) readonly buffer Instances { Instance uInstances[]; };
layout(set = 1, binding = 2) readonly buffer Bones { mat4 uBones[]; };

layout(location = 0) out vec2 vUV;

void main() {
    Instance self = uInstances[gl_InstanceIndex];

    // Skin here too. Skip it and a character walks while its shadow stands in the bind pose,
    // which is far more obviously wrong than a slightly wrong shadow would be.
    vec3  pos       = inPos;
    float boneCount = self.params.z;
    float total     = inWeights.x + inWeights.y + inWeights.z + inWeights.w;
    if (boneCount > 0.5 && total > 0.0001) {
        int  base = int(self.params.y);
        mat4 skin = uBones[base + int(inJoints.x)] * inWeights.x
                  + uBones[base + int(inJoints.y)] * inWeights.y
                  + uBones[base + int(inJoints.z)] * inWeights.z
                  + uBones[base + int(inJoints.w)] * inWeights.w;
        pos = (skin * vec4(inPos, 1.0)).xyz;
    }

    vUV = inUV * uPush.params.y;
    gl_Position = uPush.lightMVP * vec4(pos, 1.0);
}
