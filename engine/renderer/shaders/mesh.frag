#version 450

layout(set = 0, binding = 0) uniform Frame {
    mat4 viewProj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambient;
    vec4 cameraPos;
} uFrame;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 baseColor;
} uPush;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uFrame.lightDir.xyz);   // surface -> light
    float diff = max(dot(N, L), 0.0);

    // Blinn-Phong specular (only where the surface faces the light).
    vec3 V = normalize(uFrame.cameraPos.xyz - vWorldPos);
    vec3 H = normalize(L + V);
    float spec = (diff > 0.0) ? pow(max(dot(N, H), 0.0), 32.0) : 0.0;

    vec3 light = uFrame.lightColor.rgb * uFrame.lightColor.w;
    vec3 lit   = uFrame.ambient.rgb + light * (diff + 0.25 * spec);
    outColor   = vec4(uPush.baseColor.rgb * lit, uPush.baseColor.a);
}
