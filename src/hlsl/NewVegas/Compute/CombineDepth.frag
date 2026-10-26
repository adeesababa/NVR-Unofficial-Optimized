// CombineDepth as a Vulkan pass (needs NVR DXVK): Effects/CombineDepth.fx.hlsl CombineDepth, line for line. effects/CombineDepth.cpp ExtRender.
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_worldDepth;      // TESR_DepthBufferWorld (POINT, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_viewModelDepth;  // TESR_DepthBufferViewModel (POINT, CLAMP)

layout(push_constant) uniform Push {
    vec4 depthConstants;   // 0: TESR_DepthConstants (x view model near Z)
    vec4 cameraData;       // 16: TESR_CameraData (x near Z, y far Z)
    uint samplerIndex;     // 32: POINT, CLAMP
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_depth;

// Convert depth to view space Z.
float toVS(float depth, float nearZ, float farZ) {
    return nearZ * farZ / (nearZ + depth * (farZ - nearZ));
}

// Convert view space Z to depth.
float toPS(float viewZ, float nearZ, float farZ) {
    return nearZ * (farZ - viewZ) / (viewZ * (farZ - nearZ));
}

void main() {
    float nearZ = pc.cameraData.x;
    float farZ = pc.cameraData.y;
    float viewModelNearZ = pc.depthConstants.x;

    float worldDepth = texture(sampler2D(t_worldDepth, NVR_SAMPLER(pc.samplerIndex)), uv).x;
    float viewModelDepth = texture(sampler2D(t_viewModelDepth, NVR_SAMPLER(pc.samplerIndex)), uv).x;

    float worldViewZ = toVS(worldDepth, nearZ, farZ);
    float viewModelViewZ = toVS(viewModelDepth, viewModelNearZ, farZ);

    float combinedViewZ = worldViewZ;
    if (viewModelDepth > 0.0)
        combinedViewZ = viewModelViewZ;

    o_depth = vec4(combinedViewZ / farZ, toPS(combinedViewZ, nearZ, farZ), 1.0, 1.0);
}
