#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

layout(set = 1, binding = 0) uniform texture2D t_rendered;
layout(set = 1, binding = 1) uniform texture2D t_sunFar;
layout(set = 1, binding = 2) uniform texture2D t_pointShadow;
layout(set = 1, binding = 3) uniform texture2D t_depth;
layout(set = 1, binding = 4) uniform texture2D t_normals;

layout(push_constant) uniform Push {
    vec4 viewColumn0;
    vec4 viewColumn1;
    vec4 viewColumn2;
    vec4 viewColumn3;
    vec4 cameraPosition;
    vec4 projectionFar;
    vec4 shadowFade;
    vec4 shadowData;
    vec4 waterSettings;
    vec4 sunFarApply;
    vec4 sunAmbient;
    vec4 skyColor;
    uint samplerIndex;
    uint pointSamplerIndex;
} pc;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

mat4 viewT() { return mat4(pc.viewColumn0, pc.viewColumn1, pc.viewColumn2, pc.viewColumn3); }

vec3 toWorld(vec2 tex) {
    vec3 v = pc.viewColumn2.xyz;
    v += (1.0 / pc.projectionFar.x * (2.0 * tex.x - 1.0)) * pc.viewColumn0.xyz;
    v += (-1.0 / pc.projectionFar.y * (2.0 * tex.y - 1.0)) * pc.viewColumn1.xyz;
    return v;
}

float readDepthLod(vec2 coord) {
    return textureLod(sampler2D(t_depth, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0).x * pc.projectionFar.z;
}

vec3 getWorldNormalLod(vec2 coord) {
    vec4 normal = vec4(textureLod(sampler2D(t_normals, NVR_SAMPLER(pc.samplerIndex)), coord, 0.0).xyz * 2.0 - 1.0, 1.0);
    return (viewT() * normal).xyz;
}

void main() {
    vec2 uvc = uv;
    vec4 color = texture(sampler2D(t_rendered, NVR_SAMPLER(pc.samplerIndex)), uvc);
    color.rgb *= mix(vec3(1.0), textureLod(sampler2D(t_sunFar, NVR_SAMPLER(pc.pointSamplerIndex)), uvc, 0.0).rgb, saturate(pc.sunFarApply.x));

    if (pc.waterSettings.z == 1.0) {
        float depth = readDepthLod(uvc);
        vec3 cameraVector = toWorld(uvc) * depth;
        vec4 worldPos = vec4(pc.cameraPosition.xyz + cameraVector, 1.0);
        vec3 worldNormal = getWorldNormalLod(uvc);
        if (worldPos.z < (pc.waterSettings.x + 2.0) && worldPos.z > (pc.waterSettings.x - 2.0) && dot(worldNormal, vec3(0.0, 0.0, -1.0)) > 0.999) {
            o_color = color;
            return;
        }
    }

    vec2 shadow = texture(sampler2D(t_pointShadow, NVR_SAMPLER(pc.samplerIndex)), uvc).rg;
    shadow.r = mix(pc.shadowFade.x, 1.0, shadow.r);

    float darkness = max(0.0, 1.0 - pc.shadowData.y);
    float ambient = mix(1.0, luma(pc.sunAmbient.rgb), darkness * pc.shadowFade.z);
    shadow.r = mix(0.0, ambient, shadow.r);
    shadow.r += shadow.g;

    shadow.r = mix(darkness, 1.0, shadow.r);
    shadow.r = saturate(shadow.r);

    color.rgb = pows(color.rgb, 2.2);
    vec4 skyColor = vec4(pows(pc.skyColor.rgb, 2.2), pc.skyColor.w);
    vec4 colorShadow = luma(color.rgb) * shadow.r * skyColor;
    colorShadow.rgb = mix(colorShadow.rgb, color.rgb * shadow.r, saturate(shadow.r + 0.5));
    colorShadow.rgb = pows(max(vec3(0.0), colorShadow.rgb), 1.0 / 2.2);
    o_color = vec4(colorShadow.rgb, 1.0);
}
