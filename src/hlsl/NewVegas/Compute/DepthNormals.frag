#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"
#include "Normals.glsl"

layout(set = 1, binding = 0) uniform texture2D t_worldDepth;
layout(set = 1, binding = 1) uniform texture2D t_viewModelDepth;

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;
    vec4 cameraData;
    vec4 depthConstants;
    mat4 invProjection;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_depth;
layout(location = 1) out vec4 o_normal;

float toVS(float depth, float nearZ, float farZ) {
    return nearZ * farZ / (nearZ + depth * (farZ - nearZ));
}

float toPS(float viewZ, float nearZ, float farZ) {
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

vec2 combinedDepthAt(vec2 p) {
    float nearZ = pc.cameraData.x;
    float farZ = pc.cameraData.y;
    float worldDepth = texture(sampler2D(t_worldDepth, NVR_SAMPLER(pc.samplerIndex)), p).x;
    float viewModelDepth = texture(sampler2D(t_viewModelDepth, NVR_SAMPLER(pc.samplerIndex)), p).x;
    float combinedViewZ = toVS(worldDepth, nearZ, farZ);
    if (viewModelDepth > 0.0)
        combinedViewZ = toVS(viewModelDepth, pc.depthConstants.x, farZ);
    return vec2(combinedViewZ / farZ, toPS(combinedViewZ, nearZ, farZ));
}

void main() {
    vec2 texel = pc.reciprocalResolution.xy;
    float farZ = pc.cameraData.y;

    vec2 centerDepth = combinedDepthAt(uv);
    vec3 viewNormal = viewNormalFromTaps(uv, texel, pc.invProjection, farZ,
        centerDepth,
        combinedDepthAt(uv + vec2(1.0, 0.0) * texel), combinedDepthAt(uv + vec2(2.0, 0.0) * texel),
        combinedDepthAt(uv + vec2(-1.0, 0.0) * texel), combinedDepthAt(uv + vec2(-2.0, 0.0) * texel),
        combinedDepthAt(uv + vec2(0.0, -1.0) * texel), combinedDepthAt(uv + vec2(0.0, -2.0) * texel),
        combinedDepthAt(uv + vec2(0.0, 1.0) * texel), combinedDepthAt(uv + vec2(0.0, 2.0) * texel));

    o_depth = vec4(centerDepth.x, centerDepth.y, 1.0, 1.0);
    o_normal = vec4(viewNormal * 0.5 + 0.5, 1.0);
}
