#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"
#include "VolumetricFog.glsl"

layout(location = 0) out vec4 o_mulDepth;
layout(location = 1) out vec4 o_add;

void main() {
    vec2 h = 0.5 * (pc.fogLayout.zw - cb.reciprocalResolution.xy);
    vec2 best = uv - h;
    float bestDepth = readDepthLod(best);
    const vec2 signs[3] = vec2[3](vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    for (int i = 0; i < 3; i++) {
        vec2 candidate = uv + signs[i] * h;
        float candidateDepth = readDepthLod(candidate);
        if (candidateDepth < bestDepth) { best = candidate; bestDepth = candidateDepth; }
    }
    vec3 fogMul, fogAdd;
    float nightAmount;
    FogTerms(best, fogMul, fogAdd, nightAmount, FOG_LEGACY_ON, FOG_INTERIOR_ON, FOG_CLASSIC_ON);
    o_mulDepth = vec4(fogMul, saturate(bestDepth / farZ));
    o_add = vec4(fogAdd, 1.0);
}
