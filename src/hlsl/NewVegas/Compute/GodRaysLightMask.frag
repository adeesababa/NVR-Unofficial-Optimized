#version 450
#extension GL_GOOGLE_include_directive : enable
#include "GodRays.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rays;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

void main() {
    vec2 texel = cb.layout_.zw;
    vec3 color = LINEAR_LOD(t_rays, uv).rgb;
    color += LINEAR_LOD(t_rays, uv + vec2(-1.0, -1.0) * texel).rgb;
    color += LINEAR_LOD(t_rays, uv + vec2(-1.0, 1.0) * texel).rgb;
    color += LINEAR_LOD(t_rays, uv + vec2(1.0, -1.0) * texel).rgb;
    color += LINEAR_LOD(t_rays, uv + vec2(1.0, 1.0) * texel).rgb;
    color /= 5.0;

    float treshold = mix(2.0, 0.0, pow(abs(sunHeight), 8.0));
    float bloom = smoothstepHlsl(treshold, treshold + lumTreshold * 15.0, luma(color));
    color = saturate(bloom * color * 100.0 * intensity);

    o_color = vec4(color.rgb, 1.0);
}
