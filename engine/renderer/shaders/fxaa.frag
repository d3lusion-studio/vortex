#version 450

// FXAA 3.11-style edge anti-aliasing on the tone-mapped (sRGB) image.
layout(set = 0, binding = 0) uniform texture2D uImage;
layout(set = 0, binding = 1) uniform sampler   uSampler;

layout(push_constant) uniform Push {
    vec4 params;   // xy: 1/resolution
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec2 texel = pc.params.xy;
    vec3 rgbM  = texture(sampler2D(uImage, uSampler), vUV).rgb;
    float lM   = luma(rgbM);
    float lNW  = luma(texture(sampler2D(uImage, uSampler), vUV + vec2(-1, -1) * texel).rgb);
    float lNE  = luma(texture(sampler2D(uImage, uSampler), vUV + vec2( 1, -1) * texel).rgb);
    float lSW  = luma(texture(sampler2D(uImage, uSampler), vUV + vec2(-1,  1) * texel).rgb);
    float lSE  = luma(texture(sampler2D(uImage, uSampler), vUV + vec2( 1,  1) * texel).rgb);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    // Skip flat areas.
    if (lMax - lMin < lMax * 0.125 + 0.0312) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),
                     ((lNW + lSW) - (lNE + lSE)));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
    float rcpDir = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpDir, vec2(-8.0), vec2(8.0)) * texel;

    vec3 rgbA = 0.5 * (
        texture(sampler2D(uImage, uSampler), vUV + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(sampler2D(uImage, uSampler), vUV + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(sampler2D(uImage, uSampler), vUV + dir * -0.5).rgb +
        texture(sampler2D(uImage, uSampler), vUV + dir *  0.5).rgb);

    float lB = luma(rgbB);
    outColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
