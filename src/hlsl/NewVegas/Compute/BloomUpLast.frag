#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"
#include "Bloom.glsl"

layout(set = 1, binding = 0) uniform texture2D t_buffer;
layout(set = 1, binding = 1) uniform texture2D t_add;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    o_color = UpsampleLast(t_buffer, t_add, NVR_SAMPLER(pc.samplerIndex), uv);
}
