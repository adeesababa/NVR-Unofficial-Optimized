// UNOFFICIAL 2026-10-29 shared RT denoiser (core\RtDenoise.h), RGB mode, pass 2 (run 3 times): the edge-stopping a-trous of
// RtDenoiseAtrous.frag on rgb, the value weight from the luma's temporal variance. In / out: A = (rgb, history length),
// B = (luma second moment, view depth, 0, 0); the last iteration writes the caller's target (rgb in A's place, B to a scratch).
// Compile: glslangValidator -V --target-env vulkan1.1 -o RtDenoiseAtrousRgb.spv RtDenoiseAtrousRgb.frag
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_a;
layout(set = 1, binding = 1) uniform texture2D t_normals;
layout(set = 1, binding = 2) uniform texture2D t_b;

layout(push_constant) uniform Push {
    vec4 params;   // x step, y depth sigma, z normal power, w value sigma
    vec4 size;     // x 1 / width, y 1 / height
    uint samplerIndex;
    uint pad[3];
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_a;
layout(location = 1) out vec4 o_b;

const float kernel[3] = float[3](3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);

void main() {
    const vec4 ca = textureLod(sampler2D(t_a, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    const vec4 cb = textureLod(sampler2D(t_b, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0);
    const vec3 n = normalize(textureLod(sampler2D(t_normals, NVR_SAMPLER(pc.samplerIndex)), uv, 0.0).xyz * 2.0 - 1.0);
    const float z = cb.g;
    if (z <= 0.0) { o_a = ca; o_b = cb; return; }
    const float l = luma(ca.rgb);
    const float variance = max(cb.r - l * l, 0.0);
    const float valueSigma = pc.params.w * sqrt(variance + (ca.a < 4.0 ? 0.05 : 0.0)) + 1.0e-3;
    const float zSlope = max(abs(dFdx(z)), abs(dFdy(z))) * pc.params.x + 1.0e-2 * z;

    vec3 sum = vec3(0.0);
    float moment = 0.0, weights = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            const vec2 tapUv = uv + vec2(x, y) * pc.params.x * pc.size.xy;
            const vec4 ta = textureLod(sampler2D(t_a, NVR_SAMPLER(pc.samplerIndex)), tapUv, 0.0);
            const vec4 tb = textureLod(sampler2D(t_b, NVR_SAMPLER(pc.samplerIndex)), tapUv, 0.0);
            if (tb.g <= 0.0) continue;
            const vec3 tn = normalize(textureLod(sampler2D(t_normals, NVR_SAMPLER(pc.samplerIndex)), tapUv, 0.0).xyz * 2.0 - 1.0);
            float w = kernel[abs(x)] * kernel[abs(y)];
            w *= exp(-abs(tb.g - z) / (pc.params.y * zSlope * length(vec2(x, y)) + 1.0e-3));
            w *= pow(max(dot(n, tn), 0.0), pc.params.z);
            w *= exp(-abs(luma(ta.rgb) - l) / valueSigma);
            sum += ta.rgb * w;
            moment += tb.r * w;
            weights += w;
        }
    }
    o_a = vec4(weights > 0.0 ? sum / weights : ca.rgb, ca.a);
    o_b = vec4(weights > 0.0 ? moment / weights : cb.r, z, 0.0, 0.0);
}
