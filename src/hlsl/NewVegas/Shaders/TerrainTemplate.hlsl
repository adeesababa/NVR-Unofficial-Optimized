//  Template for terrain shaders for blending up to 7 textures and using up to 8 pointlights.

#if defined(__INTELLISENSE__)
    #define PS
    #define NUM_PT_LIGHTS 24
#endif

#define TERRAIN

#define REVERSED_DEPTH

#ifndef TEX_COUNT
    #define TEX_COUNT 1
#endif

#if defined(NUM_PT_LIGHTS) && (NUM_PT_LIGHTS > 24)
    #define NUM_PT_LIGHTS 24
#endif

#include "includes/Helpers.hlsl"
#ifndef LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING_ON 0
#endif
#if defined(PS) && LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING
#endif
#include "includes/Terrain.hlsl"
#include "includes/Parallax.hlsl"

// BaseMap[7] holds s0-s6 and NormalMap[7] holds s7-s13, so the atlas cannot use the s9
// default. s14/s15 are the only free sampler slots in ps_3_0 here.
#define SHADOW_ATLAS_SAMPLER_REG s14
#define SHADOW_OLD_ATLAS_SAMPLER_REG s15
#include "includes/Shadow.hlsl"

#ifndef TERRAIN_BLEND
    #define TERRAIN_BLEND 0
#endif
#if TERRAIN_BLEND && !SUN_CROSSFADE
    #define TERRAIN_BLEND_ACTIVE 1
#else
    #define TERRAIN_BLEND_ACTIVE 0
#endif
#if TEX_COUNT >= 7 && SKYLIGHTING_MODE
    #define HEIGHT_EDGE2_CONTRAST 0
#else
    #define HEIGHT_EDGE2_CONTRAST 1
#endif


struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float4 blend_0 : TEXCOORD1;
    float4 blend_1 : TEXCOORD2;
};

struct VS_OUTPUT {
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
    float3 vertex_color : TEXCOORD1;
    float4 lPosition : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 binormal : TEXCOORD4;
    float3 normal : TEXCOORD5;
    float4 projectionPosition : TEXCOORD6;
    float3 viewPosition : TEXCOORD7;
};

#ifdef VS

row_major float4x4 ModelViewProj : register(c0);

float4 FogParam : register(c14);
float3 FogColor : register(c15);
float4 EyePosition : register(c16);

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 posPS = mul(float4x4(ModelViewProj[0].xyzw, ModelViewProj[1].xyzw, ModelViewProj[2].xyzw, ModelViewProj[3].xyzw), IN.position.xyzw);

    OUT.blend_0 = IN.blend_0;
    OUT.blend_1 = IN.blend_1;
    
    OUT.sPosition.xyzw = posPS;
    OUT.uv.xy = IN.uv.xy;
    OUT.vertex_color.xyz = clamp(IN.vertex_color.rgb, 0.0f, 1.0f);
    OUT.lPosition.xyz = IN.position.xyz;
    OUT.lPosition.w = posPS.w;
    OUT.tangent.xyz = IN.tangent.xyz;
    OUT.binormal.xyz = IN.binormal.xyz;
    OUT.normal.xyz = IN.normal.xyz;
    OUT.projectionPosition.xyzw = posPS;
    OUT.viewPosition.xyz = EyePosition.xyz;

    return OUT;
};

#endif  // Vertex shader.

