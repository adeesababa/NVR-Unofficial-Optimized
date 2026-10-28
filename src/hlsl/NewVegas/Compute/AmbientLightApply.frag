#version 450
#extension GL_GOOGLE_include_directive : enable
#include "AmbientLight.glsl"

layout(set = 1, binding = 0)  uniform texture2D t_source;
layout(set = 1, binding = 1)  uniform texture2D t_sunFar;
layout(set = 1, binding = 2)  uniform texture2D t_depth;
layout(set = 1, binding = 3)  uniform texture2D t_normals;
layout(set = 1, binding = 4)  uniform texture2D t_pointShadow;
layout(set = 1, binding = 5)  uniform texture2D t_skyVis;
layout(set = 1, binding = 6)  uniform texture2D t_viewModel;
layout(set = 1, binding = 7)  uniform texture2D t_occlusionHistory;
layout(set = 1, binding = 8)  uniform texture2D t_gridWave;
layout(set = 1, binding = 9)  uniform texture2D t_gridAccum;
layout(set = 1, binding = 10) uniform texture2D t_gridAccumColor;
layout(set = 1, binding = 11) uniform texture2D t_balance;
layout(set = 1, binding = 12) uniform texture2D t_buffer;
#ifdef NVR_GI_APPLY
layout(set = 1, binding = 13) uniform texture2D t_gi;
#endif

layout(location = 0) out vec4 o_color;

vec4 gridWaveLod(vec2 uv) { return (pc.variant & 2u) != 0u ? pointLod(t_gridWave, uv) : vec4(0.0); }

vec3 SunShadowApply(vec3 color, vec2 uv) {
    color *= mix(vec3(1.0), pointLod(t_sunFar, uv).rgb, cb.sunFarFlags.x);
    if (cb.waterSettings.z == 1.0) {
        vec3 worldPos = cb.cameraPosition.xyz + toWorld(uv) * readDepthLod(t_depth, uv);
        if (worldPos.z < (cb.waterSettings.x + 2.0) && worldPos.z > (cb.waterSettings.x - 2.0) && dot(GetWorldNormalLod(t_normals, uv), vec3(0.0, 0.0, -1.0)) > 0.999)
            return color;
    }
    float darkness = max(0.0, 1.0 - cb.shadowData.y);
    vec2 shadow = linearLod(t_pointShadow, uv).rg;
    shadow.r = mix(cb.shadowFade.x, 1.0, shadow.r);
    float ambient = mix(1.0, luma(cb.sunAmbient.rgb), darkness * cb.shadowFade.z);
    shadow.r = mix(0.0, ambient, shadow.r);
    shadow.r += shadow.g;
    shadow.r = saturate(mix(darkness, 1.0, shadow.r));
    vec3 linearColor = pows(color, 2.2);
    vec3 colorShadow = luma(linearColor) * shadow.r * pows(cb.skyColor.rgb, 2.2);
    colorShadow = mix(colorShadow, linearColor * shadow.r, saturate(shadow.r + 0.5));
    return pows(max(vec3(0.0), colorShadow), 1.0 / 2.2);
}

vec2 SkyFactorAt(vec2 uv, float depth) {
    if (!(depth < endFade)) return vec2(1.0);
    vec2 position = uv / cb.alLayout.zw - 0.5;
    vec2 base = floor(position);
    vec2 fraction = position - base;
    vec2 sum = vec2(0.0);
    float weights = 0.0;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            vec2 tapUV = clamp((base + vec2(float(x), float(y)) + 0.5) * cb.alLayout.zw, 0.5 * cb.alLayout.zw, 1.0 - 0.5 * cb.alLayout.zw);
            vec3 tap = pointLod(t_skyVis, tapUV).rgb;
            float weight = (x != 0 ? fraction.x : 1.0 - fraction.x) * (y != 0 ? fraction.y : 1.0 - fraction.y);
            weight /= 1.0 + abs(tap.y - depth) / max(cb.aoData.z, 0.001);
            sum += tap.xz * weight;
            weights += weight;
        }
    }
    return clamp(sum / max(weights, 1.0e-6), 0.0, 1.0);
}

