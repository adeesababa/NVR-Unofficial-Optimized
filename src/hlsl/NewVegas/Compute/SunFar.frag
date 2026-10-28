#version 450
#extension GL_GOOGLE_include_directive : enable
#include "Common.glsl"

#define LOD(t, s, coord, lod) textureLod(sampler2D(t, NVR_SAMPLER(s)), (coord), (lod))

layout(push_constant) uniform Push {
    uint samplerPoint;
    uint samplerLinear;
    uint samplerMip;
    uint pad;
} pc;

layout(set = 1, binding = 0) uniform texture2D t_depth;
layout(set = 1, binding = 1) uniform texture2D t_normals;
layout(set = 1, binding = 2) uniform texture2D t_atlas;
layout(set = 1, binding = 3) uniform texture2D t_terrainShadow;
layout(set = 1, binding = 4) uniform texture2D t_terrainGround;
layout(set = 1, binding = 5) uniform texture2D t_layer0;
layout(set = 1, binding = 6) uniform texture2D t_layer1;
layout(set = 1, binding = 7) uniform texture2D t_layer2;
layout(set = 1, binding = 8) uniform texture2D t_layer3;
layout(set = 1, binding = 9) uniform texture2D t_blend0;
layout(set = 1, binding = 10) uniform texture2D t_blend1;
layout(set = 1, binding = 11) uniform texture2D t_blend2;
layout(set = 1, binding = 12) uniform texture2D t_blend3;
layout(set = 1, binding = 13) uniform texture2D t_volume;
layout(set = 1, binding = 14) uniform texture2D t_water;
layout(set = 1, binding = 15) uniform texture2D t_scene;

layout(set = 2, binding = 0, std140) uniform Constants {
    vec4 waterSettings;
    vec4 shadowFade;
    vec4 shadowSunLight;
    vec4 shadowAmbientLight;
    vec4 pbrData;
    vec4 pbrExtraData;
    vec4 terrainData;
    vec4 terrainSkyData;
    vec4 skyIrradiance[9];
    vec4 terrainShadowData;
    vec4 terrainShadowRange;
    vec4 terrainShadowParams;
    vec4 sunFarData;
    vec4 sunFarClouds;
    vec4 sunFarCloudOffset[2];
    vec4 sunFarCloudWeight;
    vec4 sunFarCloudBlend;
    vec4 sunFarVolume;
    vec4 sunDirection;
    vec4 sunFarWater;
    vec4 fogData;
    vec4 fogColor;
    vec4 sunFarLand;
    vec4 cameraData;
    vec4 cameraPosition;
    vec4 smoothedSunDir;
    vec4 shadowFormatData;
    vec4 shadowBlur;
    vec4 shadowNearCenter;
    vec4 shadowMiddleCenter;
    vec4 shadowFarCenter;
    vec4 shadowLodCenter;
    mat4 shadowToLightNear;
    mat4 shadowToLightMiddle;
    mat4 shadowToLightFar;
    mat4 shadowToLightLod;
    mat4 projectionTransform;
    mat4 viewTransform;
} cb;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 o_color;

#define farZ (cb.cameraData.y)

float readDepthLod(vec2 coord) {
    return LOD(t_depth, pc.samplerPoint, coord, 0.0).x * farZ;
}

vec3 toWorld(vec2 tex) {
    vec3 v = vec3(cb.viewTransform[0][2], cb.viewTransform[1][2], cb.viewTransform[2][2]);
    v += (1.0 / cb.projectionTransform[0][0] * (2.0 * tex.x - 1.0)) * vec3(cb.viewTransform[0][0], cb.viewTransform[1][0], cb.viewTransform[2][0]);
    v += (-1.0 / cb.projectionTransform[1][1] * (2.0 * tex.y - 1.0)) * vec3(cb.viewTransform[0][1], cb.viewTransform[1][1], cb.viewTransform[2][1]);
    return v;
}

vec3 GetWorldNormalLod(vec2 coord) {
    vec4 normal = vec4(LOD(t_normals, pc.samplerPoint, coord, 0.0).xyz * 2.0 - 1.0, 1.0);
    return (normal * cb.viewTransform).xyz;
}

float Linstep(float a, float b, float v) {
    return saturate((v - a) / (b - a));
}

