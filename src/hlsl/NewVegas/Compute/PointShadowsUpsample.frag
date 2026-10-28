#version 450
#extension GL_GOOGLE_include_directive : enable
#include "PointShadowsCommon.glsl"

layout(set = 1, binding = 0) uniform texture2D t_mask;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    uint linearSampler;
    uint pointSampler;
    uint mode;
    uint pad;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define LINEAR_CLAMP NVR_SAMPLER(pc.linearSampler)
#define POINT_WRAP NVR_SAMPLER(pc.pointSampler)

void main() {
    vec2 lowRR = 2.0 * cb.reciprocalResolution.xy;
    vec2 position = uv / lowRR - 0.5;
    vec2 base = floor(position);
    vec2 fraction = fract(position);
    float depth = readDepthAt(t_depth, LINEAR_CLAMP, uv);
    float tolerance = 1.0 + 0.01 * depth;
    float sum = 0.0, weights = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec2 tap = textureLod(sampler2D(t_mask, POINT_WRAP), (base + vec2(float(x), float(y)) + 0.5) * lowRR, 0.0).rg;
            float weight = (x != 0 ? fraction.x : 1.0 - fraction.x) * (y != 0 ? fraction.y : 1.0 - fraction.y) + 1e-4;
            float gap = abs(tap.y - depth) / tolerance;
            weight /= 1.0 + gap * gap * 16.0;
            sum += tap.x * weight;
            weights += weight;
        }
    }
    float mask = sum / max(weights, 1e-6);
    o_color = vec4(mask, mask, mask, 1.0);
}
