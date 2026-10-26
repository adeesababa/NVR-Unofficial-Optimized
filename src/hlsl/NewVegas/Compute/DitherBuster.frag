// DitherBuster as a Vulkan pass (needs NVR DXVK): Effects/DitherBuster.fx.hlsl technique POST_PROCESS_ANTI_ALIASING
// (DitherOnly) line for line. effects/DitherBuster.cpp ExtRender. LINEAR, CLAMP, no mips, as the technique's samplers.
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;  // 0: TESR_ReciprocalResolution
    vec4 ditherBusterData;      // 16: TESR_DitherBusterData (x strength, y mask power, z dither threshold, w radius)
    uint samplerIndex;          // 32: LINEAR, CLAMP
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

// GetBufferOffset(TESR_RenderedBuffer, o): the texel offset o, in texels
vec4 BufferOffset(vec2 o) {
    return texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv + o * pc.reciprocalResolution.xy);
}

void main() {
    // DitherOnly
    vec3 center = BufferOffset(vec2(0.0, 0.0)).rgb;
    vec3 colorW = BufferOffset(vec2(-1.0, 0.0)).rgb;
    vec3 colorE = BufferOffset(vec2(1.0, 0.0)).rgb;
    vec3 colorN = BufferOffset(vec2(0.0, 1.0)).rgb;
    vec3 colorS = BufferOffset(vec2(0.0, -1.0)).rgb;
    const vec3 LumaWeights = vec3(0.2126, 0.7152, 0.0722);
    float lumaC = dot(center, LumaWeights);
    float lumaW = dot(colorW, LumaWeights);
    float lumaE = dot(colorE, LumaWeights);
    float lumaN = dot(colorN, LumaWeights);
    float lumaS = dot(colorS, LumaWeights);
    vec2 gradient = vec2(lumaS - lumaN, lumaE - lumaW) * 0.5;
    float spike = abs(lumaC - lumaN) + abs(lumaC - lumaS) +
                  abs(lumaC - lumaE) + abs(lumaC - lumaW);
    float isolation = max(0.0, spike - 4.0 * length(gradient));

    if (pc.ditherBusterData.z > 0.0 && isolation > pc.ditherBusterData.z) {
        vec3 neighbourhood = (center + colorN + colorS + colorE + colorW) * 0.2;
        center = mix(center, neighbourhood, saturate(pc.ditherBusterData.x));
    }
    o_color = vec4(center, 1.0);
}
