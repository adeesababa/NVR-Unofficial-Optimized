#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientOcclusion.glsl"

layout(set = 1, binding = 0) uniform texture2D t_ao;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_normals;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec3 GetNormal(vec2 uv) {
    return POINT_LOD(t_normals, uv).xyz * 2.0 - 1.0;
}

void main() {
    vec2 center = LINEAR_LOD(t_ao, DedicatedUV(uv)).rg;
    if (center.y >= endFade) { o_color = vec4(1.0, center.y, 0.0, 1.0); return; }
    float rawDepth = projectedDepthFromLinear(LINEAR_LOD(t_depth, uv).x);
    if (invertedDepth) rawDepth = 1.0 - rawDepth;
    float depthTolerance = blurDrop * (0.5 + 2.0 * compress(GetNormal(uv).z));
    float sum = center.x * 0.114725602;
    float weights = 0.114725602;
    for (int i = 0; i < cKernelSize; ++i) {
        vec2 tapUV = uv + BlurOffsets(i) * pc.axis * blurRadius / max(rawDepth, 1.0e-6);
        vec2 tap = LINEAR_LOD(t_ao, DedicatedUV(tapUV)).rg;
        float weight = BlurWeights[i] * (abs(center.y - tap.y) <= depthTolerance ? 1.0 : 0.0);
        sum += tap.x * weight;
        weights += weight;
    }
    o_color = vec4(sum / weights, center.y, 0.0, 1.0);
}
