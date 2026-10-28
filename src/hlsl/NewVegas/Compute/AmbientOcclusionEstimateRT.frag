// UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT]: the Dedicated
// techniques' Estimate with ray queries against the scene (RtCommon.glsl) instead of the screen samples (PackedEstimate / GTAOEstimate).
// Into NVR_AOBuffer1 (or Low1): AmbientOcclusionTemporalRT.frag then averages it with last frame's into NVR_AOBuffer0, where the blurs and
// the combine read Estimate's result as always. Output as Estimate's: x the AO after StrengthMultiplier, the fog and the far fade (GTAOValue's
// tail), y the depth. uv is PackedVS's: the AO target's texel centre. effects/AmbientOcclusion.cpp ExtDedicated.
#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_ray_query : require
#define NVR_RT 1
#include "AmbientOcclusion.glsl"
#include "RtCommon.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;      // TESR_DepthBuffer (LINEAR, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_normals;    // TESR_NormalsBuffer (NONE = POINT, CLAMP)
layout(set = 1, binding = 2) uniform texture2D t_blueNoise;  // TESR_BlueNoiseSampler (NONE = POINT, WRAP)

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

float readDepth(vec2 coord) { return LINEAR_LOD(t_depth, coord).x * farZ; }

vec3 reconstructPosition(vec2 uv) {
    float z = projectedDepthFromLinear(LINEAR_LOD(t_depth, uv).x);
    vec4 viewSpace = cb.invProjectionTransform * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, z, 1.0);
    return viewSpace.xyz / viewSpace.w;
}

float fogCoeff(float depth) { return saturate(invlerp(cb.fogData.x, cb.fogData.y, depth)); }

void main() {
    vec3 P = reconstructPosition(uv);
    float depth = min(readDepth(uv), endFade);
    if (P.z > endFade) { o_color = vec4(1.0, depth, 0.0, 1.0); return; }
    vec3 N = POINT_LOD(t_normals, uv).xyz * 2.0 - 1.0;
    N = dot(N, -P) < 0.0 ? -N : N;
    vec3 p = (cb.invViewTransform * vec4(P, 0.0)).xyz;               // camera-relative world (as the sky passes)
    vec3 n = normalize((cb.invViewTransform * vec4(N, 0.0)).xyz);
    vec2 noise = POINT_WRAP_LOD(t_blueNoise, uv / (256.0 * cb.aoLayout.zw)).xy;
    noise = fract(noise + (cb.rtTemporal.x > 0.0 ? cb.rtTemporal.z : 0.0) * vec2(0.618034, 0.754878));
    float inside, meanHit;
    float visibility = RtOcclusion(p, n, P.z, noise, inside, meanHit);
    if (RT_DEBUG > 0.5 && RT_DEBUG < 1.5) { o_color = vec4(visibility, depth, 0.0, 1.0); return; }
    if (RT_DEBUG > 2.5) { o_color = vec4(inside > 0.0 ? 0.0 : 0.25 + 0.75 * meanHit, depth, 0.0, 1.0); return; }
    // GTAOValue's tail: the same strength, fog and fade, so the settings mean what they mean for the screen AO.
    float darkness = saturate(1.0 - (1.0 - visibility) * AOstrength);
    float fogColor = luma(cb.fogColor.rgb);
    darkness = min(max(mix(darkness, fogColor, fogCoeff(P.z)), darkness), 1.0);
    o_color = vec4(mix(darkness, 1.0, saturate(invlerp(startFade, endFade, P.z))), depth, 0.0, 1.0);
}