float ReduceLightBleeding(float pMax, float amount) {
    return Linstep(amount, 1.0, pMax);
}

float ChebyshevUpperBound(vec2 moments, float mean, float minVariance, float lightBleedingReduction) {
    float variance = moments.y - (moments.x * moments.x);
    variance = max(variance, minVariance);

    float d = mean - moments.x;
    float pMax = variance / (variance + (d * d));

    pMax = ReduceLightBleeding(pMax, lightBleedingReduction);

    return (mean <= moments.x ? 1.0 : pMax);
}

float ShadowPCF2x2(vec2 uvc, float depth, float texel) {
    vec2 p = uvc / texel - 0.5;
    vec2 base = floor(p);
    vec2 f = p - base;
    vec2 uv0 = (base + 0.5) * texel;
    vec4 d = vec4(LOD(t_atlas, pc.samplerLinear, uv0, 0.0).x,
                  LOD(t_atlas, pc.samplerLinear, uv0 + vec2(texel, 0.0), 0.0).x,
                  LOD(t_atlas, pc.samplerLinear, uv0 + vec2(0.0, texel), 0.0).x,
                  LOD(t_atlas, pc.samplerLinear, uv0 + vec2(texel), 0.0).x);
    vec4 lit = vec4(depth <= d.x ? 1.0 : 0.0, depth <= d.y ? 1.0 : 0.0, depth <= d.z ? 1.0 : 0.0, depth <= d.w ? 1.0 : 0.0);
    return mix(mix(lit.x, lit.y, f.x), mix(lit.z, lit.w, f.x), f.y);
}

float GetLightAmountValueVSM(vec2 moments, float depth, float bias, float bleedReduction) {
    return ChebyshevUpperBound(moments, depth, bias, bleedReduction);
}

vec2 GetEVSMExponents(float positiveExponent, float negativeExponent, float formatBits) {
    float maxExponent = formatBits == 0.0 ? 5.54 : 42.0;
    vec2 lightSpaceExponents = vec2(positiveExponent, negativeExponent);
    return min(lightSpaceExponents, vec2(maxExponent));
}

vec2 WarpDepth(float depth, vec2 exponents) {
    depth = 2.0 * depth - 1.0;
    float pos = exp(exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return vec2(pos, neg);
}

float GetLightAmountValueEVSM2(vec2 moments, float depth, float bias, float bleedReduction, float formatBits) {
    vec2 exponents = GetEVSMExponents(40.0, 5.0, formatBits);
    vec2 warpedDepth = WarpDepth(depth, exponents);

    vec2 depthScale = bias * exponents * warpedDepth;
    vec2 minVariance = depthScale * depthScale;

    return ChebyshevUpperBound(moments, warpedDepth.x, minVariance.x, bleedReduction);
}

float GetLightAmountValueEVSM4(vec4 moments, float depth, float bias, float bleedReduction, float formatBits) {
    vec2 exponents = GetEVSMExponents(40.0, 5.0, formatBits);
    vec2 warpedDepth = WarpDepth(depth, exponents);

    vec2 depthScale = bias * exponents * warpedDepth;
    vec2 minVariance = depthScale * depthScale;

    float posContrib = ChebyshevUpperBound(vec2(moments.x, moments.z), warpedDepth.x, minVariance.x, bleedReduction);
    float negContrib = ChebyshevUpperBound(vec2(moments.y, moments.w), warpedDepth.y, minVariance.y, bleedReduction);

    return min(posContrib, negContrib);
}

vec4 SunCascadeTexCoord(vec4 coord) {
    coord.xyz /= coord.w;
    coord.x = coord.x * 0.5 + 0.5;
    coord.y = coord.y * -0.5 + 0.5;
    return coord;
}

float SunCascadeValue(mat4 lightTransform, vec4 coord, float offsetX, float offsetY, float bias, float bleedReduction) {
    vec4 lightSpace = SunCascadeTexCoord(lightTransform * coord);
    lightSpace.xy = lightSpace.xy * 0.5 + vec2(offsetX, offsetY);
    if (cb.shadowFormatData.x == 3.0) return ShadowPCF2x2(lightSpace.xy, lightSpace.z - 0.0002, cb.shadowBlur.x);
    vec4 moments = LOD(t_atlas, pc.samplerLinear, lightSpace.xy, 0.0);
    float shadow;
    if (cb.shadowFormatData.x == 0.0)
        shadow = GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleedReduction);
    else if (cb.shadowFormatData.x == 1.0)
        shadow = GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleedReduction, cb.shadowFormatData.y);
    else
        shadow = GetLightAmountValueEVSM4(moments, lightSpace.z, bias, bleedReduction, cb.shadowFormatData.y);
    return shadow;
}


