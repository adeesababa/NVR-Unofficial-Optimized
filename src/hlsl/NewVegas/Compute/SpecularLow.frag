#version 450
#extension GL_GOOGLE_include_directive : enable
#include "SpecularCommon.glsl"

layout(set = 1, binding = 0) uniform texture2D t_specularNormals;
layout(set = 1, binding = 1) uniform texture2D t_shadowAtlas;
layout(set = 1, binding = 2) uniform texture2D t_skyVis;
layout(set = 1, binding = 3) uniform texture2D t_sunFar;

layout(push_constant) uniform Push {
    uint pointSampler;
    uint linearSampler;
    uint pad[2];
} pc;

layout(location = 0) out vec4 o_color;

void main() {
    vec2 vpos = floor(gl_FragCoord.xy);
    vec2 px = cb.reciprocalResolution.xy;
    float lowScale = cb.specularEffects.z > 0.0 ? cb.specularEffects.z : 4.0;
    vec2 lowPx = px * lowScale;
    vec2 uv = (vpos + 0.5) * lowPx;
    vec4 centre = sampleLod(t_specularNormals, pc.pointSampler, uv);
    float depth = centre.a;
    if (depth >= cb.specularData.w || depth >= cb.cameraData.y * 0.98) {
        o_color = vec4(0.0, 0.0, 0.0, depth);
        return;
    }

    vec3 eyeVector = toWorld(uv) * depth;
    vec3 V = -normalize(eyeVector);

    vec3 sum = centre.xyz;
    float weight = 1.0;
    float tolerance = 0.05 * depth + 4.0;
    const vec2 ring[16] = vec2[16](vec2(1, 0), vec2(1, 1), vec2(0, 1), vec2(-1, 1), vec2(-1, 0), vec2(-1, -1), vec2(0, -1), vec2(1, -1),
                                   vec2(3, 1), vec2(1, 3), vec2(-1, 3), vec2(-3, 1), vec2(-3, -1), vec2(-1, -3), vec2(1, -3), vec2(3, -1));
    for (int k = 0; k < 16; k++) {
        vec4 s = sampleLod(t_specularNormals, pc.pointSampler, uv + ring[k] * lowPx);
        float w = abs(s.a - depth) < tolerance ? 1.0 : 0.0;
        sum += w * s.xyz;
        weight += w;
    }
    vec3 meanNormal = sum / weight;
    float agreement = saturate(length(meanNormal));
    vec3 N = normalize(viewToWorld(meanNormal));

    vec3 worldPos = cb.cameraPosition.xyz + eyeVector;
    if (N.z > 0.9 && abs(worldPos.z - cb.waterSettings.x) < (depth / cb.cameraData.y) * 200.0) {
        o_color = vec4(0.0, 0.0, 0.0, depth);
        return;
    }

    float alpha = cb.specularData.y * cb.specularData.y;
    float variance = (1.0 - agreement) / max(agreement, 0.001);
    float alpha2 = saturate(alpha * alpha + cb.specularEffects.x * 2.0 * variance);
    float roughAA = sqrt(sqrt(alpha2));

    vec3 L = normalize(cb.sunDirection.xyz);
    vec3 Hv = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = max(dot(N, V), 0.0001);
    float NdotH = saturate(dot(N, Hv));
    float VdotH = saturate(dot(V, Hv));
    float F0 = cb.specularEffects.y;
    float dn = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    float D = alpha2 / (PI_HLSL * dn * dn);
    float vis = 0.5 / (NdotL * sqrt(NdotV * NdotV * (1.0 - alpha2) + alpha2) + NdotV * sqrt(NdotL * NdotL * (1.0 - alpha2) + alpha2) + 0.00001);
    float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    float sunSpec = min(D * vis * F * NdotL * PI_HLSL, 2.0) * cb.specularData.x;
    if (sunSpec > 0.00001 && cb.shadowFade.y > 0.0) sunSpec *= sunCascadeLight(t_shadowAtlas, pc.linearSampler, eyeVector, N);

    vec3 R = reflect(-V, N);
    float skyAt = sqrt(saturate(R.z));
    float below = R.z >= 0.0 ? 1.0 : mix(0.6, 0.25, saturate(-R.z * 4.0));
    vec2 env = envBRDFApprox(roughAA, NdotV);
    float skyAmount = (F0 * env.x + env.y) * cb.specularData.z * below;
    skyAmount *= mix(1.0, mix(cb.specularExtra.y, 1.0, sampleLod(t_skyVis, pc.linearSampler, uv).b), cb.specularExtra.x);
    sunSpec *= mix(1.0, sampleLod(t_sunFar, pc.linearSampler, uv).a, cb.specularExtra.z);

    float fade = 1.0 - smoothstep(0.7 * cb.specularData.w, cb.specularData.w, depth);
    o_color = vec4(skyAmount * fade, skyAt, sunSpec * fade, depth);
}
