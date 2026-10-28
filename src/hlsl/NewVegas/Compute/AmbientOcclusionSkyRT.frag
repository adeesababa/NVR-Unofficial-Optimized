// UNOFFICIAL 2026-10-28 (NVR DXVK fork, hybrid RT item 2; docs/next/FORK-RT-AO.md) [Shaders.AmbientOcclusion.RT] Sky: Skylighting v2's
// sky factor (SkyVisibilityV2, AmbientOcclusionSky.frag) from the RT helper's traced sky visibility (RtAOTrace.comp, NVR_RTAORaw .y,
// 'covered' .z) instead of the top-down ortho map: overhangs, bridges, porches and the inside of buildings block the sky by their real
// shape. Same target, same output (x the factor, y the depth, z the visibility), same settings (Strength, OpenThreshold, ShareApply,
// AmbientScale, the sun mask; LightCap and Skylighting's DebugView 1-4 downstream), so AmbientLight's apply and the fog's composite take
// it unchanged. effects/AmbientOcclusion.cpp RenderSkyVisibility. FrameVS's uv in the sky target's viewport, as the v2 pass.
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientOcclusion.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;      // TESR_DepthBuffer (LINEAR, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_normals;    // TESR_NormalsBuffer (NONE = POINT, CLAMP)
layout(set = 1, binding = 2) uniform texture2D t_sunMask;    // NVR_SunMask (POINT, CLAMP); a stand-in unless cb.sky4.z > 0.5
layout(set = 1, binding = 3) uniform texture2D t_skyPrep;    // NVR_SkyPrep (POINT, CLAMP); a stand-in unless cb.skyLayout.z > 0.5
layout(set = 1, binding = 4) uniform texture2D t_rt;         // NVR_RTAORaw (half resolution; LINEAR)

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec2 SkyUnpack2(float p) { float high = floor(p / 4096.0); return vec2(high, p - high * 4096.0) / 4095.0; }
vec3 SkyOctDecode(vec2 e) {
    e = e * 2.0 - 1.0;
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += mix(vec2(t), vec2(-t), greaterThanEqual(n.xy, vec2(0.0)));
    return normalize(n);
}

void main() {
    // FrameVS's uv in this viewport (as AmbientOcclusionSky.frag): texel i at i / size + the quad's 0.5 / full resolution.
    vec2 inUV = uv + 0.5 * (cb.reciprocalResolution.xy - cb.skyLayout.xy);
    vec2 uv = (floor(inUV / cb.reciprocalResolution.xy) + 0.5) * cb.reciprocalResolution.xy;
    float depth = LINEAR_LOD(t_depth, uv).x * farZ;
    vec3 viewNormal = POINT_LOD(t_normals, uv).xyz * 2.0 - 1.0;
    if (cb.skyLayout.z > 0.5) viewNormal = SkyOctDecode(SkyUnpack2(POINT_LOD(t_skyPrep, inUV).y));
    float result = 1.0, skyVisibility = 1.0;
    float sunMask = cb.sky4.z > 0.5 ? mix(cb.shadowFade.x, 1.0, POINT_LOD(t_sunMask, uv).r) : 1.0;
    if (depth < endFade) {
        vec3 n = normalize((cb.invViewTransform * vec4(viewNormal, 0.0)).xyz);
        vec4 rt = LINEAR_LOD(t_rt, inUV);
        float visibility = saturate(rt.y * cb.sky3.y);
        result = SkyFactor(visibility, n, sunMask, rt.z);
        skyVisibility = visibility;
    }
    o_color = vec4(result, min(depth, endFade), skyVisibility, 1.0);
}