float CascadeLight(vec3 positionWS, vec3 normal) {
    vec4 radii = vec4(cb.shadowNearCenter.w, cb.shadowMiddleCenter.w, cb.shadowFarCenter.w, cb.shadowLodCenter.w);
    vec4 distances = vec4(length(positionWS - cb.shadowNearCenter.xyz), length(positionWS - cb.shadowMiddleCenter.xyz),
                          length(positionWS - cb.shadowFarCenter.xyz), length(positionWS - cb.shadowLodCenter.xyz));
    float c = distances.x < radii.x ? 0.0 : distances.y < radii.y ? 1.0 : distances.z < radii.z ? 2.0 : distances.w < radii.w ? 3.0 : 4.0;
    if (c > 3.5) return 1.0;
    mat4 lightTransform = c < 0.5 ? cb.shadowToLightNear : c < 1.5 ? cb.shadowToLightMiddle :
                          c < 2.5 ? cb.shadowToLightFar : cb.shadowToLightLod;
    vec2 quadrant = c < 0.5 ? vec2(0.0, 0.0) : c < 1.5 ? vec2(0.5, 0.0) : c < 2.5 ? vec2(0.0, 0.5) : vec2(0.5, 0.5);
    float radius = c < 0.5 ? radii.x : c < 1.5 ? radii.y : c < 2.5 ? radii.z : radii.w;
    float bleed = c < 0.5 ? 0.1 : c < 1.5 ? 0.2 : c < 2.5 ? 0.6 : 0.8;
    float offsetScale = saturate(1.0 - dot(normal, cb.smoothedSunDir.xyz));
    float offset = offsetScale * 2.5 * 4.0 * radius * max(cb.shadowBlur.x, 1.0 / 16384.0);
    float bias = (cb.shadowFormatData.x == 0.0 ? 0.00001 : 0.01) * (1.0 + offsetScale);
    return SunCascadeValue(lightTransform, vec4(positionWS + offset * normal, 1.0), quadrant.x, quadrant.y, bias, bleed);
}

vec3 LightColor(vec3 c) { return pow(max(c, vec3(1e-6)), vec3(cb.sunFarData.z)); }
float LightScale(float s) { return pow(max(s, 1e-6), cb.sunFarData.z); }

vec3 SkyIrradiance(vec3 n) {
    vec3 irradiance = cb.skyIrradiance[0].rgb
        + cb.skyIrradiance[1].rgb * n.y
        + cb.skyIrradiance[2].rgb * n.z
        + cb.skyIrradiance[3].rgb * n.x
        + cb.skyIrradiance[4].rgb * (n.x * n.y)
        + cb.skyIrradiance[5].rgb * (n.y * n.z)
        + cb.skyIrradiance[6].rgb * (3.0 * n.z * n.z - 1.0)
        + cb.skyIrradiance[7].rgb * (n.x * n.z)
        + cb.skyIrradiance[8].rgb * (n.x * n.x - n.y * n.y);
    return sqrt(max(irradiance, vec3(0.0)));
}

float TerrainShadowCoverage(vec3 worldPos) {
    if (cb.terrainShadowData.w <= 0.0) return 0.0;
    vec2 uvh = vec2((worldPos.x - cb.terrainShadowData.x) * cb.terrainShadowData.z, (cb.terrainShadowData.y - worldPos.y) * cb.terrainShadowParams.x);
    return all(greaterThanEqual(uvh, vec2(0.0))) && all(lessThanEqual(uvh, vec2(1.0))) ? 1.0 : 0.0;
}

