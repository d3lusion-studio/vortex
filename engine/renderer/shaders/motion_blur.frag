#version 450

// Per-pixel motion blur, driven by the velocity the G-buffer recorded.
//
// The G-buffer's fourth target holds, for every pixel, how far it moved across the screen
// since the last frame — computed per vertex from that instance's own previous model
// matrix. So this blurs a spinning object under a still camera, which the earlier
// depth-reprojection version could not: reprojection only knows where the *camera* was.
//
//   set 0 = scene colour (binding 0) + velocity (binding 1), sharing binding 5's sampler.

layout(set = 0, binding = 0) uniform texture2D uScene;
layout(set = 0, binding = 1) uniform texture2D uVelocity;
layout(set = 0, binding = 5) uniform texture2D uUnused;
layout(set = 0, binding = 6) uniform sampler   uSampler;

#ifdef VORTEX_NO_PUSH_CONSTANTS
layout(set = 3, binding = 0, std140) uniform Push {
#else
layout(push_constant) uniform Push {
#endif
    vec4 params;   // x = strength, y = sample count, z = max velocity (UV)
} pc;

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 velocity = texture(sampler2D(uVelocity, uSampler), vUV).xy * pc.params.x;

    // Clamp the streak: a fast spin can throw a pixel most of the way across the screen,
    // and smearing it that far reads as a glitch rather than as motion.
    float speed = length(velocity);
    if (speed > pc.params.z) velocity *= pc.params.z / speed;

    if (speed < 0.0001) {
        outColor = texture(sampler2D(uScene, uSampler), vUV);
        return;
    }

    int  samples = int(pc.params.y);
    vec3 sum     = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        // Walk along the motion, centred on the pixel.
        float t  = float(i) / float(samples - 1) - 0.5;
        vec2  uv = clamp(vUV - velocity * t, vec2(0.0), vec2(1.0));
        sum += texture(sampler2D(uScene, uSampler), uv).rgb;
    }

    outColor = vec4(sum / float(samples), 1.0);
}
