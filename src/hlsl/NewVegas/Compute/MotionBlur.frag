// MotionBlur as a Vulkan pass (needs NVR DXVK): Effects/MotionBlur.fx.hlsl line for line; the technique runs this pass twice
// in a row (effects/MotionBlur.cpp ExtRender does the same through the frame chain).
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;  // 0: TESR_ReciprocalResolution
    vec4 blurParams;            // 16: TESR_MotionBlurParams
    vec4 blurData;              // 32: TESR_MotionBlurData
    uint samplerIndex;          // 48: LINEAR, CLAMP
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
