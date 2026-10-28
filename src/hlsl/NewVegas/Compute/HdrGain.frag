#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_samplerless_texture_functions : require
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_scene;
layout(set = 1, binding = 1) uniform texture2D t_sdr;

layout(push_constant) uniform Push {
    float peak;
    float knee;
    float strength;
    float sceneGamma;
    float exposure;
    float outGamma;
    float pad0;
    float pad1;
    uint  samplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_gain;

void main() {
    vec3 scene = texture(sampler2D(t_scene, NVR_SAMPLER(pc.samplerIndex)), uv).rgb;
    vec3 sdr = texelFetch(t_sdr, ivec2(gl_FragCoord.xy), 0).rgb;

    vec3 sceneLin = pc.sceneGamma > 0.0 ? pows(max(scene, vec3(0.0)), pc.sceneGamma) : linearize(max(scene, vec3(0.0)));
    vec3 sdrLin = pc.outGamma > 0.0 ? pow(saturate(sdr), vec3(pc.outGamma)) : linearize(saturate(sdr));

    float sceneY = luma(sceneLin) * pc.exposure;
    float sdrY = max(luma(sdrLin), 1e-4);
    float sdrMax = max(sdrLin.r, max(sdrLin.g, sdrLin.b));

    float hdrY = pc.peak * (1.0 - exp(-sceneY / pc.peak));
    float gain = max(hdrY / sdrY, 1.0);

    float t = smoothstep(pc.knee, 1.0, sdrMax) * saturate(pc.strength);
    gain = clamp(mix(1.0, gain, t), 1.0, pc.peak);

    o_gain = vec4(gain, gain, gain, 1.0);
}
