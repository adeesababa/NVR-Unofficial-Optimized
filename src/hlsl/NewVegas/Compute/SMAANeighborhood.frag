#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_blend;

layout(push_constant) uniform Push {
    vec4 resolution;
    vec4 data;
    vec4 depthData;
    vec4 pred;
    vec4 toneMappingClip;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv_in;
layout(location = 0) out vec4 o_color;

#define SMAA_RT_METRICS pc.resolution

vec4 Blend(vec2 coord) { return texture(sampler2D(t_blend, NVR_SAMPLER(pc.samplerIndex)), coord); }
vec4 ColorLevelZero(vec2 coord) { return textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0); }

void main() {
    vec2 texcoord = uv_in + pc.data.w * pc.resolution.xy;
    vec4 offset = SMAA_RT_METRICS.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + texcoord.xyxy;

    vec4 a;
    a.x = Blend(offset.xy).a;
    a.y = Blend(offset.zw).g;
    a.wz = Blend(texcoord).xz;

    if (dot(a, vec4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
        o_color = ColorLevelZero(texcoord);
    } else {
        bool h = max(a.x, a.z) > max(a.y, a.w);
        vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
        vec2 blendingWeight = a.yw;
        if (h) blendingOffset = vec4(a.x, 0.0, a.z, 0.0);
        if (h) blendingWeight = a.xz;
        blendingWeight /= dot(blendingWeight, vec2(1.0, 1.0));
        vec4 blendingCoord = blendingOffset * vec4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy) + texcoord.xyxy;
        vec4 color = blendingWeight.x * ColorLevelZero(blendingCoord.xy);
        color += blendingWeight.y * ColorLevelZero(blendingCoord.zw);
        o_color = color;
    }
}
