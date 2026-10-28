#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;
    vec4 blurParams;
    vec4 blurData;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const int BlurSamples = 24;

void main() {
    vec2 offsetBlurDirection;
    vec3 blurredColor = vec3(0.0);
    float gaussianSum = 0.0;
    offsetBlurDirection.x = clamp(pc.blurData.x, -pc.blurParams.z, pc.blurParams.z);
    offsetBlurDirection.y = clamp(pc.blurData.y, -pc.blurParams.z, pc.blurParams.z);
    for (int i = 0; i < BlurSamples / 2; i++) {
        vec2 lookup;
        vec4 offsetColor;
        float weight = exp(float(i * i) / (5.0 * pc.blurParams.x * pc.blurParams.x)) / pc.blurParams.x;
        lookup = offsetBlurDirection * pc.reciprocalResolution.xy * pc.blurParams.y;
        lookup = lookup * float(i) / float(BlurSamples) + uv;
        offsetColor = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), lookup);
        gaussianSum += weight;
        blurredColor += offsetColor.rgb * weight;
        lookup = -offsetBlurDirection * pc.reciprocalResolution.xy * pc.blurParams.y;
        lookup = lookup * float(i) / float(BlurSamples) + uv;
        offsetColor = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), lookup);
        gaussianSum += weight;
        blurredColor += offsetColor.rgb * weight;
    }
    blurredColor = blurredColor / gaussianSum;
    o_color = vec4(blurredColor, 1.0);
}
