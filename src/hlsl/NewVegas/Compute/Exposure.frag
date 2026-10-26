// Exposure as a Vulkan pass (needs NVR DXVK): Effects/Exposure.fx.hlsl line for line. effects/Exposure.cpp ExtRender.
#version 450
#extension GL_EXT_control_flow_attributes : enable
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer
layout(set = 1, binding = 1) uniform texture2D t_avgLuma;    // TESR_AvgLumaBuffer

layout(push_constant) uniform Push {
    vec4 exposureData;     // 0: TESR_ExposureData (x min brightness, y max brightness)
    uint samplerIndex;     // 16: LINEAR, CLAMP
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    float minBrightness = pc.exposureData.x;
    float maxBrightness = pc.exposureData.y;
    vec4 color = linearize(texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv));
    float averageLuma = texture(sampler2D(t_avgLuma, NVR_SAMPLER(pc.samplerIndex)), vec2(0.5, 0.5)).g;

    float negativeLumaDiff = max(log(minBrightness / averageLuma), 0.0) + 1.0;
    float additiveLumaDiff = max(log(averageLuma / minBrightness), 0.0) + 1.0;
    float lumaDiff = additiveLumaDiff / negativeLumaDiff;

    o_color = delinearize(vec4(mix(color.rgb, color.rgb / lumaDiff, maxBrightness), 1.0));
}
