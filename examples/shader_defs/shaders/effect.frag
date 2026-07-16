#version 450

// One source, several pipelines. The WAVE and TINT blocks are switched on at BUILD
// time by -D flags passed to glslc, producing a distinct SPIR-V per combination.
// The runtime picks among them and the PipelineCache specializes a pipeline for
// each — that is what "shader defs" means here.

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 col = vec3(vUV, 0.5);

#ifdef WAVE
    // Ripple the colour across the screen.
    col += 0.25 * sin(vUV.x * 30.0 + vUV.y * 12.0);
#endif

#ifdef TINT
    // Push it toward a warm tint.
    col *= vec3(1.0, 0.4, 0.25);
#endif

    outColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
