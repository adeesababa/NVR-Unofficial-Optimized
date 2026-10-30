// GodRays pass SkyMask (DedicatedGodRays pass 0, DedicatedSkyMask) as a Vulkan pass (needs NVR DXVK): Effects/GodRays.fx.hlsl
// line for line, into NVR_GodRaysBuffer0 (or Low0). uv is HalfVS's: the half-resolution texel centre.
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "GodRays.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer (LINEAR, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_depth;      // TESR_DepthBuffer (LINEAR, CLAMP)

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

// reconstructPosition() for a depth that was already read (same maths, one fewer depth fetch).
vec3 reconstructPositionFromDepth(vec2 uv, float linearDepth01) {
    vec4 viewSpace = cb.invProjectionTransform * vec4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, projectedDepthFromLinear(linearDepth01), 1.0);
    viewSpace /= viewSpace.w;
    return viewSpace.xyz;
}

void main() {
    float rawDepth = LINEAR_LOD(t_depth, uv).x;
    float depth = ((rawDepth * farZ) / farZ) > 0.9 ? 1.0 : 0.0;
    float horizon = smoothstepHlsl(0.0, 0.01, sunHeight);
    if (depth * horizon == 0.0) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }

    float sunset = pows(sunHeight, 8.0);
    // UNOFFICIAL 2026-10-29 SunsetGlareFix (godRaysRay.w 1): the sunset boost at the horizon, as GlareSunColor in the .fx; 0: as before
    vec3 sunColor = linearize(cb.sunColor).rgb + linearize(cb.sunsetColor.rgb) * (cb.godRaysRay.w > 0.5 ? sunset : 1.0 - sunset);
    float glarePower = mix(0.1, 8.0, sunset);

    vec3 sunGlare = vec3(pows(dot(cb.viewSpaceLightDir.xyz, normalize(reconstructPositionFromDepth(uv, rawDepth))), 180.0) * glarePower);
    vec3 color = linearize(LINEAR_LOD(t_rendered, uv)).rgb;
    color = (color + sunGlare * sunColor) * depth * horizon;

    o_color = vec4(color, 1.0);
}
