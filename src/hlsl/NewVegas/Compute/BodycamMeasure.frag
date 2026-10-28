#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_exposure;
layout(set = 1, binding = 2) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 adaptC;
    uint linearIndex;
    uint pointIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    float sum = 0.0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            vec3 c = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), (vec2(float(x), float(y)) + 0.5) / 8.0, 0.0).rgb;
            sum += log(max(luma(pow(clamp(c, 0.0, 1.0), vec3(2.2))), 1e-3));
        }
    }
    float now = sum / 64.0;
    vec4 stored = textureLod(sampler2D(t_exposure, NVR_SAMPLER(pc.pointIndex)), vec2(0.5), 0.0);
    vec2 last = log(max(stored.rg, vec2(1e-4)));
    if (pc.adaptC.w < 0.5) last = vec2(now);
    float fast = mix(last.x, now, 1.0 - exp(-pc.adaptC.y / 0.08));
    float slow = mix(last.y, fast, 1.0 - exp(-pc.adaptC.y / max(pc.adaptC.z, 0.05)));
    float depth = 0.0;
    for (int k = 0; k < 5; k++) {
        vec2 at = vec2(0.5) + vec2(k == 1 ? -0.06 : k == 2 ? 0.06 : 0.0, k == 3 ? -0.06 : k == 4 ? 0.06 : 0.0);
        depth += log(max(textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.pointIndex)), at, 0.0).x, 1e-4));
    }
    depth /= 5.0;
    float focus = pc.adaptC.w < 0.5 ? depth : mix(stored.a, depth, 1.0 - exp(-pc.adaptC.y / 0.3));
    o_color = vec4(exp(fast), exp(slow), depth, focus);
}
