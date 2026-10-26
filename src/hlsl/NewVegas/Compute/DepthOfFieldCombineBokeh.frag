// DepthOfField, technique 0, pass 6 (Combine) plus the bokeh sprites (needs NVR DXVK; fork-only, [Shaders.DepthOfField.Bokeh]
// Bokeh): DepthOfFieldCombine.frag line for line, then the sprites DepthOfFieldBokehScatter.comp scattered (a half-resolution
// fixed-point RGB buffer, read with a bilinear filter) are added in linear light with a soft limit, 1 - (1 - c) * exp(-b):
// a plain add while the sum is dark, never past white. effects/DepthOfField.cpp ExtRender.
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer
layout(set = 1, binding = 1) uniform texture2D t_source;     // TESR_SourceBuffer

layout(set = 1, binding = 2, std430) readonly buffer Accum {
    uint accum[];                                            // (r, g, b) * 65536 per half-resolution pixel
};

#include "DepthOfField.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 o_color;

vec3 bokehAt(ivec2 p, ivec2 size) {
    p = clamp(p, ivec2(0), size - 1);
    uint base = (uint(p.y) * uint(size.x) + uint(p.x)) * 3u;
    return vec3(accum[base], accum[base + 1u], accum[base + 2u]) * (1.0 / 65536.0);
}

void main() {
    vec2 uv = inUV;

    // DepthOfFieldCombine.frag
    vec2 halfPixel = pc.reciprocalResolution.xy * 1.5;
    vec2 nearImageUV = clamp(uv * 0.5, vec2(0.0, 0.0), vec2(0.5, 0.5 - halfPixel.y));
    vec2 nearBlurUV = clamp(uv * 0.5 + vec2(0.0, 0.5), vec2(0.0, 0.5 + halfPixel.y), vec2(0.5 + halfPixel.x, 1.0));
    vec2 farImageUV = clamp(uv * 0.5 + vec2(0.5, 0.0), vec2(0.5 + halfPixel.x, 0.0), vec2(1.0, 0.5 - halfPixel.y));
    vec2 farBlurUV = clamp(uv * 0.5 + vec2(0.5, 0.5), vec2(0.5 + halfPixel.x, 0.5 + halfPixel.y), vec2(1.0, 1.0));

    vec4 nearBlurImage = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), nearImageUV, 0.0);
    vec4 farBlurImage = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), farImageUV, 0.0);
    float nearBlurAmount = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), nearBlurUV, 0.0).r;
    float farBlurAmount = textureLod(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), farBlurUV, 0.0).g;

    vec4 baseColor = textureLod(sampler2D(t_source, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    vec4 color = mix(baseColor, farBlurImage, farBlurAmount);
    color = mix(color, nearBlurImage, nearBlurAmount);

    // the bokeh sprites (push pad[0], pad[1]: the buffer's half-resolution size)
    ivec2 size = ivec2(pc.pad[0], pc.pad[1]);
    vec2 p = uv * vec2(size) - 0.5;
    ivec2 i0 = ivec2(floor(p));
    vec2 f = p - vec2(i0);
    vec3 bokeh = mix(mix(bokehAt(i0, size), bokehAt(i0 + ivec2(1, 0), size), f.x),
                     mix(bokehAt(i0 + ivec2(0, 1), size), bokehAt(i0 + ivec2(1, 1), size), f.x), f.y);
    vec3 lin = saturate(linearize(color.rgb));
    lin = 1.0 - (1.0 - lin) * exp(-bokeh);

    o_color = vec4(delinearize(lin), 1.0);
}