struct PS_INPUT
{
    float2 uv : TEXCOORD0;
    float3 vertex_color : TEXCOORD1_centroid;
#if TERRAIN_BLEND_ACTIVE
    float4 lPosition : TEXCOORD2;
#else
    float3 lPosition : TEXCOORD2_centroid;
#endif
    float3 tangent : TEXCOORD3_centroid;
    float3 binormal : TEXCOORD4_centroid;
    float3 normal : TEXCOORD5_centroid;
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 projectionPosition : TEXCOORD6_centroid;
    float3 viewPosition : TEXCOORD7_centroid;
    float4 sPosition : POSITION1;
#if TERRAIN_BLEND_ACTIVE
    float2 screenPos : VPOS;
#endif
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#ifdef PS

sampler2D BaseMap[7]:register(s0);
sampler2D NormalMap[7]:register(s7);

float4 AmbientColor : register(c1);
float4 SunColor : register(c3);
float4 SunDir : register(c18);

float4 LandSpec[2] : register(c32);
float4 LandHeight[2] : register(c34);
float4 FogParam : register(c36);
float4 FogColor : register(c37);

#ifdef NUM_PT_LIGHTS
float4 PointLightColor[NUM_PT_LIGHTS] : register(c39);
float4 PointLightPosition[NUM_PT_LIGHTS] : register(c63);
float PointLightCount : register(c88);
#endif

#if TERRAIN_BLEND_ACTIVE
float4 TESR_TerrainBlendData : register(c176);
float4 TESR_TerrainBlendExtra : register(c177);
float4 TESR_TerrainBlendShape : register(c178);
sampler2D TESR_TerrainBlendDepth : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

float terrainBlendViewDepth(float depth) {
    float2 zw = depth * float2(TESR_InvProjectionTransform[2][2], TESR_InvProjectionTransform[2][3]) +
        float2(TESR_InvProjectionTransform[3][2], TESR_InvProjectionTransform[3][3]);
    return abs(zw.x / zw.y);
}

struct TerrainBlendGap {
    float covered;
    float width;
    float tangent;
};

float3 terrainBlendObject(float2 uv, float3 landPos, float landViewDepth, float3 eye) {
    float sceneViewDepth = terrainBlendViewDepth(tex2Dlod(TESR_TerrainBlendDepth, float4(uv, 0.0f, 0.0f)).x);
    return eye + (landPos - eye) * (sceneViewDepth / max(landViewDepth, 1e-3f));
}

TerrainBlendGap terrainBlendGap(float2 screenPos, float3 landPos, float landViewDepth, float3 dPx, float3 dPy, float dZx, float dZy, float3 eye, float3 normal) {
    TerrainBlendGap gap;
    gap.width = 0.0f;
    gap.tangent = 0.0f;
    float2 texel = TESR_TerrainBlendExtra.xy;
    float2 uv = (screenPos + 0.5f) * texel;
    float3 o = terrainBlendObject(uv, landPos, landViewDepth, eye);
    gap.covered = dot(landPos - o, normal);
    float tolerance = 1.0f + landViewDepth * 0.001f;
    float blendDistance = 1.0f / TESR_TerrainBlendData.y;
    float limit = blendDistance * 4.0f * max(1.0f, TESR_TerrainBlendShape.z);
    [branch] if (TESR_TerrainBlendData.x > 0.0f && gap.covered > -tolerance && gap.covered < limit) {
        gap.covered = max(gap.covered, 0.0f);
        float3 oR = terrainBlendObject(uv + float2(texel.x, 0.0f), landPos + dPx, landViewDepth + dZx, eye);
        float3 oL = terrainBlendObject(uv - float2(texel.x, 0.0f), landPos - dPx, landViewDepth - dZx, eye);
        float3 oD = terrainBlendObject(uv + float2(0.0f, texel.y), landPos + dPy, landViewDepth + dZy, eye);
        float3 oU = terrainBlendObject(uv - float2(0.0f, texel.y), landPos - dPy, landViewDepth - dZy, eye);
        float3 eR = oR - o, eL = o - oL, eD = oD - o, eU = o - oU;
        float3 ex = length(eR) < length(eL) ? eR : eL;
        float3 ey = length(eD) < length(eU) ? eD : eU;
        float3 ex2 = (length(eR) < 2.0f * length(eL) && length(eL) < 2.0f * length(eR)) ? (length(eR) < length(eL) ? eL : eR) : ex;
        float3 ey2 = (length(eD) < 2.0f * length(eU) && length(eU) < 2.0f * length(eD)) ? (length(eD) < length(eU) ? eU : eD) : ey;
        float3 facet = normalize(cross(dPx, dPy));
        float3 n1 = normalize(cross(ex, ey)), n2 = normalize(cross(ex2, ey)), n3 = normalize(cross(ex, ey2)), n4 = normalize(cross(ex2, ey2));
        float sineFacet = min(min(length(cross(n1, facet)), length(cross(n2, facet))), min(length(cross(n3, facet)), length(cross(n4, facet))));
        float sineNormal = min(min(length(cross(n1, normal)), length(cross(n2, normal))), min(length(cross(n3, normal)), length(cross(n4, normal))));
        float sine = min(sineFacet, sineNormal);
        gap.tangent = sine / max(sqrt(saturate(1.0f - sine * sine)), 1e-3f);
        gap.width = abs(dot(dPx - ex, normal)) + abs(dot(dPy - ey, normal));
    }
    return gap;
}

float terrainBlendDepthRange(TerrainBlendGap gap, float slope) {
    float distance = (1.0f / TESR_TerrainBlendData.y) * lerp(1.0f, TESR_TerrainBlendShape.z, saturate(slope * 2.0f));
    return min(distance, TESR_TerrainBlendShape.x * gap.tangent);
}

float getTerrainBlend(TerrainBlendGap gap, float landViewDepth, float slope) {
    float maxDistance = TESR_TerrainBlendData.z;
    if (landViewDepth > maxDistance || gap.covered < 0.0f) return -1.0f;
    float keep;
    [branch] if (TESR_TerrainBlendShape.y < -9.5f) {
        const float slopeFactor = lerp(1.0f, TESR_TerrainBlendShape.z, saturate(slope * 2.0f));
        const float range = max((1.0f / TESR_TerrainBlendData.y) * slopeFactor, 0.05f);
        keep = saturate(gap.covered / max(range * 0.35f, 0.0001f));
    }
    else {
        float distance = terrainBlendDepthRange(gap, slope);
        if (distance < 0.05f) return -1.0f;
        float range = clamp(gap.width * TESR_TerrainBlendShape.w, distance, distance * 4.0f);
        keep = saturate(gap.covered / range);
    }
    return lerp(keep, 1.0f, saturate((landViewDepth / maxDistance - 0.75f) * 4.0f));
}

float terrainBlendEdge(float keep, float height) {
    if (keep >= 1.0f || keep < 0.0f) return keep;
    [branch] if (TESR_TerrainBlendShape.y < 0.0f) {
        const bool edge2 = TESR_TerrainBlendShape.y < -9.5f;
        const float strength = edge2 ? -TESR_TerrainBlendShape.y - 10.0f : -TESR_TerrainBlendShape.y;
        if (strength <= 0.0f) return keep;
#if HEIGHT_EDGE2_CONTRAST
        if (edge2) height = saturate((height - 0.5f) * 8.0f + 0.5f + 0.20f);
#endif
        const float coveragePower = 1.0f + strength, heightPower = 4.0f * strength;
        const float terrainWeight = pow(keep, coveragePower) * (0.001f + pow(saturate(height), heightPower));
        const float meshWeight = pow(1.0f - keep, coveragePower) * (0.001f + pow(0.5f, heightPower));
        return terrainWeight / max(terrainWeight + meshWeight, 0.000001f);
    }
    return saturate(keep + (height - 0.5f) * TESR_TerrainBlendShape.y * 4.0f * keep * (1.0f - keep));
}

float terrainBlendHeight(float2 uv, float2 dx, float2 dy, int texCount, sampler2D tex[7], float weights[7], float heightStatus[7]) {
    float height = 0.0f;
    [unroll] for (int i = 0; i < texCount; i++)
        height += (heightStatus[i] > 0.0f ? tex2Dgrad(tex[i], uv, dx, dy).a : 0.5f) * weights[i];
    return height;
}

float terrainBlendNoise(float2 screenPos) {
    float2 p = screenPos + 5.588238f * TESR_TerrainBlendExtra.z;
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}
#endif


PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

#if TERRAIN_BLEND_ACTIVE
    float terrainKeep = -1.0f;
    float3 blendLandPos = IN.lPosition.xyz;
    float blendViewDepth = IN.lPosition.w;
    float3 blendNormal = normalize(IN.normal.xyz);
    TerrainBlendGap blendGap = terrainBlendGap(IN.screenPos, blendLandPos, blendViewDepth, ddx(blendLandPos), ddy(blendLandPos),
        ddx(blendViewDepth), ddy(blendViewDepth), IN.viewPosition.xyz, blendNormal);
    [branch] if (TESR_TerrainBlendData.x > 0.0f) {
        terrainKeep = getTerrainBlend(blendGap, blendViewDepth, 1.0f - blendNormal.z);
        if (TESR_TerrainBlendData.w <= 0.0f && TESR_TerrainBlendExtra.w > 0.5f && terrainKeep >= 0.0f && terrainKeep < 0.004f)
            clip(-1.0f);
    }
#endif
    
