#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : enable
#include "Common.glsl"
#include "Underwater.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_noise;
layout(set = 1, binding = 3) uniform texture2D t_caustics;

layout(push_constant) uniform Push {
    uint linearIndex;
    uint noiseIndex;
    uint causticsIndex;
    uint pad;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec3 random(vec2 seed) {
    return textureLod(sampler2D(t_noise, NVR_SAMPLER(pc.noiseIndex)), (seed / 256.0 + 0.5) / cb.reciprocalResolution.xy, 0.0).xyz;
}

void main() {
    vec2 uv2 = uv * 2.0;
    if (any(greaterThan(uv2, vec2(1.0)))) {
        o_color = texelFetch(t_rendered, ivec2(gl_FragCoord.xy), 0);
        return;
    }

    vec3 eyeVector = toWorld(uv2);
    float depth = length(readDepth(uv2, t_depth, pc.linearIndex) * eyeVector);

    float godraysMaxDistance = 1000.0;
    float stepSize = godraysMaxDistance / 50.0;
    vec3 stepVector = eyeVector * stepSize;

    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    vec3 samplingUV = cb.cameraPosition.xyz;
    float sunL = sunLuma();
    for (int i = 1; i < 50; i++) {
        float fi = float(i);
        samplingUV += stepVector * mix(vec3(0.5), vec3(1.0), random(uv2 + cb.reciprocalResolution.xx * fi));
        float rayDistance = stepSize * fi;
        float samplingDepthFade = hsmooth(800.0, 0.0, cb.waterSettings.x - samplingUV.z);
        float samplingDistanceFade = 1.0 - rayDistance / godraysMaxDistance;
        vec4 rayColor = mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(1.0, 1.0, 1.0, 1.0), samplingDistanceFade) * sunL * 0.06;
        color += getCaustics(samplingUV, t_caustics, pc.causticsIndex) * rayColor * samplingDepthFade * samplingDistanceFade * float(rayDistance < depth);
    }

    o_color = vec4(smoothstep(0.1, 0.4, color).rgb, 1.0);
}
