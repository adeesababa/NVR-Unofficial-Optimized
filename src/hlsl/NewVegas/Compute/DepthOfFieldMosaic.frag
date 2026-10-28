#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_source;

#include "DepthOfField.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 o_color;

void main() {
    vec2 uv = inUV;

    vec4 quadrants = getQuadrants(uv);
    uv = remapFromQuadrant(uv, quadrants);

    vec4 blur = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    vec4 color = textureLod(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    color = mix(color, vec4(blur.rg, 1.0, 1.0), (quadrants.z != 0.0 || quadrants.w != 0.0) ? 1.0 : 0.0);

    o_color = color;
}