vec4 Apply(vec2 uv, bool sky) {
    vec4 color = texture(sampler2D(t_source, LINEAR_S), uv);
    if (cb.alFlags.x > 0.5) color = vec4(SunShadowApply(color.rgb, uv), 1.0);
    float depth = texture(sampler2D(t_depth, POINT_S), uv).x * farZ;
    vec2 skyFactor = vec2(1.0);
    if (sky) skyFactor = SkyFactorAt(uv, depth);
    if (depth < NearFade || depth > DrawDistance || IsViewModel(t_viewModel, uv)) {
        if (sky) color.rgb = pows(SkyTake(pows(color.rgb, 2.2), skyFactor.x, skyFactor.y, cb.aoSky5.x, cb.aoSky5.w), 1.0 / 2.2);
        return DebugView > 0.5 ? vec4(0.0, 0.0, 0.0, 1.0) : color;
    }

    vec2 position = uv / cb.alLayout.zw - 0.5;
    vec2 base = floor(position);
    vec2 fraction = position - base;
    float tolerance = max(depth * 0.03, 2.0);
    vec4 sum = vec4(0.0);
    float weights = 1.0e-5;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            vec2 tapUV = (base + vec2(float(x), float(y)) + 0.5) * cb.alLayout.zw;
            float tapDepth = readDepthLod(t_depth, tapUV);
            float weight = (x != 0 ? fraction.x : 1.0 - fraction.x) * (y != 0 ? fraction.y : 1.0 - fraction.y);
            weight /= 1.0 + abs(tapDepth - depth) / tolerance;
            sum.a += pointLod(t_occlusionHistory, tapUV).r * weight;
#ifdef NVR_GI_APPLY
            sum.rgb += pointLod(t_gi, tapUV).rgb * weight;
#endif
            weights += weight;
        }
    }
    vec4 ambient = sum / weights;

    vec3 lin = linearize(color.rgb);
    float ao = mix(DarkestAO, 1.0, ambient.a);
    ao = mix(ao, 1.0, saturate((luma(lin) - BrightFade) * 3.0));
    vec2 px = cb.reciprocalResolution.xy;
    float left = readDepthLod(t_depth, uv - vec2(px.x, 0.0)), right = readDepthLod(t_depth, uv + vec2(px.x, 0.0));
    float up = readDepthLod(t_depth, uv - vec2(0.0, px.y)), down = readDepthLod(t_depth, uv + vec2(0.0, px.y));
    float bendX = abs(left + right - 2.0 * depth);
    float bendY = abs(up + down - 2.0 * depth);
    if (cb.alFlags.y > 0.5) {
        float left2 = readDepthLod(t_depth, uv - vec2(2.0 * px.x, 0.0)), right2 = readDepthLod(t_depth, uv + vec2(2.0 * px.x, 0.0));
        float up2 = readDepthLod(t_depth, uv - vec2(0.0, 2.0 * px.y)), down2 = readDepthLod(t_depth, uv + vec2(0.0, 2.0 * px.y));
        bendX = min(bendX, min(abs(left2 + depth - 2.0 * left), abs(depth + right2 - 2.0 * right)));
        bendY = min(bendY, min(abs(up2 + depth - 2.0 * up), abs(depth + down2 - 2.0 * down)));
    }
    float busy = saturate((bendX + bendY) / depth * 20.0 - 0.1);
#ifdef NVR_GI_APPLY
    vec3 screenBounce = ambient.rgb * (cb.data.z * SurfaceColour(lin));
    if (DebugView > 12.5) return delinearize(vec4(ambient.rgb * cb.data.z, 1.0));
#endif
    if (DebugView > 11.5) {
        vec3 c = floor((toWorld(uv) * depth - cb.gridOrigin.xyz) / cb.gridOrigin.w);
        vec4 m = gridWaveLod(CellUV(clamp(c, vec3(0.0), vec3(GridX, GridY, GridZ) - 1.0)));
        return vec4(saturate(m.xyz * cb.gridStep.y), 1.0);
    }
    if (DebugView > 10.5) return vec4(vec3(busy), 1.0);
    ao = mix(ao, mix(1.0, ao, 0.25), busy);
    vec3 albedo = SurfaceColour(lin);
    vec3 gridBounce = vec3(0.0);
    if (cb.gridData.x > 0.0) {
        vec3 cameraRelative = toWorld(uv) * depth;
        vec3 normal = normalize(GetWorldNormalLod(t_normals, uv));
        gridBounce = GridBounce(t_gridAccum, t_gridAccumColor, cameraRelative, normal) * (cb.gridData.x * ao);
    }
    if (DebugView > 8.5) return delinearize(vec4(DebugView < 9.5 ? gridBounce : albedo, 1.0));
    if (cb.alInterior.x > 0.5) gridBounce *= pointLod(t_balance, vec2(0.5, 0.5)).x;
    if (DebugView > 7.5) {
        vec3 cameraRelative = toWorld(uv) * depth;
        vec3 c = floor((cameraRelative - cb.gridOrigin.xyz) / cb.gridOrigin.w);
        vec4 m = gridWaveLod(CellUV(clamp(c, vec3(0.0), vec3(GridX, GridY, GridZ) - 1.0)));
        return vec4(saturate(m.x * 4.0), m.x != 0.0 ? 0.5 : 0.0, saturate(gridBounce.b * 4.0), 1.0);
    }
    if (DebugView > 6.5) return delinearize(vec4(gridBounce * albedo, 1.0));
    if (DebugView > 2.5) return vec4(pointLod(t_buffer, uv).rgb, 1.0);
    if (DebugView > 0.5) return delinearize(vec4(DebugView < 1.5 ? vec3(ao) : gridBounce * albedo, 1.0));
    if (cb.alInterior.x > 0.5) lin = lin - (1.0 - ao) * min(lin, albedo * cb.alScene.rgb) + gridBounce * albedo;
    else {
        vec3 lit = lin * ao;
        if (sky) lit = SkyTake(lit, skyFactor.x, skyFactor.y, cb.aoSky5.x, cb.aoSky5.w);
        lin = lit + gridBounce * albedo;
    }
#ifdef NVR_GI_APPLY
    lin += screenBounce;
#endif
    return delinearize(vec4(lin, color.a));
}

void main() { o_color = Apply(frameUV(), (pc.variant & 1u) != 0u); }