float TerrainShadow(vec3 worldPos) {
    if (cb.terrainShadowData.w <= 0.0) return 1.0;
    vec2 uvh = vec2((worldPos.x - cb.terrainShadowData.x) * cb.terrainShadowData.z, (cb.terrainShadowData.y - worldPos.y) * cb.terrainShadowParams.x);
    vec2 edge = min(uvh, 1.0 - uvh);
    float inside = saturate(min(edge.x, edge.y) * 20.0);
    if (inside <= 0.0) return 1.0;
    vec3 encoded = vec3(LOD(t_terrainShadow, pc.samplerLinear, uvh, 0.0).rg, LOD(t_terrainGround, pc.samplerLinear, uvh, 0.0).r) * cb.terrainShadowRange.y + cb.terrainShadowRange.x;
    vec2 heights = encoded.xy - cb.terrainShadowRange.z;
    float z = max(worldPos.z, encoded.z);
    float lower = heights.y - cb.terrainShadowRange.w;
    float transition = heights.x + cb.terrainShadowRange.w - lower;
    float lit = transition <= 0.0 ? (z >= heights.x ? 1.0 : 0.0) : saturate((z - lower) / transition);
    lit = saturate((lit - 0.5) * cb.terrainShadowParams.w + 0.5);
    return mix(1.0, lit, cb.terrainShadowData.w * inside);
}

float CloudCover(texture2D layer, texture2D incoming, vec2 uvc, float weight, float blend) {
    float a = LOD(layer, pc.samplerMip, uvc, cb.sunFarClouds.z).a;
    a = mix(a, LOD(incoming, pc.samplerMip, uvc, cb.sunFarClouds.z).a, blend);
    return saturate(a * weight);
}

float CloudShadow(vec3 rel) {
    if (cb.sunFarData.x <= 0.0) return 1.0;
    vec3 light = cb.smoothedSunDir.xyz;
    if (light.z <= 0.02) return 1.0;
    vec2 meet = rel.xy + light.xy * ((cb.sunFarClouds.x - rel.z) / light.z);
    vec2 world = meet + cb.cameraPosition.xy;
    float s = sin(cb.sunFarClouds.w);
    float c = cos(cb.sunFarClouds.w);
    vec2 uvc = vec2(world.x * c - world.y * s, world.x * s + world.y * c) * cb.sunFarClouds.y;
    float clear = (1.0 - CloudCover(t_layer0, t_blend0, uvc + cb.sunFarCloudOffset[0].xy, cb.sunFarCloudWeight.x, cb.sunFarCloudBlend.x))
                * (1.0 - CloudCover(t_layer1, t_blend1, uvc + cb.sunFarCloudOffset[0].zw, cb.sunFarCloudWeight.y, cb.sunFarCloudBlend.y))
                * (1.0 - CloudCover(t_layer2, t_blend2, uvc + cb.sunFarCloudOffset[1].xy, cb.sunFarCloudWeight.z, cb.sunFarCloudBlend.z))
                * (1.0 - CloudCover(t_layer3, t_blend3, uvc + cb.sunFarCloudOffset[1].zw, cb.sunFarCloudWeight.w, cb.sunFarCloudBlend.w));
    return saturate(1.0 - (1.0 - clear) * cb.sunFarData.x);
}

float VolumeCloudShadow(vec3 world) {
    if (cb.sunFarVolume.x <= 0.0) return 1.0;
    vec3 light = normalize(cb.sunDirection.xyz);
    light.z = max(light.z, 0.25);
    light = normalize(light);
    vec3 p = world / 70.0;
    float texel = cb.sunFarVolume.y / cb.sunFarVolume.z;
    vec2 centre = floor(cb.cameraPosition.xy / 70.0 / texel) * texel;
    vec2 q = p.xy + light.xy * ((cb.sunFarVolume.w - p.z) / light.z);
    vec2 suv = (q - centre) / cb.sunFarVolume.y + 0.5;
    vec2 edge = min(suv, 1.0 - suv);
    float inside = saturate(min(edge.x, edge.y) * 10.0);
    return mix(1.0, LOD(t_volume, pc.samplerLinear, suv, 0.0).r, inside * cb.sunFarVolume.x);
}

bool OnWater(vec3 world, vec3 normal, float depth) {
    return normal.z > 0.7 && abs(world.z - cb.waterSettings.x) < max(2.0, depth * 0.002);
}

float WaterDeep(vec2 uvc, vec3 rel) {
    float deep = saturate(LOD(t_water, pc.samplerLinear, uvc, 0.0).x);
    return mix(deep, 1.0, smoothstep(4096.0, 8192.0, length(rel.xy)));
}

