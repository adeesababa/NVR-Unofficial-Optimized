#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_visibility;

layout(push_constant) uniform Push {
    float strength;
    uint samplerIndex;
    uint grey;
    uint pad;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    float visibility = textureLod(sampler2D(t_visibility, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0).r;
    float v = mix(1.0, visibility, pc.strength);
    o_color = pc.grey != 0u ? vec4(v, v, v, 1.0) : vec4(v, 1.0, 1.0, 1.0);
}
