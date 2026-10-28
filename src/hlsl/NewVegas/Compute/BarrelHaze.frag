#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;

layout(push_constant) uniform Push {
    vec4 reciprocalResolution;
    vec4 hazeLine;
    vec4 hazeData;
    vec4 hazeAnimation;
    vec4 hazeBlast;
    vec4 cameraData;
    uint linearIndex;
    uint pointIndex;
    uint pad[2];
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.pointIndex)), coord).x * pc.cameraData.y;
}

void main() {
    vec2 pixel = (uv - pc.hazeLine.xy) / pc.reciprocalResolution.xy;
    vec2 barrel = (pc.hazeLine.zw - pc.hazeLine.xy) / pc.reciprocalResolution.xy;
    float along = clamp(dot(pixel, barrel) / max(dot(barrel, barrel), 1.0), 0.0, 1.0);
    vec2 relative = pixel - barrel * along;
    float height = max(pc.hazeData.z, 1.0), width = max(pc.hazeData.w, 1.0);
    float rise = -relative.y / height;

    float sleeve = exp(-dot(relative, relative) / (width * width));
    float plume = smoothstep(0.0, 0.12, rise) * (1.0 - smoothstep(0.65, 1.0, rise)) * exp(-relative.x * relative.x / (width * width));
    float mask = max(sleeve, plume) * pc.hazeData.x;
    if (pc.hazeAnimation.z < 0.5) mask *= step(pc.hazeAnimation.w * 0.8, readDepth(uv));
    float phase = pc.hazeAnimation.x * pc.hazeAnimation.y;
    vec2 wave = vec2(sin(relative.x / width * 2.7 + rise * 14.0 - phase * 4.1),
                     sin(relative.x / width * 4.3 - rise * 19.0 + phase * 2.9));
    vec2 offset = wave * vec2(1.0, 0.35) * pc.hazeData.y * mask * pc.reciprocalResolution.xy;

    float radius = max(pc.hazeBlast.y, 1.0);
    float dist = length(pixel);
    float blastMask = exp(-dist * dist / (radius * radius)) * step(0.01, pc.hazeBlast.x);
    if (pc.hazeAnimation.z < 0.5) blastMask *= step(pc.hazeAnimation.w * 0.8, readDepth(uv));
    float ripple = sin(dist / radius * 9.0 - phase * 40.0) + 0.5 * sin(dist / radius * 17.0 + phase * 23.0);
    offset += (pixel / max(dist, 1.0)) * ripple * pc.hazeBlast.x * blastMask * pc.reciprocalResolution.xy;

    vec4 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.linearIndex)), clamp(uv + offset, 0.0, 1.0));
    if (pc.hazeAnimation.z > 0.5) color.rgb = mix(color.rgb, vec3(1.0, 0.2, 0.0), clamp(mask, 0.0, 1.0) * 0.65);
    if (pc.hazeAnimation.z > 0.5) color.rgb = mix(color.rgb, vec3(0.0, 0.6, 1.0), clamp(blastMask * pc.hazeBlast.x / 6.0, 0.0, 1.0) * 0.6);
    o_color = color;
}
