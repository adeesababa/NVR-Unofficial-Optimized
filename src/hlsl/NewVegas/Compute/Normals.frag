// Normals as a Vulkan pass (needs NVR DXVK): Effects/Normals.fx.hlsl technique 0 (ComputeNormals), line for line. Normals.glsl has the maths.
// effects/Normals.cpp ExtRender.
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"
#include "Normals.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;     // TESR_DepthBuffer, CombineDepth's output (POINT, CLAMP)

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;  // 0: TESR_ReciprocalResolution
    vec4 cameraData;            // 16: TESR_CameraData (x near Z, y far Z)
    mat4 invProjection;         // 32: TESR_InvProjectionTransform
    uint samplerIndex;          // 96: POINT, CLAMP
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_normal;

vec2 depthPair(vec2 p) {
    return depthPairFromLinear(texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), p).x, pc.cameraData.x, pc.cameraData.y);
}

void main() {
    vec2 texel = pc.reciprocalResolution.xy;
    float farZ = pc.cameraData.y;

    vec3 viewNormal = viewNormalFromTaps(uv, texel, pc.invProjection, farZ,
        depthPair(uv),
        depthPair(uv + vec2(1.0, 0.0) * texel), depthPair(uv + vec2(2.0, 0.0) * texel),
        depthPair(uv + vec2(-1.0, 0.0) * texel), depthPair(uv + vec2(-2.0, 0.0) * texel),
        depthPair(uv + vec2(0.0, -1.0) * texel), depthPair(uv + vec2(0.0, -2.0) * texel),
        depthPair(uv + vec2(0.0, 1.0) * texel), depthPair(uv + vec2(0.0, 2.0) * texel));

    o_normal = vec4(viewNormal * 0.5 + 0.5, 1.0);
}
