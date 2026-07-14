#version 450

// A 4x4 box blur over the raw AO. The per-pixel kernel rotation in ssao.frag trades
// banding for noise; this is the half of that bargain that pays it off. The kernel is
// exactly the size of the rotation's period, so the noise averages out completely.

layout(set = 0, binding = 0) uniform texture2D uAO;
layout(set = 0, binding = 1) uniform sampler   uSampler;

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    vec4 params;   // xy = texel size
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    float sum = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            sum += texture(sampler2D(uAO, uSampler),
                           vUV + vec2(x, y) * pc.params.xy).r;

    outColor = vec4(vec3(sum / 16.0), 1.0);
}