    int texCount = TEX_COUNT;  // Macro.
    float3 tangent = normalize(IN.tangent.xyz);
    float3 binormal = normalize(IN.binormal.xyz);
    float3 normal = normalize(IN.normal.xyz);
    float3x3 tbn = float3x3(tangent, binormal, normal);
    float3 eyeDir = normalize(mul(tbn, IN.viewPosition.xyz - IN.lPosition.xyz));
    float dist = length(IN.viewPosition.xyz - IN.lPosition.xyz);

    float2 dx, dy;
    dx = ddx(IN.uv.xy);
    dy = ddy(IN.uv.xy);
    
    float weights[7] = { 0, 0, 0, 0, 0, 0, 0 };
    float blends[7] = { IN.blend_0.x, IN.blend_0.y, IN.blend_0.z, IN.blend_0.w, IN.blend_1.x, IN.blend_1.y, IN.blend_1.z };
    float spec[7] = { LandSpec[0].x, LandSpec[0].y, LandSpec[0].z, LandSpec[0].w, LandSpec[1].x, LandSpec[1].y, LandSpec[1].z };
    float heightStatus[7] = { LandHeight[0].x, LandHeight[0].y, LandHeight[0].z, LandHeight[0].w, LandHeight[1].x, LandHeight[1].y, LandHeight[1].z };

