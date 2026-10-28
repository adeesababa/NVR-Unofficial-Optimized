#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_atlas;

#define VL_DEPTH
#define VL_SHADOWS
#include "VolumetricLight.glsl"

float GradientNoise(vec2 pixel) {
    pixel += NoiseFrame * 5.588238;
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 coord = (vpos + 0.5) * LowTexel;
    float depth = readDepthLod(coord - 0.5 * (LowTexel - cb.reciprocalResolution.xy));
    float isSky = depth >= farZ * 0.996 ? 1.0 : 0.0;
    float planar = isSky > 0.0 ? MarchDistance : min(depth, MarchDistance);

    vec3 ray = toWorld(coord);
    float sigma = Density * length(ray);
    float dt = planar / Steps;
    float offset = GradientNoise(vpos);

    float lit = 0.0, all = 0.0;
    for (float i = 0.0; i < Steps; i += 1.0) {
        float t = (i + offset) * dt;
        float transmittance = exp(-sigma * t);
        lit += MarchShadow(ray * t) * transmittance;
        all += transmittance;
    }
    float scattered = sigma * dt;
    o_color = vec4(lit * scattered, all * scattered, depth / farZ, 1.0);
}