vec4 SunFarPS(vec2 uvc) {
    float depth = readDepthLod(uvc);
    if (depth <= 0.0 || depth >= farZ * 0.996) return vec4(1.0);
    vec3 rel = toWorld(uvc) * depth;
    vec3 world = cb.cameraPosition.xyz + rel;

    float terrain = TerrainShadow(world);
    float cloud = CloudShadow(rel) * VolumeCloudShadow(world);
    float blocked = terrain * cloud;
    blocked = cb.sunFarData.y > 3.5 ? 0.0 : blocked;
    float shade = (1.0 - terrain) * cb.terrainShadowParams.z;

    if (cb.sunFarData.y > 0.5 && cb.sunFarData.y < 3.5) {
        vec3 tint = mix(vec3(1.0), vec3(0.25, 1.0, 0.25), (1.0 - terrain) * 0.8) * mix(vec3(1.0), vec3(0.35, 0.5, 1.0), (1.0 - cloud) * 0.8);
        if (cb.sunFarData.y > 1.5) tint *= mix(vec3(1.0), vec3(0.85, 0.9, 1.0), TerrainShadowCoverage(world));
        float water = cb.sunFarWater.z * step(2.5, cb.sunFarData.y) * (OnWater(world, GetWorldNormalLod(uvc), depth) ? 1.0 : 0.0);
        tint *= mix(vec3(1.0), mix(vec3(1.0), vec3(1.0, 0.15, 0.15), WaterDeep(uvc, rel)), water);
        return vec4(tint, 1.0);
    }
    if (blocked >= 0.999 && shade <= 0.001) return vec4(1.0);
    vec3 normal = GetWorldNormalLod(uvc);

    float keep = 1.0;
    if (OnWater(world, normal, depth)) {
        keep = cb.sunFarWater.x * mix(1.0, cb.sunFarWater.y, WaterDeep(uvc, rel));
        if (keep <= 0.0) return vec4(1.0, 1.0, 1.0, blocked);
    }

    float cascade = 1.0;
    if (cb.shadowFade.y > 0.0 && keep >= 1.0) cascade = saturate(mix(CascadeLight(rel, normal), 1.0, cb.shadowFade.x));

    float terrainWeight = (cb.terrainData.z + cb.terrainData.w > 0.0) ? smoothstep(0.5, 0.8, normal.z) : 0.0;
    vec3 scales = mix(vec3(cb.pbrData.z, cb.pbrData.w, cb.pbrExtraData.y), vec3(cb.terrainData.z, cb.terrainData.w, cb.terrainSkyData.x), terrainWeight);
    vec3 lightDir = cb.sunFarWater.w > 0.5 ? cb.smoothedSunDir.xyz : normalize(cb.sunDirection.xyz);
    vec3 landDir = cb.sunFarLand.w > 0.5 && cb.sunFarWater.w < 0.5 ? cb.sunFarLand.xyz : lightDir;
    float NdotL = mix(saturate(dot(normal, lightDir)), saturate(dot(normal, landDir)), terrainWeight);
    vec3 sunLight = LightColor(cb.shadowSunLight.rgb) * (NdotL * LightScale(scales.x));
    vec3 ambientLight = LightColor(cb.shadowAmbientLight.rgb) * LightScale(scales.y) + LightColor(SkyIrradiance(normal)) * LightScale(scales.z);
    vec3 litNow = ambientLight + sunLight * cascade;
    vec3 factor = saturate((ambientLight * (1.0 - shade) + sunLight * (cascade * blocked)) / max(litNow, vec3(0.0001)));
    factor = keep < 1.0 ? mix(vec3(1.0), factor, keep) : factor;
    factor = pow(max(factor, vec3(1e-6)), vec3(cb.sunFarData.w));
    float fogAmount = pow(1.0 - saturate((cb.fogData.y - length(rel)) / max(cb.fogData.y - cb.fogData.x, 1.0)), max(cb.fogData.w, 0.0001));
    vec3 fogShare = saturate(fogAmount * cb.fogColor.rgb / max(LOD(t_scene, pc.samplerPoint, uvc, 0.0).rgb, vec3(0.0001)));
    factor = cb.sunFarWater.w > 0.5 ? factor : factor + (1.0 - factor) * fogShare;
    return vec4(factor, blocked);
}

void main() {
    o_color = SunFarPS(uv);
}
