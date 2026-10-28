#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_stats;
layout(set = 1, binding = 1) uniform texture2D t_balance;

layout(location = 0) out vec4 o_color;

const vec2 StatsSize = vec2(16.0, 9.0);

void main() {
    vec3 total = vec3(0.0);
    for (int j = 0; j < 9; j++) {
        for (int i = 0; i < 16; i++) total += pointLod(t_stats, (vec2(float(i), float(j)) + 0.5) / StatsSize).xyz;
    }
    float scale = saturate(cb.alInterior.z * total.y / max(total.z, 1.0e-6));
    float last = pointLod(t_balance, vec2(0.5, 0.5)).x;
    scale = cb.alInterior.y > 0.5 ? mix(scale, last, cb.alInterior.w) : scale;
    o_color = vec4(scale, total.yzx / (StatsSize.x * StatsSize.y * 4.0));
}
