#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_mask;
layout(set = 1, binding = 2) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 cameraData;
    vec4 shadowData;
    vec4 shadowFade;
    mat4 viewTransform;
    mat4 projectionTransform;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec3 toWorld(vec2 tex) {
    mat4 V = pc.viewTransform;
    mat4 P = pc.projectionTransform;
    vec3 v = vec3(V[0][2], V[1][2], V[2][2]);
    v += (1.0 / P[0][0] * (2.0 * tex.x - 1.0)) * vec3(V[0][0], V[1][0], V[2][0]);
    v += (-1.0 / P[1][1] * (2.0 * tex.y - 1.0)) * vec3(V[0][1], V[1][1], V[2][1]);
    return v;
}

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord).x * pc.cameraData.y;
}

float invlerps(float a, float b, float t) {
    return clamp((t - a) / (b - a), 0.0, 1.0);
}

vec4 applyInteriorShadowDepth(vec2 coord, vec4 color, float shadowSample, float depth) {
    color.rgb = pows(color.rgb, 2.2);
    vec3 eyeDir = toWorld(coord);
    float uniformDepth = length(depth * eyeDir);

    float Shadow = clamp(shadowSample, 0.0, 1.0);
    Shadow = mix(Shadow, 1.0, invlerps(300.0, pc.shadowFade.w, uniformDepth));
    float darkness = (pc.shadowData.y * 5.0) / (1.0 + pc.shadowData.y * 5.0);
    darkness = clamp(1.0 - darkness * pc.shadowFade.y, 0.0, 1.0);

    Shadow = clamp(mix(darkness, 1.0, Shadow), 0.0, 1.0);
    float shadowPower = 1.0 - Shadow;

    vec4 finalColor = clamp(Shadow * pow(clamp(color, 0.0, 1.0), vec4(1.0 - shadowPower * 0.2)), 0.0, 1.0);
    finalColor = mix(vec4(luma(finalColor)), finalColor, 1.0 + shadowPower * 0.3);

    finalColor += max(color - 1.0, 0.0);
    finalColor.rgb = pows(finalColor.rgb, 1.0 / 2.2);

    return vec4(finalColor.rgb, 1.0);
}

void main() {
    vec4 rendered = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv);
    float mask = texture(sampler2D(t_mask, NVR_SAMPLER(pc.samplerIndex)), uv).r;
    o_color = applyInteriorShadowDepth(uv, rendered, mask, readDepth(uv));
}