    // No extra interpolator needed: projectionPosition is already the clip-space position,
    // and clip position is affine in object space, so it interpolates exactly.
    // ddx/ddy must stay at top level, outside dynamic flow control.
    float3 shadowWorldPos = GetShadowWorldPos(IN.projectionPosition);
    float3 shadowNormal = GetShadowGeometricNormal(shadowWorldPos);

    bool parallax = shadowWorldPos.z >= TESR_TerrainParallaxExtraData.w;
    float2 offsetUV = IN.uv.xy;
    [branch] if (parallax) offsetUV = getParallaxCoords(dist, IN.uv.xy, dx, dy, eyeDir, texCount, BaseMap, blends, heightStatus, weights);
    else weights = blends;

    float gloss = 0.0f;
    float specExponent = 0.0f;
    float2 odx = ddx(offsetUV), ody = ddy(offsetUV);
    float3 landVertexColor = lerp(IN.vertex_color, 1.0f, TESR_LODBlendData.z);
#if TERRAIN_VARIATION
    StochasticOffsets stochastic = ComputeStochasticOffsets(IN.uv.xy, TESR_TerrainVariationData.x);
    stochastic.tap1Weight = lerp(1.0f, stochastic.tap1Weight, saturate((dist - TESR_TerrainVariationData.z) / max(TESR_TerrainVariationData.w, 1.0f)));
    float tapWeights[7] = { 1, 1, 1, 1, 1, 1, 1 };
    float3 baseColor = LinColor(blendDiffuseMapsVaried(landVertexColor, offsetUV, odx, ody, texCount, BaseMap, weights, stochastic, tapWeights));
#else
    float3 baseColor = LinColor(blendDiffuseMaps(landVertexColor, offsetUV, odx, ody, texCount, BaseMap, weights));
#endif
#if TERRAIN_BLEND_ACTIVE
    [branch] if (terrainKeep >= 0.0f && terrainKeep < 1.0f && TESR_TerrainBlendShape.y != 0.0f)
        terrainKeep = terrainBlendEdge(terrainKeep, terrainBlendHeight(offsetUV, dx, dy, texCount, BaseMap, weights, heightStatus));
    if (TESR_TerrainBlendData.w <= 0.0f && TESR_TerrainBlendExtra.w <= 0.5f && terrainKeep >= 0.0f && terrainKeep < 1.0f)
        clip(terrainKeep - terrainBlendNoise(IN.screenPos));
#endif
#if TERRAIN_VARIATION
    StochasticOffsets stochasticNormals = stochastic;
    stochasticNormals.tap1Weight = lerp(1.0f, stochastic.tap1Weight, saturate(TESR_TerrainVariationExtra.x));
    float3 combinedNormal = blendNormalMapsVaried(offsetUV, odx, ody, texCount, NormalMap, weights, spec, stochasticNormals, tapWeights, gloss, specExponent);
#else
    float3 combinedNormal = blendNormalMaps(offsetUV, odx, ody, texCount, NormalMap, weights, spec, gloss, specExponent);
#endif

