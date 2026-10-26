// WaterLens as a Vulkan pass (needs NVR DXVK): Effects/WaterLens.fx.hlsl. The D3D9 vertex shader's noise coordinates are
// linear in the texture coordinate, so they are computed here per pixel (the half-texel shift it adds is D3D9's pixel-centre
// convention; the pass's uv is already at pixel centres). effects/WaterLens.cpp ExtRender. t_noise is Effects\water_NRM_lens.dds (WRAP).
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer (LINEAR, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_noise;      // TESR_waterlensSampler (LINEAR, WRAP)

layout(push_constant) uniform Push {
    vec4 gameTime;        // 0: TESR_GameTime (z: time)
    vec4 waterLensData;   // 16: TESR_WaterLensData
    uint samplerIndex;    // 32: LINEAR, CLAMP
    uint wrapIndex;       // 36: LINEAR, WRAP
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

vec4 noise(vec2 at) { return texture(sampler2D(t_noise, NVR_SAMPLER(pc.wrapIndex)), at); }

void main() {
    float timea = pc.gameTime.z * pc.waterLensData.x;
    float timeb = pc.gameTime.z * pc.waterLensData.y;
    vec2 noiseA = (uv * 0.5) + vec2(sin(0.25 * timea), -0.25 * timea);
    vec2 noiseB = uv - vec2(0.0, timeb);

    vec4 normalColor = (noise(noiseA) * 2.0) - 1.0;
    vec4 animColor = (noise(noiseB) * 2.0) - 1.0;
    normalColor.z += animColor.w;
    vec3 normal = normalize(normalColor.xyz);
    vec3 refractionVec = normalize(normal * vec3(pc.waterLensData.z, pc.waterLensData.z, 1.0));
    vec3 screenColor = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv - (refractionVec.xy * 0.2 * pc.waterLensData.w)).xyz;
    vec2 refractHighlight = refractionVec.xy * pc.waterLensData.w;
    vec3 refColor = (saturate(pow(refractHighlight.x, 6.0)) + saturate(pow(refractHighlight.y, 6.0))) * vec3(0.85, 0.85, 1.0);
    o_color = vec4(screenColor + refColor, 1.0);
}
