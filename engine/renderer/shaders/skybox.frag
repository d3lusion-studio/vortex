#version 450

layout(set = 0, binding = 0) uniform Sky {
    mat4 invViewProj;
    vec4 cameraPos;
    vec4 params;        // x = intensity
} uSky;

layout(set = 1, binding = 0) uniform textureCube uIrradiance;   // unused here, but the
layout(set = 1, binding = 1) uniform textureCube uEnv;          // scene set is shared
layout(set = 1, binding = 2) uniform sampler     uIblSampler;
layout(set = 1, binding = 3) uniform texture2D   uShadowMap;
layout(set = 1, binding = 4) uniform sampler     uShadowSampler;

layout(location = 0) in  vec3 vDir;
layout(location = 0) out vec4 outColor;

void main() {
    // HDR out, like the rest of the scene pass: the post chain tone-maps it.
    vec3 sky = texture(samplerCube(uEnv, uIblSampler), normalize(vDir)).rgb;
    outColor = vec4(sky * uSky.params.x, 1.0);
}
