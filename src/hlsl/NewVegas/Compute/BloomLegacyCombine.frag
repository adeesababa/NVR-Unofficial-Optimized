#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_bloom;
layout(set = 1, binding = 1) uniform texture2D t_source;

layout(push_constant) uniform Push {
    vec4 bloomValues;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec3 adjustSaturation(vec3 color, float saturation) {
    float grey = dot(color, vec3(0.2126, 0.7152, 0.0722));

    return mix(vec3(grey), color, saturation);
}

void main() {
    vec3 bloomColor = saturate(texture(sampler2D(t_bloom, NVR_SAMPLER(pc.samplerIndex)), uv).rgb);
    vec3 originalColor = texture(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), uv).rgb;

    bloomColor = adjustSaturation(bloomColor, pc.bloomValues.z) * pc.bloomValues.x;
    originalColor = adjustSaturation(originalColor, pc.bloomValues.w) * pc.bloomValues.y;
    originalColor *= max((max(originalColor, vec3(1.0)) - bloomColor), vec3(0.0));

    o_color = vec4(originalColor + bloomColor, 1.0);
}
