#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;

layout(push_constant) uniform Push {
    vec4 imageAdjustData;
    vec4 darkAdjustColor;
    vec4 lightAdjustColor;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec4 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv);

    color = mix(vec4(luma(color)), color, pc.imageAdjustData.z);

    color.rgb = pows(color.rgb, 2.2);

    vec3 darks = smoothstep(vec3(0.0), vec3(1.0), vec3(1.0) - color.rgb) * color.rgb * pows(pc.darkAdjustColor.rgb, 2.2);
    vec3 lights = smoothstep(vec3(0.0), vec3(1.0), color.rgb) * color.rgb * pows(pc.lightAdjustColor.rgb, 2.2);

    vec3 newColor = darks + lights;

    newColor *= max(0.0, pc.imageAdjustData.x);

    color.rgb = mix(color.rgb, newColor, pc.imageAdjustData.w);

    color.rgb = pows(color.rgb, 1.0 / 2.2);

    newColor = (color.rgb - vec3(0.5)) * pc.imageAdjustData.y + vec3(0.5);

    color.rgb = mix(color.rgb, newColor, pc.imageAdjustData.w);
    o_color = vec4(color.rgb, 1.0);
}
