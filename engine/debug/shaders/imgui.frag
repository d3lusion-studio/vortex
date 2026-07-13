#version 450

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(set = 0, binding = 0) uniform texture2D uTexture;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(location = 0) out vec4 outColor;

// ImGui packs its vertex colours as sRGB bytes — that is what its style editor and every
// ImVec4 in user code mean. The target is an sRGB view now, so the hardware encodes on
// write; handing it sRGB values would apply the curve twice and wash the whole UI out.
// Decode here and the UI lands on screen the colour ImGui actually asked for.
//
// The font atlas needs no such care: it is white with a coverage alpha, and white is the
// one colour the two encodings agree on.
vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92,
               pow((c + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), c));
}

void main() {
    vec4 tint = vec4(srgbToLinear(vColor.rgb), vColor.a);
    outColor  = tint * texture(sampler2D(uTexture, uSampler), vUV);
}
