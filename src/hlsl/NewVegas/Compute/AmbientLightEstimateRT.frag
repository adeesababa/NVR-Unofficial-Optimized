// UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT]: AmbientLight's
// Estimate with ray queries against the scene (RtCommon.glsl) instead of the screen-space GTAO search: what is off screen, behind the
// first surface or thinner than the depth tells still occludes. Into NVR_AmbientLight0 (half resolution), in Estimate's place, with
// Estimate's output (rgb 0: no screen bounce; a the visibility after OcclusionStrength and the near/far fade), so the passes after it
// (blur X / Y, Accumulate's reprojected history, Apply) take it as they take Estimate's: they are the denoiser. The ray noise changes
// each frame only while Accumulate keeps a history (NVR_ALTemporal.x > 0), else it stands still. effects/AmbientLight.cpp ExtDraw.
// Compiled with -DNVR_RT (the 64-byte push block; Compute/variants.txt is not needed: this file is only built this way).
#version 460
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_ray_query : require
#define NVR_RT 1
#include "AmbientLight.glsl"
#include "RtCommon.glsl"

layout(set = 1, binding = 0) uniform texture2D t_prep;       // NVR_ALPrep (POINT)
layout(set = 1, binding = 1) uniform texture2D t_blueNoise;  // TESR_BlueNoiseSampler (POINT, WRAP)

layout(location = 0) out vec4 o_color;

vec4 EstimateRT(vec2 uv) {
    vec4 center = pointLod(t_prep, uv);
    vec3 origin = PositionFromViewZ(uv, center.x);
    float fade = smoothstep(NearFade, NearFade * 2.0, origin.z) * (1.0 - smoothstep(DrawDistance * 0.7, DrawDistance, origin.z));
    if (fade <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 normal = OctDecode(Unpack2(center.y));
    // View -> camera-relative world: HLSL mul(TESR_ViewTransform, v) with w 0, the rotation only (as GetWorldNormalLod, toWorld).
    vec3 p = (vec4(origin, 0.0) * cb.viewTransform).xyz;
    vec3 n = normalize((vec4(normal, 0.0) * cb.viewTransform).xyz);
    vec2 noise = textureLod(sampler2D(t_blueNoise, NVR_SAMPLER(pc.pointWrap)), uv / (256.0 * cb.alLayout.zw), 0.0).xy;
    noise = fract(noise + (cb.alTemporal.x > 0.0 ? cb.alTemporal.z : 0.0) * vec2(0.618034, 0.754878));
    float inside, meanHit;
    float visibility = RtOcclusion(p, n, origin.z, noise, inside, meanHit);
    if (RT_DEBUG > 0.5 && RT_DEBUG < 1.5) return vec4(0.0, 0.0, 0.0, visibility);              // the raw RT visibility (AmbientLight's view 1)
    if (RT_DEBUG > 2.5) return vec4(0.0, 0.0, 0.0, inside > 0.0 ? 0.0 : 0.25 + 0.75 * meanHit); // black: rays began inside; grey: mean hit / Distance
    visibility = pow(saturate(visibility), 1.5 * OcclusionStrength);   // Estimate's mapping: OcclusionStrength means the same on both
    return vec4(0.0, 0.0, 0.0, saturate(mix(1.0, visibility, fade)));
}

void main() { o_color = EstimateRT(frameUV()); }
