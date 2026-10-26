// GodRays passes Blur1-3 (DedicatedGodRays passes 2-4, DedicatedRadialBlur) as Vulkan passes (needs NVR DXVK): Effects/GodRays.fx.hlsl
// line for line; the push block's blurStep and storeAttenuation are the passes' uniform arguments (0.1, 0.01, 0.001; 0, 0, 1).
// Reads one ray buffer and writes the other (effects/GodRays.cpp RenderDedicated).
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "GodRays.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rays;       // NVR_GodRaysBuffer (LINEAR, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_max;        // NVR_GodRaysMax (POINT, CLAMP); a stand-in when skip.x is 0 (never read then)
layout(set = 1, binding = 2) uniform texture2D t_depth;      // TESR_DepthBuffer (LINEAR, CLAMP)

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

bool GodRaysEmpty() {
    return pc.skip.x > 0.5 && POINT_LOD(t_max, vec2(0.5, 0.5)).r <= 0.0;
}

// Depth.hlsl reconstructPosition
vec3 reconstructPosition(vec2 uv) {
    float x = uv.x * 2.0 - 1.0;
    float y = (1.0 - uv.y) * 2.0 - 1.0;
    float z = projectedDepthFromLinear(LINEAR_LOD(t_depth, uv).x);
    vec4 clipSpace = vec4(x, y, z, 1.0);

    vec4 viewSpace = cb.invProjectionTransform * clipSpace;

    viewSpace /= viewSpace.w;

    return viewSpace.xyz;
}

float RayAttenuation(vec2 uv) {
    vec3 eyeDir = normalize(reconstructPosition(uv));
    float heightAttenuation = cb.godRaysData.w != 0.0 ? mix(0.2, 4.0, pows(sunHeight, 4.0)) : 1.0;
    return pow(compress(shade(cb.viewSpaceLightDir.xyz, eyeDir)), 2.5) *
        heightAttenuation * (sunHeight < 1.0 ? 1.0 : 0.0);
}

void main() {
    if (GodRaysEmpty()) { o_color = vec4(0.0, 0.0, 0.0, 1.0); return; }	// UNOFFICIAL SkipEmpty: nothing to blur this frame
    // coord is the HLSL's uv after the offset, dist its distance (GLSL names: uv is the input, distance a built-in)
    vec2 coord = uv - 0.5 * cb.reciprocalResolution.xy;

    vec2 sunPos = projectPosition(cb.viewSpaceLightDir.xyz * farZ).xy;
    vec2 blurDirection = (sunPos.xy - coord) * vec2(1.0, raspect);
    float dist = length(blurDirection);
    vec2 dir = blurDirection / max(dist, 0.000001);
    float stepSize = pc.blurStep * stepLengthMult;

    vec2 samplePos = coord;
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);
    float total = 1.0;
    for (float i = 0.0; i < samples; i++) {
        float sampleDistance = stepSize * i;
        samplePos = coord + (dir * sampleDistance / vec2(1.0, raspect));
        if (sampleDistance <= dist && samplePos.x > 0.0 && samplePos.y > 0.0 && samplePos.x < 1.0 && samplePos.y < 1.0) {
            color += LINEAR_LOD(t_rays, samplePos);
            total += 1.0;
        }
    }
    color /= total;

    // lerp(1.0, RayAttenuation(uv), storeAttenuation) with the pass's literal 0 or 1 (fxc folds it to 1 or the attenuation)
    o_color = vec4(color.rgb, pc.storeAttenuation != 0.0 ? RayAttenuation(coord) : 1.0);
}
