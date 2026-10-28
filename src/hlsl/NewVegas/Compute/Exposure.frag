#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_avgLuma;

layout(push_constant) uniform Push {
    vec4 exposureData;
    uint samplerIndex;
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
