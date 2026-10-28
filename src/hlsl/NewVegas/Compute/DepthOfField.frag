#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 viewTransform[4];
    vec4 projectionTransform[4];
    vec4 reciprocalResolution;
    vec4 blur;
    vec4 data;
    vec4 cameraPosition;
    vec4 cameraData;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

float readDepth(vec2 coord) {
    return textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0).x * pc.cameraData.y;
}
float readDepthLod(vec2 coord) { return readDepth(coord); }

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(pc.viewTransform[0][2], pc.viewTransform[1][2], pc.viewTransform[2][2]);
    v += (1.0 / pc.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(pc.viewTransform[0][0], pc.viewTransform[1][0], pc.viewTransform[2][0]);
    v += (-1.0 / pc.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(pc.viewTransform[0][1], pc.viewTransform[1][1], pc.viewTransform[2][1]);
    return v;
}

float invlerps(float a, float b, float t) { return saturate((t - a) / (b - a)); }

void main() {
    const float DistantBlur = pc.blur.x != 0.0 ? 1.0 : 0.0;
    const float DistantStart = pc.blur.y;
    const float DistantEnd = pc.blur.z;
    const float BaseBlurRadius = pc.blur.w;

    float depth = readDepth(uv);
    vec3 cameraVector = toWorld(uv) * depth;
    float worldHeight = pc.cameraPosition.z + cameraVector.z;
    float blur = saturate(invlerps(DistantStart, DistantEnd, depth) * DistantBlur * invlerps(100000.0, 5000.0, worldHeight));

    vec4 center = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    if (blur <= 0.0) {
        o_color = vec4(center.rgb, 1.0);
        return;
    }

    vec2 radius = pc.reciprocalResolution.xy * BaseBlurRadius * (1.0 + blur);
    const vec2 corners[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0), vec2(1.0, 1.0));
    vec4 blurred = vec4(0.0);
    float weights = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 tapUV = uv + radius * corners[k];
        float weight = saturate(1.5 - abs(readDepthLod(tapUV) - depth) / (0.15 * depth));
        blurred += textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), tapUV, 0.0) * weight;
        weights += weight;
    }
    blurred = weights > 0.0001 ? blurred / weights : center;
    o_color = vec4(mix(center.rgb, blurred.rgb, blur), 1.0);
}
