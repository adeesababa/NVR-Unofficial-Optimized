#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

#define BIAS 0.018

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_normals;
layout(set = 1, binding = 2) uniform texture2D t_pointShadow;
layout(set = 1, binding = 3) uniform textureCube t_cube6;
layout(set = 1, binding = 4) uniform textureCube t_cube7;
layout(set = 1, binding = 5) uniform textureCube t_cube8;
layout(set = 1, binding = 6) uniform textureCube t_cube9;
layout(set = 1, binding = 7) uniform textureCube t_cube10;

layout(set = 2, binding = 0, std140) uniform Constants {
    vec4 cameraPosition;
    vec4 cameraData;
    mat4 viewTransform;
    mat4 projectionTransform;
    vec4 shadowFade;
    vec4 shadowLightPosition[12];
    vec4 lightColor[24];
} cb;

layout(push_constant) uniform Push {
    uint samplerIndex;
    uint pointSamplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

float readDepth(vec2 coord) {
    return texture(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord).x * cb.cameraData.y;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

vec3 getWorldNormal(vec2 coord) {
    vec4 normal = vec4(texture(sampler2D(t_normals, NVR_SAMPLER(pc.pointSamplerIndex)), coord).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

float GetPointLightAmountValue(textureCube cube, uint samplerIndex, vec3 lightDir, float dist) {
    if (cb.shadowFade.z == 0.0) return 1.0;

    float lightDepth = texture(samplerCube(cube, NVR_SAMPLER(samplerIndex)), lightDir).r;
    float shadow = lightDepth + BIAS * dist > dist ? 1.0 : 0.0;

    return mix(1.0, shadow, (lightDepth > 0.0 && lightDepth < 1.0) ? 1.0 : 0.0);
}

float GetPointLightAtten(vec3 lightDir, float dist, vec4 normal) {
    float s = saturate(dist * dist);
    float atten = saturate(((1.0 - s) * (1.0 - s)) / (1.0 + 5.0 * s));

    lightDir = normalize(lightDir);
    float diffuse = mix(1.0, dot(lightDir, normal.xyz), smoothstep(0.0, 0.2, dist));

    return saturate(diffuse * atten);
}

float GetPointLightAmount(textureCube cube, vec4 worldPos, vec4 lightPos, vec4 normal) {
    if (lightPos.w == 0.0) return 0.0;

    vec3 lightDir = lightPos.xyz - worldPos.xyz;
    vec3 lightUV = lightDir * vec3(-1.0, -1.0, 1.0);

    float dist = length(lightDir) / lightPos.w;

    float lightAmount = GetPointLightAmountValue(cube, pc.samplerIndex, lightUV, dist) * GetPointLightAtten(lightDir, dist, normal);
    return saturate(lightAmount);
}

float GetPointLightContribution(vec4 worldPos, vec4 lightPos, vec4 normal) {
    vec3 lightDir = lightPos.xyz - worldPos.xyz;
    float dist = length(lightDir) / lightPos.w;
    return GetPointLightAtten(lightDir, dist, normal);
}

void main() {
    float depth = readDepth(uv);
    vec3 cameraVector = toWorld(uv) * depth;
    vec4 worldPos = vec4(cb.cameraPosition.xyz + cameraVector, 1.0);
    vec4 normal = vec4(getWorldNormal(uv), 1.0);

    float shadow = texture(sampler2D(t_pointShadow, NVR_SAMPLER(pc.samplerIndex)), uv).r;
    shadow += GetPointLightAmount(t_cube6, worldPos, cb.shadowLightPosition[6], normal) * luma(cb.lightColor[6].rgb) * cb.lightColor[6].w;
    shadow += GetPointLightAmount(t_cube7, worldPos, cb.shadowLightPosition[7], normal) * luma(cb.lightColor[7].rgb) * cb.lightColor[7].w;
    shadow += GetPointLightAmount(t_cube8, worldPos, cb.shadowLightPosition[8], normal) * luma(cb.lightColor[8].rgb) * cb.lightColor[8].w;
    shadow += GetPointLightAmount(t_cube9, worldPos, cb.shadowLightPosition[9], normal) * luma(cb.lightColor[9].rgb) * cb.lightColor[9].w;
    shadow += GetPointLightAmount(t_cube10, worldPos, cb.shadowLightPosition[10], normal) * luma(cb.lightColor[10].rgb) * cb.lightColor[10].w;
    shadow += GetPointLightContribution(worldPos, cb.shadowLightPosition[11], normal);

    shadow = saturate(shadow);
    o_color = vec4(shadow, shadow, shadow, 1.0);
}
