#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 heatHazeData;
    vec4 heatHazeNoise;
    vec4 misc;
    vec4 viewCol0;
    vec4 viewCol1;
    vec4 viewCol2;
    uint linearIndex;
    uint pointIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const float PI = 3.14159265;

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.pointIndex)), coord).x * pc.misc.z;
}

vec3 toWorld(vec2 tex) {
    vec3 v = pc.viewCol2.xyz;
    v += (1.0 / pc.misc.x * (2.0 * tex.x - 1.0)) * pc.viewCol0.xyz;
    v += (-1.0 / pc.misc.y * (2.0 * tex.y - 1.0)) * pc.viewCol1.xyz;
    return v;
}

float hfmod(float x, float y) { return x - y * trunc(x / y); }

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(vec2 p, float period, float seed) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float x0 = hfmod(i.x, period);
    float x1 = hfmod(i.x + 1.0, period);
    float a = hash(vec2(x0, i.y) + seed);
    float b = hash(vec2(x1, i.y) + seed);
    float c = hash(vec2(x0, i.y + 1.0) + seed);
    float d = hash(vec2(x1, i.y + 1.0) + seed);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float hazeNoise(vec2 p, float period, float seed) {
    float n = valueNoise(p, period, seed) * 0.65 + valueNoise(p * 2.0, period * 2.0, seed + 17.0) * 0.35;
    return n * 2.0 - 1.0;
}

void main() {
    vec3 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), uv).rgb;

    float startDistance = pc.heatHazeData.y;
    float fullDistance = pc.heatHazeData.z;

    float depth = readDepth(uv);
    float distanceFactor = smoothstep(startDistance, fullDistance, depth);
    if (distanceFactor <= 0.0) {
        o_color = vec4(color, 1.0);
        return;
    }

    vec3 dir = normalize(toWorld(uv));
    float horizon = 1.0 - smoothstep(0.0, pc.heatHazeData.w, abs(dir.z));
    float amount = pc.heatHazeData.x * distanceFactor * horizon;
    if (amount <= 1e-6) {
        o_color = vec4(color, 1.0);
        return;
    }

    float period = max(1.0, floor(pc.heatHazeNoise.x * 2.0 * PI));
    float around = (atan(dir.y, dir.x) / (2.0 * PI) + 0.5) * period;
    float up = dir.z * pc.heatHazeNoise.x * 3.0 - pc.heatHazeNoise.z * pc.heatHazeNoise.y;
    vec2 p = vec2(around, up);

    vec2 offset = vec2(hazeNoise(p, period, 3.0) * 0.5, hazeNoise(p, period, 41.0)) * amount;
    offset.x /= pc.misc.w;

    vec2 sampleUV = uv + offset;
    if (readDepth(sampleUV) < startDistance) {
        o_color = vec4(color, 1.0);
        return;
    }

    o_color = vec4(texture(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), sampleUV).rgb, 1.0);
}
