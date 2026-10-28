#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_depth;
layout(set = 1, binding = 2) uniform texture2D t_noise;

layout(push_constant) uniform Push {
    vec4 cinemaData;
    vec4 cinemaSettings;
    vec4 gameTime;
    vec4 reciprocalResolution;
    vec4 cameraData;
    vec4 skyDitherData;
    uint samplerIndex;
    uint noiseSamplerIndex;
    uint dither;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec2 expand2(vec2 v) { return ((v) - 0.5) / 0.5; }

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord).x * pc.cameraData.y;
}

float overlayChannel(float base, float blend) {
    return (base <= 0.5) ? 2.0 * base * blend : 1.0 - 2.0 * (1.0 - base) * (1.0 - blend);
}

vec4 blendModeOverlay(vec4 base, vec4 blend) {
    vec4 result = vec4(0.0);
    result.r = overlayChannel(base.r, blend.r);
    result.g = overlayChannel(base.g, blend.g);
    result.b = overlayChannel(base.b, blend.b);
    result.a = blend.a;
    return result * result.a + (1.0 - result.a) * base;
}

vec3 random(vec2 seed) {
    return texture(sampler2D(t_noise, NVR_SAMPLER(pc.noiseSamplerIndex)), (seed / 256.0 + 0.5) / pc.reciprocalResolution.xy).xyz;
}

vec4 cinema(vec2 uv) {
    float aspectRatio = pc.cinemaData.x;
    if (aspectRatio != 1.0) {
        float letterboxHeight = (1.0 - pc.reciprocalResolution.z / aspectRatio) / 2.0;
        float depth = readDepth(uv);
        if ((uv.y < letterboxHeight || uv.y > 1.0 - letterboxHeight) && depth > pc.cinemaSettings.w)
            return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float time = pc.gameTime.z * 25.0;
    vec2 chromaShift = pc.reciprocalResolution.xy * pc.cinemaSettings.z;
    vec2 posToCenter = vec2(0.0) - expand2(uv);

    vec4 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv);
    color.r = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv + posToCenter * chromaShift).r;
    color.b = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv - posToCenter * chromaShift).b;

    float grainAmount = pc.cinemaSettings.y * 0.01;
    vec3 noise1 = random(uv + time * pc.reciprocalResolution.xy);
    vec3 noise2 = random(uv - time * pc.reciprocalResolution.xy);
    color = blendModeOverlay(vec4(0.5 + grainAmount * ((noise1.r * noise2.b * 2.0 - 0.5) / 0.5)), color);

    float overlayStrength = pc.cinemaData.w;
    color = mix(color, blendModeOverlay(color, color), overlayStrength);

    float softness = pc.cinemaData.y;
    float intensity = pc.cinemaData.z;
    float dist = length(posToCenter) / 2.0;
    float e0 = 1.0;
    float e1 = 1.0 - softness;
    float s = clamp((dist - e0) / (e1 - e0), 0.0, 1.0);
    float vignette = s * s * (3.0 - 2.0 * s);

    color.rgb *= vignette * intensity + (1.0 - intensity);

    return vec4(color.rgb, 1.0);
}

float gradientNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

void main() {
    vec4 color = cinema(uv);
    if (pc.dither != 0) {
        if (pc.skyDitherData.y > 0.0 && texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), uv).x < 0.996) {
            o_color = color;
            return;
        }
        vec2 pixel = floor(gl_FragCoord.xy) + pc.skyDitherData.z * vec2(5.588238, 3.235);
        float noise = gradientNoise(pixel) + gradientNoise(pixel + vec2(47.0, 17.0)) - 1.0;
        color.rgb += noise * pc.skyDitherData.x;
    }
    o_color = color;
}
