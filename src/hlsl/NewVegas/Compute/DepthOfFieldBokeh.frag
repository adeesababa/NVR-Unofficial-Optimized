#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;

#include "DepthOfField.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 o_color;

const vec2 taps[12] = vec2[12](
    vec2(-0.326212, -0.405810),
    vec2(-0.840144, -0.073580),
    vec2(-0.695914,  0.457137),
    vec2(-0.203345,  0.620716),
    vec2( 0.962340, -0.194983),
    vec2( 0.473434, -0.480026),
    vec2( 0.519456,  0.767022),
    vec2( 0.185461, -0.893124),
    vec2( 0.507431,  0.064425),
    vec2( 0.896420,  0.412458),
    vec2(-0.321940, -0.932615),
    vec2(-0.791559, -0.597710)
);

void main() {
    vec2 uv = inUV;
    const float useBokeh = pc.useBokeh;
    vec4 quadrants = getQuadrants(uv);

    if (quadrants.w != 0.0) {
        o_color = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
        return;
    }

    uv = remapFromQuadrant(uv, quadrants);

    float farBlurAmount = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv * 0.5 + vec2(0.5, 0.5), 0.0).g;
    float centerBlur = dot(vec3(1.0, farBlurAmount, 1.0), quadrants.xyz);

    vec4 color = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), inUV, 0.0);
    vec4 bokehColor = color;
    float amount = 1.0;
    float radius = mix(pc.data.y, pc.blur.w, useBokeh);
    for (int i = 1; i < 12; i++) {
        vec2 offset = pc.reciprocalResolution.xy * taps[i] * radius * centerBlur;
        vec2 samplePos = inUV + offset;

        vec2 blurSamplePos = uv + offset * 2.0;
        farBlurAmount = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), blurSamplePos * 0.5 + vec2(0.5, 0.5), 0.0).g;
        float blur = dot(vec3(1.0, farBlurAmount, 1.0), quadrants.xyz);

        vec4 sampleQuadrants = getQuadrants(samplePos);
        float isValid = dot(quadrants, sampleQuadrants) * blur;

        vec4 sampleColor = isValid != 0.0 ? textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), samplePos, 0.0) : vec4(0.0);
        sampleColor *= isValid;
        color += sampleColor;

        bokehColor = (luma(sampleColor) > luma(bokehColor)) ? sampleColor : bokehColor;
        amount += isValid;
    }

    color /= amount;
    o_color = mix(color, bokehColor, (luma(bokehColor) > pc.data.z ? 1.0 : 0.0) * useBokeh);
}
