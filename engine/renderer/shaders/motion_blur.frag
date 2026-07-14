#version 450

// Per-pixel motion blur by depth reprojection.
//
// Where the pixel was last frame is recoverable without storing any motion vectors:
// unproject it with this frame's camera, project it with last frame's, and the screen
// distance between the two is how far it moved. The scene is then averaged along that
// line. This captures camera motion exactly; an object moving on its own while the
// camera holds still is NOT captured, because its per-object matrix is not part of the
// reprojection — that needs a velocity target in the G-buffer.
//
//   set 0 = scene colour (binding 0) + depth (binding 1), sharing binding 5's sampler.
//   set 3 = push constants.

layout(set = 0, binding = 0) uniform texture2D uScene;
layout(set = 0, binding = 1) uniform texture2D uDepth;
layout(set = 0, binding = 5) uniform sampler   uSampler;

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    mat4 reprojection;   // this frame's clip -> last frame's clip
    vec4 params;         // x = strength, y = sample count, z = max velocity (UV)
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    float depth = texture(sampler2D(uDepth, uSampler), vUV).r;

    vec4 clip     = vec4(vUV * 2.0 - 1.0, depth, 1.0);
    vec4 prevClip = pc.reprojection * clip;
    if (prevClip.w <= 0.0) {
        outColor = texture(sampler2D(uScene, uSampler), vUV);
        return;
    }

    vec2 prevUV   = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    vec2 velocity = (vUV - prevUV) * pc.params.x;

    // Clamp the streak: a pixel near the far plane under a fast turn can reproject
    // halfway across the screen, and smearing it that far reads as a glitch, not blur.
    float speed = length(velocity);
    if (speed > pc.params.z) velocity *= pc.params.z / speed;
    if (speed < 0.0001) {
        outColor = texture(sampler2D(uScene, uSampler), vUV);
        return;
    }

    int  samples = int(pc.params.y);
    vec3 sum     = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        // Walk backwards along the motion, centred on the pixel.
        float t  = float(i) / float(samples - 1) - 0.5;
        vec2  uv = clamp(vUV - velocity * t, vec2(0.0), vec2(1.0));
        sum += texture(sampler2D(uScene, uSampler), uv).rgb;
    }

    outColor = vec4(sum / float(samples), 1.0);
}
