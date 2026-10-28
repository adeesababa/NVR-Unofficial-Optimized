#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_day;
layout(set = 1, binding = 2) uniform texture2D t_night;
layout(set = 1, binding = 3) uniform texture2D t_interior;

layout(push_constant) uniform Push {
    vec4 lutData;
    vec4 lutBlend;
    uint samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec3 SampleLUT(texture2D lut, vec3 color, float slotN) {
    float N = max(slotN, 2.0);
    float b = color.b * (N - 1.0);
    float bCell = floor(b);
    float bFrac = fract(b);
    float invW = 1.0 / (N * N);
    float invH = 1.0 / N;
    vec2 uv1, uv2;
    uv1.x = (bCell * N + color.r * (N - 1.0) + 0.5) * invW;
    uv1.y = (color.g * (N - 1.0) + 0.5) * invH;
    uv2.x = ((bCell + 1.0) * N + color.r * (N - 1.0) + 0.5) * invW;
    uv2.y = uv1.y;
    vec3 graded = mix(texture(sampler2D(lut, NVR_SAMPLER(pc.samplerIndex)), uv1).rgb, texture(sampler2D(lut, NVR_SAMPLER(pc.samplerIndex)), uv2).rgb, bFrac);
    return slotN >= 2.0 ? graded : color;
}

void main() {
    vec3 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv).rgb;
    float strength = pc.lutData.y;
    float hdrCompat = pc.lutBlend.z;
    float scale = max(max(color.r, color.g), max(color.b, 1.0));
    vec3 lutInput = color / mix(1.0, scale, hdrCompat);
    vec3 dayColor = SampleLUT(t_day, lutInput, pc.lutData.x);
    vec3 nightColor = SampleLUT(t_night, lutInput, pc.lutData.z);
    vec3 interiorColor = SampleLUT(t_interior, lutInput, pc.lutData.w);
    vec3 exteriorColor = mix(nightColor, dayColor, pc.lutBlend.x);
    vec3 graded = mix(exteriorColor, interiorColor, pc.lutBlend.y);
    graded *= mix(1.0, scale, hdrCompat);
    o_color = vec4(mix(color, graded, strength), 1.0);
}
