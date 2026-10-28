#version 450
#extension GL_GOOGLE_include_directive : require
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_fsr;
layout(set = 1, binding = 1) uniform texture2D t_source;
layout(set = 1, binding = 2) uniform texture2D t_viewmodel;
layout(set = 1, binding = 3) uniform texture2D t_motion;
layout(set = 1, binding = 4) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    uint pointIndex;
    float debugView;
    vec2 size;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define POINT_S NVR_SAMPLER(pc.pointIndex)

void main() {
    ivec2 pixel = ivec2(floor(gl_FragCoord.xy));
    vec4 source = texelFetch(sampler2D(t_source, POINT_S), pixel, 0);
    bool weapon = texelFetch(sampler2D(t_viewmodel, POINT_S), pixel, 0).x > 0.0;
    vec3 color = weapon ? source.rgb : texelFetch(sampler2D(t_fsr, POINT_S), pixel, 0).rgb;
    if (pc.debugView > 0.5 && pc.debugView < 1.5) {
        vec2 m = texelFetch(sampler2D(t_motion, POINT_S), pixel, 0).xy * pc.size / 16.0;
        color = vec3(abs(m), 0.0);
    }
    else if (pc.debugView > 4.5) {
        float d = texelFetch(sampler2D(t_depth, POINT_S), pixel, 0).x;
        color = vec3(d);
    }
    o_color = vec4(saturate(color), source.a);
}
