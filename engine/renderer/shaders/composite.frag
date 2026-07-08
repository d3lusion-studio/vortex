#version 450

// Pass-through used with additive blending to add the blurred bloom back onto
// the HDR scene target.
layout(set = 0, binding = 0) uniform texture2D uTex;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(push_constant) uniform Push {
    vec4 params;   // x: bloom intensity
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(sampler2D(uTex, uSampler), vUV).rgb * pc.params.x;
    outColor = vec4(c, 1.0);
}