    float3 lightTS = mul(tbn, SunDir.xyz);
    float parallaxShadowMultiplier = 1.0;
    [branch] if (parallax) parallaxShadowMultiplier = getParallaxShadowMultipler(dist, offsetUV, dx, dy, lightTS, texCount, blends, heightStatus, BaseMap);

    // Forward sun shadows. Folded into parallaxShadowMultiplier, which getSunLighting
    // applies to the sun colour only -- ambient is added afterwards and stays untouched.
    #if FORWARD_SHADOWS
    parallaxShadowMultiplier *= GetSunShadow(shadowWorldPos, shadowNormal);
    #endif

    float3 lighting = getSunLighting(lightTS, LinColor(SunColor.rgb), eyeDir, combinedNormal, LinColor(AmbientColor.rgb), baseColor, gloss, specExponent, 1.0, parallaxShadowMultiplier, shadowNormal);

    #if defined(NUM_PT_LIGHTS)
        [loop] for (int i = 0; i < PointLightCount; i++) {
            float3 pointlightDir = PointLightPosition[i].xyz - IN.lPosition.xyz;
            float att = vanillaAtt(pointlightDir, PointLightPosition[i].w);
        
            [branch]
            if (att > 0.001) {
                pointlightDir = mul(tbn, pointlightDir);
                lighting += getPointLightLighting(pointlightDir, LampAttOther(att), LinColor(PointLightColor[i].rgb), eyeDir, combinedNormal, baseColor, gloss, specExponent);
            }
        }
    #endif
    
    // Per pixel fog.
    float4 fog;
    float3 fogPos = IN.projectionPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = IN.projectionPosition.w - IN.projectionPosition.z;
    #endif
    float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
    fog.rgb = FogColor.rgb;
    fog.a = pow(fogStrength, FogParam.z);
    
    float3 finalColor = LinEncode(lighting);
    finalColor = lerp(finalColor, fog.rgb, fog.a); // Apply fog.

#if TERRAIN_BLEND_ACTIVE
    float blendDebug = TESR_TerrainBlendData.w;
    [branch] if (blendDebug > 2.5f && TESR_TerrainBlendData.x > 0.0f) {
        if (blendGap.covered < 0.0f) finalColor = float3(0.0f, 0.0f, 0.3f);
        else if (blendDebug < 3.5f) finalColor = saturate(blendGap.covered * TESR_TerrainBlendData.y * 0.25f);
        else finalColor = saturate(terrainBlendDepthRange(blendGap, 1.0f - blendNormal.z) * TESR_TerrainBlendData.y);
    }
    else if (blendDebug > 1.5f && terrainKeep >= 0.0f) finalColor = terrainKeep;
    else if (blendDebug > 0.5f && terrainKeep >= 0.0f) finalColor = lerp(float3(1.0f, 0.0f, 1.0f), finalColor, terrainKeep);
#endif

    OUT.color_0.a = 1;
#if TERRAIN_BLEND_ACTIVE
    if (TESR_TerrainBlendExtra.w > 0.5f && TESR_TerrainBlendData.w <= 0.0f && terrainKeep >= 0.0f) OUT.color_0.a = terrainKeep;
#endif
    OUT.color_0.rgb = finalColor;

    return OUT;
};

#endif  // Pixel shader.
