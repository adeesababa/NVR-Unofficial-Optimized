#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;
    vec4 ditherBusterData;
    vec4 ditherBusterGate;
    vec4 toneMappingClip;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec4 BufferOffset(vec2 o) {
    return texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv + o * pc.reciprocalResolution.xy);
}

float DitherGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

float TonemapDitherAt(vec2 pixel) {
    vec2 p = pixel + pc.toneMappingClip.z * vec2(5.588238, 3.235);
    return (DitherGradientNoise(p) + DitherGradientNoise(p + vec2(47.0, 17.0)) - 1.0) * pc.toneMappingClip.y;
}

float LumaWithoutDither(float luma, vec2 pixel) {
    return luma - TonemapDitherAt(pixel) * saturate(luma * 128.0);
}

float SmoothstepHLSL(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);

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
    if (pc.ditherBusterGate.y > 0.5) {
        lumaC = LumaWithoutDither(lumaC, vpos);
        lumaW = LumaWithoutDither(lumaW, vpos + vec2(-1.0, 0.0));
        lumaE = LumaWithoutDither(lumaE, vpos + vec2(1.0, 0.0));
        lumaN = LumaWithoutDither(lumaN, vpos + vec2(0.0, 1.0));
        lumaS = LumaWithoutDither(lumaS, vpos + vec2(0.0, -1.0));
    }
    vec2 gradient = vec2(lumaS - lumaN, lumaE - lumaW) * 0.5;
    float spike = abs(lumaC - lumaN) + abs(lumaC - lumaS) +
                  abs(lumaC - lumaE) + abs(lumaC - lumaW);
    float isolation = max(0.0, spike - 4.0 * length(gradient));

    if (pc.ditherBusterGate.z > 0.5) {
        o_color = vec4(saturate(center - TonemapDitherAt(vpos) * saturate(center * 128.0)), 1.0);
        return;
    }

    float gate = isolation > pc.ditherBusterData.z ? 1.0 : 0.0;
    if (pc.ditherBusterGate.x > 0.0)
        gate = SmoothstepHLSL(pc.ditherBusterData.z * (1.0 - pc.ditherBusterGate.x), pc.ditherBusterData.z * (1.0 + pc.ditherBusterGate.x), isolation);

    if (pc.ditherBusterData.z > 0.0 && gate > 0.0) {
        vec3 neighbourhood = (center + colorN + colorS + colorE + colorW) * 0.2;
        center = mix(center, neighbourhood, saturate(pc.ditherBusterData.x) * gate);
    }
    o_color = vec4(center, 1.0);
}
