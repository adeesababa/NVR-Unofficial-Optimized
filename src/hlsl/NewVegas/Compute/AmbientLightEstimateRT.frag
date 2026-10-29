// UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT]: AmbientLight's
// Estimate from the RT helper's traced visibility (RtAOTrace.comp, read back into NVR_RTAORaw) instead of the screen-space GTAO search:
// what is off screen, behind the first surface or thinner than the depth tells still occludes. Into NVR_AmbientLight0 (half resolution),
// in Estimate's place, with Estimate's output (rgb 0: no screen bounce; a the visibility after OcclusionStrength and the near/far fade),
// so the passes after it (blur X / Y, Accumulate's reprojected history, Apply) take it as they take Estimate's: they are the denoiser.
// pc.variant = [Shaders.AmbientOcclusion.RT] DebugView (1 the raw visibility, 3 the inside / hit-distance view). effects/AmbientLight.cpp.
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0) uniform texture2D t_prep;       // NVR_ALPrep (POINT)
layout(set = 1, binding = 1) uniform texture2D t_rt;         // NVR_RTAORaw (the trace's output at the same half resolution; LINEAR)

layout(location = 0) out vec4 o_color;

vec4 EstimateRT(vec2 uv) {
    vec4 center = pointLod(t_prep, uv);
    vec3 origin = PositionFromViewZ(uv, center.x);
    float fade = smoothstep(NearFade, NearFade * 2.0, origin.z) * (1.0 - smoothstep(DrawDistance * 0.7, DrawDistance, origin.z));
    if (fade <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    vec4 rt = linearLod(t_rt, uv);
    if (pc.variant == 1u || pc.variant >= 4u) return vec4(0.0, 0.0, 0.0, rt.x);    // the raw RT visibility (AmbientLight's view 1 shows it); 4, 5: the input checks
    if (pc.variant == 3u) return vec4(0.0, 0.0, 0.0, rt.w);    // black: rays began inside something; grey: mean hit / Distance
    float visibility = pow(saturate(rt.x), 1.5 * OcclusionStrength);   // Estimate's mapping: OcclusionStrength means the same on both
    return vec4(0.0, 0.0, 0.0, saturate(mix(1.0, visibility, fade)));
}

void main() { o_color = EstimateRT(frameUV()); }
