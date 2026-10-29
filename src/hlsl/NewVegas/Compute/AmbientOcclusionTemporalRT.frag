#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientOcclusion.glsl"

layout(set = 1, binding = 0) uniform texture2D t_current;
layout(set = 1, binding = 1) uniform texture2D t_history;
layout(set = 1, binding = 2) uniform texture2D t_depth;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_history;

vec3 reconstructPosition(vec2 uv) {
    float z = projectedDepthFromLinear(LINEAR_LOD(t_depth, uv).x);
    vec4 viewSpace = cb.invProjectionTransform * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, z, 1.0);
    return viewSpace.xyz / viewSpace.w;
}

void main() {
    vec4 current = POINT_LOD(t_current, uv);
    o_color = current;
    o_history = current;
    if (!(cb.rtTemporal.x > 0.0) || current.y >= endFade) return;
    vec3 origin = reconstructPosition(uv);
    vec4 clip = cb.rtReproject * vec4(origin, 1.0);
    float w = max(clip.w, 0.001);
    vec3 last = vec3(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5, clip.w);
    vec2 texel = cb.aoLayout.zw;
    vec2 position = last.xy / texel - 0.5;
    vec2 base = floor(position);
    vec2 f = position - base;
    vec2 t00 = POINT_LOD(t_history, (base + 0.5) * texel).rg;
    vec2 t10 = POINT_LOD(t_history, (base + vec2(1.5, 0.5)) * texel).rg;
    vec2 t01 = POINT_LOD(t_history, (base + vec2(0.5, 1.5)) * texel).rg;
    vec2 t11 = POINT_LOD(t_history, (base + 1.5) * texel).rg;
    vec4 weight = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
    weight *= vec4(lessThan(abs(vec4(t00.g, t10.g, t01.g, t11.g) - last.z), vec4(max(last.z * 0.05, 4.0))));
    float found = dot(weight, vec4(1.0));
    float history = dot(vec4(t00.r, t10.r, t01.r, t11.r), weight) / max(found, 1.0e-4);
    float low = current.x, high = current.x;
    for (int i = 0; i < 4; i++) {
        vec2 offset = (i < 2 ? vec2(float(i * 2 - 1), 0.0) : vec2(0.0, float(i * 2 - 5))) * texel;
        float n = POINT_LOD(t_current, uv + offset).x;
        low = min(low, n);
        high = max(high, n);
    }
    float margin = (high - low) * 0.25;
    history = clamp(history, low - margin, high + margin);
    float inside = (all(greaterThan(last.xy, vec2(0.0))) && all(lessThan(last.xy, vec2(1.0))) && last.z > 1.0) ? 1.0 : 0.0;
    float moved = length((last.xy - uv) / texel);
    float keep = cb.rtTemporal.x / (1.0 + moved / cb.rtTemporal.y) * saturate(found * 2.0) * inside;
    o_color = vec4(mix(current.x, history, keep), current.y, 0.0, 1.0);
    o_history = o_color;
}
