#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_prep;
layout(set = 1, binding = 1) uniform texture2D t_rt;

layout(location = 0) out vec4 o_color;

vec4 EstimateRT(vec2 uv) {
    vec4 center = pointLod(t_prep, uv);
    vec3 origin = PositionFromViewZ(uv, center.x);
    float fade = smoothstep(NearFade, NearFade * 2.0, origin.z) * (1.0 - smoothstep(DrawDistance * 0.7, DrawDistance, origin.z));
    if (fade <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec4 rt = linearLod(t_rt, uv);
    if (pc.variant == 1u || pc.variant >= 4u) return vec4(0.0, 0.0, 0.0, rt.x);
    if (pc.variant == 3u) return vec4(0.0, 0.0, 0.0, rt.w);
    float visibility = pow(saturate(rt.x), 1.5 * OcclusionStrength);
    return vec4(0.0, 0.0, 0.0, saturate(mix(1.0, visibility, fade)));
}

void main() { o_color = EstimateRT(frameUV()); }
