#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;
    vec2 offsetMask;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const int cKernelSize = 13;

const float blurWeights[cKernelSize] = float[cKernelSize](
    0.002216,
    0.008764,
    0.026995,
    0.064759,
    0.120985,
    0.176033,
    0.199471,
    0.176033,
    0.120985,
    0.064759,
    0.026995,
    0.008764,
    0.002216
);

void main() {
    vec3 color = vec3(0.0);

    for (int i = 0; i < cKernelSize; i++) {
        vec2 blurOffset = float(i - 6) * pc.reciprocalResolution.xy;
        color += texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv + blurOffset * pc.offsetMask).rgb * blurWeights[i];
    }

    o_color = vec4(color, 1.0);
}
