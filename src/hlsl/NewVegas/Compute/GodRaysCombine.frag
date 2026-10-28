#version 450
#extension GL_GOOGLE_include_directive : enable
#include "GodRays.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_rays;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec3 GetSunColor(float sunHeight_, float athmosphere, float dayTime, vec3 sunColor, vec3 sunsetColor) {
    sunColor = linearize(sunColor);
    sunsetColor = linearize(sunsetColor);

    vec3 color = (1.0 + sunHeight_) * sunColor;
    float sunSet = saturate(pows(1.0 - sunHeight_, 8.0)) * dayTime;
    color += sunsetColor.rgb * sunSet * athmosphere;
    return color;
}

void main() {
    vec4 source = LINEAR_LOD(t_rendered, uv);
    vec4 rays = LINEAR_LOD(t_rays, uv);
    if (pc.skip.y > 0.5 && !any(notEqual(rays.rgb, vec3(0.0)))) { o_color = vec4(source.rgb, 1.0); return; }
    vec4 color = linearize(source);

    vec3 sunColor = GetSunColor(shade(cb.sunDirection.xyz, blue.xyz), 1.0, cb.sunAmount.x, cb.sunColor.rgb, cb.sunsetColor.rgb);
    vec3 godRayColor = linearize(cb.godRaysRayColor).rgb;

    rays.rgb *= multiplier * mix(sunColor, godRayColor, cb.godRaysRayColor.w);
    rays.rgb *= rays.a;

    color += max(rays, 0.0) * 5.0 * color + max(rays, 0.0) * 0.2;
    color = delinearize(color);
    o_color = vec4(color.rgb, 1.0);
}
