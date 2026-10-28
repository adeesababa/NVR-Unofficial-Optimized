#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_mask;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 cameraData;
    vec4 reciprocalResolution;
    vec2 offsetMask;
    float maxDistance;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const int cKernelSize = 12;

const float blurWeights[cKernelSize] = float[cKernelSize](
    0.057424882,
    0.058107773,
    0.061460144,
    0.071020611,
    0.088092873,
    0.106530916,
    0.106530916,
    0.088092873,
    0.071020611,
    0.061460144,
    0.058107773,
    0.057424882
);

vec2 blurOffset(int i) {
    float k = i < 6 ? float(i - 6) : float(i - 5);
    return vec2(k * pc.reciprocalResolution.x, k * pc.reciprocalResolution.y);
}

float readDepthLod(vec2 coord) {
    return textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0).x * pc.cameraData.y;
}

vec4 sampleMask(vec2 coord) {
    return textureLod(sampler2D(t_mask, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0);
}

void main() {
    vec4 center = sampleMask(uv);
    float depth1 = readDepthLod(uv);
    if (depth1 > pc.maxDistance) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec4 taps[cKernelSize];
    vec4 spread = vec4(0.0);
    for (int t = 0; t < cKernelSize; t++) {
        taps[t] = sampleMask(uv + (blurOffset(t) * pc.offsetMask) * 1.0);
        spread = max(spread, abs(taps[t] - center));
    }
    if (max(spread.r, spread.g) < 0.0001) {
        o_color = vec4(center.rgb, 1.0);
        return;
    }

    float weightSum = 0.114725602;
    vec4 color1 = center * weightSum;
    float depthDrop = 3500.0 * (depth1 / pc.cameraData.y);

    for (int i = 0; i < cKernelSize; i++) {
        float depth2 = readDepthLod(uv + (blurOffset(i) * pc.offsetMask) * 1.0);
        float diff = abs(depth1 - depth2);

        float useForBlur = diff <= depthDrop ? 1.0 : 0.0;
        color1 += blurWeights[i] * taps[i] * useForBlur;
        weightSum += blurWeights[i] * useForBlur;
    }
    color1 /= weightSum;
    o_color = vec4(color1.rgb, 1.0);
}
