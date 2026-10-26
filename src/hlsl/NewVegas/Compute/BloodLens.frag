// BloodLens as a Vulkan pass (needs NVR DXVK): Effects/BloodLens.fx.hlsl line for line. effects/BloodLens.cpp ExtRender.
// t_blood is the file texture Effects\bloodlens.dds (WRAP sampler, the second sampler slot).
#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;   // TESR_RenderedBuffer (LINEAR, CLAMP)
layout(set = 1, binding = 1) uniform texture2D t_blood;      // TESR_BloodLensSampler (LINEAR, WRAP)

layout(push_constant) uniform Push {
    vec4 params;          // 0: TESR_BloodLensParams
    vec4 bloodColor;      // 16: TESR_BloodLensColor
    uint samplerIndex;    // 32: LINEAR, CLAMP
    uint wrapIndex;       // 36: LINEAR, WRAP
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

const float Shininess = 2.5;

vec4 blood(vec2 at) { return texture(sampler2D(t_blood, NVR_SAMPLER(pc.wrapIndex)), at); }

void main() {
    vec3 sColor = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uv).rgb;
    vec2 texCoordWarp = blood(uv + vec2(pc.params.x + 0.25, pc.params.x - 0.25)).xy * pc.params.z;
    float scaledNoise = saturate(blood(uv + texCoordWarp + vec2(pc.params.x, -pc.params.x)).w - pc.params.y);
    scaledNoise = mix(0.0, pc.params.w, scaledNoise);
    vec3 normals = normalize(vec3(dFdx(scaledNoise), dFdy(scaledNoise), 0.1));
    normals = pow(abs(normals), vec3(3.0));
    vec3 resultColor = (sColor - scaledNoise) + (scaledNoise * pc.bloodColor.rgb);
    vec3 refColor = pow(normals.x, Shininess) * (pc.bloodColor.rgb * 0.5);
    o_color = vec4(resultColor + refColor, 1.0);
}
