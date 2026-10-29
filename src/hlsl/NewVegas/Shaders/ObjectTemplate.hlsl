// Template for object lighting shaders.
//
// VS
//
// AD
// SLS2028 - LIGHTS = 2 (ONLY_LIGHT)
// SLS2029 - LIGHTS = 2, SKIN (ONLY_LIGHT)
// SLS2030 - LIGHTS = 2, PROJ_SHADOW (ONLY_LIGHT)
// SLS2031 - LIGHTS = 2, PROJ_SHADOW, SKIN (ONLY_LIGHT)
// SLS2032 - LIGHTS = 3 (ONLY_LIGHT)
// SLS2033 - LIGHTS = 3, SKIN (ONLY_LIGHT)
// SLS2034 - LIGHTS = 3, PROJ_SHADOW (ONLY_LIGHT)
// SLS2035 - LIGHTS = 3, PROJ_SHADOW, SKIN (ONLY_LIGHT)
//
// ADTS
// SLS2000 - Base
// SLS2001 - LOD
// SLS2003 - SKIN
// SLS2004 - PROJ_SHADOW
// SLS2006 - PROJ_SHADOW, SKIN
// SLS2007 - STBB
// SLS2008 - LIGHTS = 2
// SLS2009 - LIGHTS = 2, SKIN
// SLS2010 - LIGHTS = 2, PROJ_SHADOW
// SLS2011 - LIGHTS = 2, PROJ_SHADOW, SKIN
// SLS2012 - SPECULAR
// SLS2013 - SPECULAR, SKIN
// SLS2014 - SPECULAR, PROJ_SHADOW
// SLS2015 - SPECULAR, PROJ_SHADOW, SKIN
// SLS2016 - SPECULAR, LIGHTS = 2
// SLS2017 - SPECULAR, LIGHTS = 2, SKIN
// SLS2018 - SPECULAR, LIGHTS = 2, PROJ_SHADOW
// SLS2019 - SPECULAR, LIGHTS = 2, PROJ_SHADOW, SKIN
// (ignored) SLS2102 - LIGHTS = 0
//
// ADTS10
// SLS2020 - LIGHTS = 9
// SLS2021 - LIGHTS = 9, SKIN
// SLS2022 - LIGHTS = 4
// SLS2023 - LIGHTS = 4, OPT
// SLS2024 - LIGHTS = 4, SKIN
// SLS2025 - LIGHTS = 4, SPECULAR
// SLS2026 - LIGHTS = 4, SPECULAR, OPT
// SLS2027 - LIGHTS = 4, SPECULAR, SKIN
//
// DiffusePt
// SLS2036 - LIGHTS = 2 (DIFFUSE)
// SLS2037 - LIGHTS = 2, SKIN (DIFFUSE)
// SLS2038 - LIGHTS = 3 (DIFFUSE)
// SLS2039 - LIGHTS = 3, SKIN (DIFFUSE)
//
// Specular
// SLS2040 - Base (ONLY_SPECULAR)
// SLS2041 - SKIN (ONLY_SPECULAR)
// SLS2042 - PROJ_SHADOW (ONLY_SPECULAR)
// SLS2043 - PROJ_SHADOW, SKIN (ONLY_SPECULAR)
// SLS2044 - POINT (ONLY_SPECULAR)
// SLS2045 - POINT, SKIN (ONLY_SPECULAR)
// SLS2046 - POINT, NUM_PT_LIGHTS = 2 (ONLY_SPECULAR)
// SLS2047 - POINT, NUM_PT_LIGHTS = 2, SKIN (ONLY_SPECULAR)
// SLS2048 - POINT, NUM_PT_LIGHTS = 3 (ONLY_SPECULAR)
// SLS2049 - POINT, NUM_PT_LIGHTS = 3, SKIN (ONLY_SPECULAR)
//
// PS
//
// AD
// SLS2037 - LIGHTS = 2 (ONLY_LIGHT, OPT)
// SLS2038 - LIGHTS = 2, SI (ONLY_LIGHT, OPT)
// SLS2039 - LIGHTS = 2, PROJ_SHADOW (ONLY_LIGHT, OPT)
// SLS2040 - LIGHTS = 2, SI, PROJ_SHADOW (ONLY_LIGHT, OPT)
// SLS2041 - LIGHTS = 3 (ONLY_LIGHT, OPT)
// SLS2042 - LIGHTS = 3, SI (ONLY_LIGHT, OPT)
// SLS2043 - LIGHTS = 3, PROJ_SHADOW (ONLY_LIGHT, OPT)
// SLS2044 - LIGHTS = 3, SI, PROJ_SHADOW (ONLY_LIGHT, OPT)
//
// ADTS
// SLS2000 - Default
// SLS2001 - OPT
// SLS2002 - OPT, LOD
// SLS2004 - SI 
// SLS2005 - PROJ_SHADOW
// SLS2007 - SI, PROJ_SHADOW 
// SLS2008 - STBB
// SLS2009 - HAIR
// SLS2010 - HAIR, PROJ_SHADOW
// SLS2011 - LIGHTS = 2
// SLS2012 - LIGHTS = 2, SI
// SLS2013 - LIGHTS = 2, HAIR
// SLS2014 - LIGHTS = 2, PROJ_SHADOW
// SLS2015 - LIGHTS = 2, SI, PROJ_SHADOW
// SLS2016 - LIGHTS = 2, HAIR, PROJ_SHADOW
// SLS2017 - SPECULAR
// SLS2018 - SPECULAR, SI
// SLS2019 - SPECULAR, HAIR
// SLS2020 - SPECULAR, PROJ_SHADOW
// SLS2021 - SPECULAR, SI, PROJ_SHADOW
// SLS2022 - SPECULAR, HAIR, PROJ_SHADOW
// SLS2023 - SPECULAR, LIGHTS = 2
// SLS2024 - SPECULAR, LIGHTS = 2, SI
// SLS2026 - SPECULAR, LIGHTS = 2, PROJ_SHADOW
// SLS2027 - SPECULAR, LIGHTS = 2, SI, PROJ_SHADOW
// (ignored) SLS2151 - LIGHTS = 0, SILHOUETTE
//
// ADTS10
// SLS2029 - LIGHTS = 9
// SLS2030 - LIGHTS = 9, SI
// SLS2031 - LIGHTS = 4
// SLS2032 - LIGHTS = 4, OPT 
// SLS2033 - LIGHTS = 4, SI
// SLS2034 - LIGHTS = 4, SPECULAR
// SLS2035 - LIGHTS = 4, SPECULAR, OPT
// SLS2036 - LIGHTS = 4, SPECULAR, SI
//
// DiffusePt
// SLS2045 - LIGHTS = 2 (DIFFUSE)
// SLS2046 - LIGHTS = 3 (DIFFUSE)
//
// Specular
// SLS2047 - Base (ONLY_SPECULAR)
// SLS2048 - HAIR (ONLY_SPECULAR)
// SLS2049 - PROJ_SHADOW (ONLY_SPECULAR)
// SLS2050 - PROJ_SHADOW, HAIR (ONLY_SPECULAR)
// SLS2051 - POINT (ONLY_SPECULAR)
// SLS2052 - POINT, HAIR (ONLY_SPECULAR)
// SLS2053 - NUM_PT_LIGHTS = 2, POINT (ONLY_SPECULAR)
// SLS2054 - NUM_PT_LIGHTS = 2, POINT, HAIR (ONLY_SPECULAR)
// SLS2055 - NUM_PT_LIGHTS = 3, POINT (ONLY_SPECULAR)
// SLS2056 - NUM_PT_LIGHTS = 3, POINT, HAIR (ONLY_SPECULAR)

#if defined(__INTELLISENSE__)
    #define VS
    #define DIFFUSE
    #define LIGHTS 2
#endif

#if defined(DIFFUSE)
    #define ONLY_LIGHT
    #define OPT
#endif

#if defined(ONLY_SPECULAR)
    #define ONLY_LIGHT
    #define SPECULAR
#endif

#ifdef ONLY_LIGHT
    #define NO_FOG
    #define NO_VERTEX_COLOR
#else
    #define METAL_UP
    #if defined(SPECULAR) && !defined(HAIR)
        #define METAL
    #endif
#endif

#ifndef FOLD_HIGHLIGHTS_ON
    #define FOLD_HIGHLIGHTS_ON 0
#endif
#if FOLD_HIGHLIGHTS_ON && defined(ONLY_LIGHT) && !defined(ONLY_SPECULAR) && !defined(HAIR) && !defined(SI)
    #define NVR_FOLD
    #define FOLD_DECL
#else
    #define FOLD_DECL float3
#endif

#ifndef LAMP_VECTOR_ATT_ON
    #define LAMP_VECTOR_ATT_ON 0
#endif
#if LAMP_VECTOR_ATT_ON
    #define LAMP_ATT_SQ(distSq, dir) vanillaAttSq(dot((dir).xyz, (dir).xyz), (dir).w)
    #define LAMP_VS_DIR(tbn, light) LampVectorAtLength(mul(tbn, light), light)
    float3 LampVectorAtLength(float3 t, float3 light) { return t * sqrt(dot(light, light) / max(dot(t, t), 1e-12f)); }
#else
    #define LAMP_ATT_SQ(distSq, dir) vanillaAttSq(distSq, (dir).w)
    #define LAMP_VS_DIR(tbn, light) mul(tbn, light)
#endif

#include "includes/Helpers.hlsl"
#ifndef LIVE_CUBE
    #define LIVE_CUBE 0
#endif
#if defined(PS) && LIVE_CUBE && !(defined(SUN_CROSSFADE) && SUN_CROSSFADE)
    #define NVR_LIVE_CUBE
    #include "includes/LiveCube.hlsl"
#endif
#ifndef MATERIAL_RULES_ON
    #define MATERIAL_RULES_ON 0
#endif
#if defined(PS) && MATERIAL_RULES_ON
    #define MATERIAL_RULES
#endif
#ifndef RMAOS_ON
    #define RMAOS_ON 0
#endif
#if defined(PS) && RMAOS_ON
    #define RMAOS
#endif
#ifndef LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING_ON 0
#endif
#if defined(PS) && LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING
    #if defined(LIN_PREDECODE_ON) && LIN_PREDECODE_ON
        #define LIN_PREDECODED
    #endif
#endif
#ifndef LAMP_SUMS_ON
    #define LAMP_SUMS_ON 0
#endif
#if defined(PS) && defined(DIFFUSE) && LAMP_SUMS_ON && LINEAR_LIGHTING_ON
    #define NVR_LAMP_SUMS
    sampler2D NVR_LampRunStart : register(s9);
#endif
#ifndef HIGHLIGHT_SUMS_ON
    #define HIGHLIGHT_SUMS_ON 0
#endif
#if defined(PS) && defined(ONLY_SPECULAR) && HIGHLIGHT_SUMS_ON && LINEAR_LIGHTING_ON
    #define NVR_LAMP_SUMS
    sampler2D NVR_LampRunStart : register(s6);
#endif
#if defined(PS) && defined(SMITH_CORRELATED_ON) && SMITH_CORRELATED_ON
    #define PBR_SMITH_CORRELATED SMITH_CORRELATED_ON
#endif
#if defined(PS) && defined(OPT) && defined(SPECULAR) && !defined(ONLY_SPECULAR) && defined(LIGHTS) && defined(SPECULAR_LAMPS6)
    #if LIGHTS > 3 && SPECULAR_LAMPS6
        #define NVR_SLOT_TIGHT
        #define RMAOS_NO_FADE
    #endif
#endif
#if defined(PS) && (defined(SPECULAR) || defined(NVR_FOLD)) && defined(SPEC_FADE_ON) && SPEC_FADE_ON
    #define PBR_SPEC_FADE
    #ifdef NVR_SLOT_TIGHT
        #define PBR_SPEC_FADE_HIGHLIGHT_ONLY
    #endif
#endif
#include "includes/Object.hlsl"
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#if defined(PS) && defined(SHADOW_CHARACTER_OBJECTS)
    #define SHADOW_SLACK_CAP_ACTOR 1
#endif
#if defined(PS) && !defined(NVR_SLOT_TIGHT) && !(defined(LIGHTS) && LIGHTS > 3)
    #define SHADOW_CHARACTER_MAP_IN 1
#endif
#include "includes/Shadow.hlsl"
#include "includes/CapturedNormals.hlsl"
#if OBJECT_SMOOTH_NORMALS && !(defined(LIGHTS) && LIGHTS >= 4)
    #ifdef ONLY_LIGHT
        #define OBJ_SMOOTH_NORMAL_INTERP 1
    #else
        #define OBJ_SMOOTH_NORMAL_W 1
    #endif
#endif
#ifdef PS
    #define POINT_SHADOW_SCALAR
    #include "includes/PointShadowForward.hlsl"
    #include "includes/HandBlood.hlsl"
    #if defined(HUD_RAIN_ON) && HUD_RAIN_ON
        #define HUD_RAIN 1
    #endif
    #include "includes/HudRain.hlsl"

    #ifndef UNLIT_SHADOW_SKIP
        #define UNLIT_SHADOW_SKIP 0
    #endif
    bool SunShadowWanted(float3 normalTS, float3 lightTS) {
        bool wanted = dot(normalTS, lightTS) > 0.0f;
        #if HUD_RAIN
            wanted = wanted || hudRainCover > 0.0f;
        #endif
        return wanted;
    }
    #ifndef LAMP_REACH_SKIP
        #define LAMP_REACH_SKIP 0
    #endif
    #ifndef NO_SLOT_LIMIT
        #define NO_SLOT_LIMIT 0
    #endif
    #if NO_SLOT_LIMIT && LIGHTS > 3 && defined(SPECULAR) && SPECULAR_LAMPS6 && !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        #define LAMP_IF(att) [branch] if ((att) > 0.0f)
        #define LAMP_IFK(k, att) [branch] if ((att) > 0.0f && (k) < lightsUsed)
        #define LAMP_SLOT_SKIP 1
    #elif LAMP_REACH_SKIP && !defined(DIFFUSE) && !defined(ONLY_SPECULAR) && !(defined(OPT) && defined(SPECULAR) && LIGHTS > 3 && SPECULAR_LAMPS6)
        #define LAMP_IF(att) [branch] if ((att) > 0.0f)
        #define LAMP_IFK(k, att) LAMP_IF(att)
    #else
        #define LAMP_IF(att)
        #define LAMP_IFK(k, att)
    #endif
#endif

float4 BarrelHeatMuzzle : register(c171);
float4 BarrelHeatAxis : register(c172);
float4 BarrelHeatShape : register(c173);

float BarrelHeatMask(float3 localPos) {
    float3 offset = localPos - BarrelHeatMuzzle.xyz;
    float along = dot(offset, BarrelHeatAxis.xyz);
    float radial = length(offset - BarrelHeatAxis.xyz * along);
    float span = max(BarrelHeatAxis.w, 1.0f);
    float radius = max(BarrelHeatShape.x, 1.0f);
    float forward = max(BarrelHeatShape.y, 1.0f);
    float rearFade = saturate(1.0f - max(along, 0.0f) / span);
    return smoothstep(-forward, -forward * 0.15f, along)
         * rearFade
         * (1.0f - smoothstep(radius * 0.375f, radius, radial))
         * BarrelHeatMuzzle.w;
}

#ifdef PS
#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
float3 ApplyBarrelHeat(float3 color, float mask, float3 materialFactor) {
#else
float3 ApplyBarrelHeat(float3 color, float mask) {
#endif
    if (BarrelHeatMuzzle.w <= 0.0f) return color;
    float heat = saturate(mask);
    float orange = smoothstep(0.5f, 1.0f, heat);
    float3 ember = lerp(float3(0.68f, 0.055f, 0.015f),
                        float3(1.25f, 0.23f, 0.035f), orange);
#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    return lerp(color, ember / materialFactor, heat * heat * 0.75f);
#else
    return lerp(color, ember, heat * heat * 0.75f);
#endif
}
#endif

// Forward sun shadows. Enabled at COMPILE TIME via FORWARD_SHADOWS in Includes/Shadow.hlsl,
// deliberately not via a runtime constant -- see the note there.

#ifdef SKIN
    #include "includes/SkinHelpers.hlsl"
#endif

#ifndef CLOTH_WIND_ON
    #define CLOTH_WIND_ON 0
#endif
#if defined(VS) && CLOTH_WIND_ON
    #define CLOTH_WIND
    #include "includes/ClothWind.hlsl"
#endif
#ifndef PLANT_WIND_ON
    #define PLANT_WIND_ON 0
#endif
#if defined(VS) && PLANT_WIND_ON
    #define PLANT_WIND
    #include "includes/PlantWind.hlsl"
#endif

// Toggles.
#define useVertexColor Toggles.x
#define useFog Toggles.y
#define glossPower Toggles.z
#define alphaTestRef Toggles.w

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
#if !defined(NO_VERTEX_COLOR) || defined(CLOTH_WIND) || defined(NVR_FOLD)
    float4 vertexColor : COLOR0;
#endif
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
#if defined(CLOTH_WIND)
    float3 clothWeight : TEXCOORD7;
#endif
};

#if defined(VS) && LIGHTS < 4

struct VS_OUTPUT {
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#if defined(NVR_FOLD) && defined(NO_VERTEX_COLOR)
    float4 foldColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 sPosition : POSITION;
    float4 uv : TEXCOORD0;
    float4 lightDir : TEXCOORD1;

#if LIGHTS > 1 || NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD2;
#endif
    
#if LIGHTS > 2 || NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD3;
#endif
    
    float4 viewDir : TEXCOORD6;

    // Object-space squared distances for point-light attenuation (vanillaAttSq), bypassing
    // lightDir/light2Dir/light3Dir above -- those are tangent-space (TBN-transformed) and their
    // length is only correct if the TBN basis is orthonormal. .x = light0 (DIFFUSE/POINT only,
    // where light0 is itself a point light rather than the sun), .y = light2, .z = light3.
    float4 lightDistSq : TEXCOORD5;

    // TEXCOORD4 is free at LIGHTS < 4. .w carries SHADOW_VS_SENTINEL.
    float4 shadowWorldPos : TEXCOORD4;

#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef OBJ_SMOOTH_NORMAL_INTERP
    float4 smoothNormal : TEXCOORD8;
#endif
};

#ifndef NO_FOG
    float3 FogColor : register(c15);
    float4 FogParam : register(c14);
#endif

float4 LightData[10] : register(c25);

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
#endif

float4 EyePosition : register(c16);

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif


VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    // Zeroed unconditionally: the DIFFUSE/POINT (.x) and LIGHTS>1/LIGHTS>2 (.y/.z) blocks below
    // only overwrite the components they actually use, and vs_3_0 requires every component of
    // OUT to be written before return -- a component that stays at this default is simply never
    // read by the PS either (same macro guards on both sides).
    OUT.lightDistSq = 0;

    OUT.uv.xy = IN.uv.xy;
    
    float4 position = IN.position.xyzw;
    
    #ifndef SKIN
        float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);
        #ifdef CLOTH_WIND
            ClothWindTBN(position.xyz, tbn, IN.vertexColor, IN.position.xyz, IN.clothWeight.x, float3(IN.clothWeight.y, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f);
        #endif
        #ifdef PLANT_WIND
            [branch] if (PlantA.x < -0.5f) position.xyz += PlantWindMove(position.xyz);
        #endif
    
        OUT.sPosition.xyzw = mul(ModelViewProj, position.xyzw);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
    
        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
        #ifdef CLOTH_WIND
            float3 clothOut = 0.0f;
            float4 clothLegL = 0.0f, clothLegR = 0.0f;
            float3 clothUp = 0.0f;
            [branch] if (ClothIsMask5()) {
                [branch] if (ClothWalk.x > 0.5f) {
                    const float hw = saturate(IN.clothWeight.x) * ClothWalk.y;
                    position.xyz = lerp(position.xyz, ClothPelvisPoint(IN.position.xyz), hw);
                    tbn = float3x3(normalize(lerp(tbn[0], normalize(ClothPelvisVector(IN.tangent)), hw)),
                                   normalize(lerp(tbn[1], normalize(ClothPelvisVector(IN.binormal)), hw)),
                                   normalize(lerp(tbn[2], normalize(ClothPelvisVector(IN.normal)), hw)));
                    clothOut = ClothPelvisVector(ClothOutwardBind(IN.position.xyz));
                    clothUp = ClothPelvisVector(float3(0.0f, 0.0f, 1.0f));
                    clothLegL = ClothLegBind(position.xyz, ClothLegNow[0], ClothLegNow[1], ClothLegNow[2]);
                    clothLegR = ClothLegBind(position.xyz, ClothLegNow[3], ClothLegNow[4], ClothLegNow[5]);
                }
                else {
                    clothOut = BonesTransformVector(Bones, offset, blend, ClothOutwardBind(IN.position.xyz));
                    const float4 legL = ClothLegBind(IN.position.xyz, ClothLimb[0], ClothLimb[1], ClothLimb[2]);
                    const float4 legR = ClothLegBind(IN.position.xyz, ClothLimb[4], ClothLimb[5], ClothLimb[6]);
                    clothLegL = float4(BonesTransformVector(Bones, offset, blend, legL.xyz), legL.w);
                    clothLegR = float4(BonesTransformVector(Bones, offset, blend, legR.xyz), legR.w);
                    clothUp = BonesTransformVector(Bones, offset, blend, float3(0.0f, 0.0f, 1.0f));
                }
            }
            ClothWindTBN(position.xyz, tbn, IN.vertexColor, IN.position.xyz, IN.clothWeight.x, clothOut, clothLegL, clothLegR, clothUp);
            [branch] if (ClothIsMask5() && ClothWalk.x > 0.5f) {
                position.xyz += ClothRingAt(IN.clothWeight.y) * IN.clothWeight.z;
                position.xyz += ClothLegPush(position.xyz, clothOut);
            }
        #endif
    
        OUT.sPosition.xyzw = mul(SkinModelViewProj, position.xyzw);
    #endif
    OUT.uv.zw = position.xy;
    
    #if defined(DIFFUSE) || defined(POINT)
        float3 light = LightData[0].xyz - position.xyz;
        OUT.lightDistSq.x = dot(light, light);
    #else
        float3 light = LightData[0].xyz;
    #endif

    OUT.lightDir.w = LightData[0].w;
    #if defined(DIFFUSE) || defined(POINT)
        OUT.lightDir.xyz = LAMP_VS_DIR(tbn, light);
    #else
        OUT.lightDir.xyz = mul(tbn, light);
    #endif

    OUT.viewDir.xyz = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.viewDir.w = position.z;

    #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
        light = LightData[1].xyz - position.xyz;
        OUT.light2Dir.w = LightData[1].w;
        OUT.light2Dir.xyz = LAMP_VS_DIR(tbn, light);
        OUT.lightDistSq.y = dot(light, light);
    #endif

    #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
        light = LightData[2].xyz - position.xyz;
        OUT.light3Dir.w = LightData[2].w;
        OUT.light3Dir.xyz = LAMP_VS_DIR(tbn, light);
        OUT.lightDistSq.z = dot(light, light);
    #endif
    
    #ifndef NO_VERTEX_COLOR
        OUT.vertexColor = clamp(IN.vertexColor, 0.0f, 1.0f);
    #endif
    #if defined(NVR_FOLD) && defined(NO_VERTEX_COLOR)
        OUT.foldColor = clamp(IN.vertexColor, 0.0f, 1.0f);
    #endif

    #ifndef NO_FOG
        float3 fogPos = OUT.sPosition.xyz;
    
        #ifdef REVERSED_DEPTH
            fogPos.z = OUT.sPosition.w - fogPos.z;
        #endif
    
        float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
        fogStrength = log2(fogStrength);
        OUT.fogColor.a = exp2(fogStrength * FogParam.z);
        OUT.fogColor.rgb = FogColor.rgb;
    #endif
    
    #ifdef PROJ_SHADOW
        float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    // Model-space shaders: recover a camera-relative world position from the clip position.
    // Written unconditionally, or the interpolator is left undefined.
    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.sPosition), SHADOW_VS_SENTINEL);
    #ifdef OBJ_SMOOTH_NORMAL_INTERP
        #ifndef SKIN
            OUT.smoothNormal = GetShadowSmoothNormal(ModelViewProj, position.xyz, tbn[2], OUT.shadowWorldPos.xyz);
        #else
            OUT.smoothNormal = GetShadowSmoothNormal(SkinModelViewProj, position.xyz, tbn[2], OUT.shadowWorldPos.xyz);
        #endif
    #endif

    #ifdef METAL_UP
        float4 upView = mul(TESR_InvProjectionTransform, float4(TESR_InvViewTransform[0][2], TESR_InvViewTransform[1][2], TESR_InvViewTransform[2][2], 0.0f));
        #ifndef SKIN
            float3 objUp = mul(upView, ModelViewProj).xyz;
        #else
            float3 objUp = mul(upView, SkinModelViewProj).xyz;
        #endif
        float3 upTangent = normalize(mul(tbn, objUp));
        OUT.lightDistSq.w = upTangent.x;
        OUT.lightDir.w = upTangent.y;
        OUT.vertexColor.a = upTangent.z * 0.5f + 0.5f;
    #endif
    #ifdef OBJ_SMOOTH_NORMAL_W
        {
            #ifndef SKIN
                const float3 smoothN = GetShadowSmoothNormal(ModelViewProj, position.xyz, tbn[2], OUT.shadowWorldPos.xyz).xyz;
            #else
                const float3 smoothN = GetShadowSmoothNormal(SkinModelViewProj, position.xyz, tbn[2], OUT.shadowWorldPos.xyz).xyz;
            #endif
            OUT.lightDistSq.w = smoothN.x;
            OUT.lightDir.w = smoothN.y;
            OUT.vertexColor.a = smoothN.z * 0.5f + 0.5f;
        }
    #endif

    return OUT;
};

#elif defined(VS) && LIGHTS >= 4

#ifndef SPECULAR_LAMPS6
    #define SPECULAR_LAMPS6 0
#endif
#if defined(MANY_LAMPS_ON) && MANY_LAMPS_ON && LIGHTS == 4 && defined(SPECULAR) && SPECULAR_LAMPS6
    #define MANY_LAMPS 1
#endif
#ifdef MANY_LAMPS
    #define MAX_LIGHTS 4
#elif LIGHTS > 4 || (LIGHTS > 3 && defined(SPECULAR) && SPECULAR_LAMPS6)
    #define MAX_LIGHTS 6
#elif LIGHTS > 3 && !defined(SPECULAR)
    #define MAX_LIGHTS 4
#else
    #define MAX_LIGHTS 3
#endif

// Camera-relative world position carrier. At MAX_LIGHTS 6 every TEXCOORD is taken, so it
// rides in light4/5/6's .w -- a light radius the PS never reads, since attenuation uses
// PSLightPosition[i].w. Below that, a free interpolator.
#if MAX_LIGHTS > 4
    // No fourth channel spare here, so the sentinel rides in lPosition.w. The vertex shader
    // normally puts LightData[0].w there and this pixel shader never reads it.
    #define SHADOW_WP_STORE(O, v) O.light4.w = (v).x; O.light5.w = (v).y; O.light6.w = (v).z; O.lPosition.w = SHADOW_VS_SENTINEL
    #define SHADOW_WP_LOAD(I)     float3((I).light4.w, (I).light5.w, (I).light6.w)
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).lPosition.w)
#else
    #define SHADOW_WP_DEDICATED
    #define SHADOW_WP_STORE(O, v) O.shadowWorldPos = float4((v), SHADOW_VS_SENTINEL)
    #define SHADOW_WP_LOAD(I)     (I).shadowWorldPos.xyz
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).shadowWorldPos.w)
#endif


struct VS_OUTPUT {
    float4 vertexColor : COLOR0;
    float4 fogColor : COLOR1;
    float4 sPosition : POSITION;
#if OBJECT_SMOOTH_NORMALS
    float4 uv : TEXCOORD0;
#else
    float2 uv : TEXCOORD0;
#endif
    float4 lPosition : TEXCOORD1;
    float4 lightDir : TEXCOORD2;  // .w = .x of viewDir
    float4 light2 : TEXCOORD3;   // .w = .y of viewDir
    float4 light3 : TEXCOORD4;  // .w = .z of viewDir
#if MAX_LIGHTS > 3
    float4 light4 : TEXCOORD5;
#endif
#if MAX_LIGHTS > 4
    float4 light5 : TEXCOORD6;
    float4 light6 : TEXCOORD7;
#endif
#ifdef SHADOW_WP_DEDICATED
    #if MAX_LIGHTS > 3
        float4 shadowWorldPos : TEXCOORD6;
    #else
        float4 shadowWorldPos : TEXCOORD5;
    #endif
#endif
};

float3 FogColor : register(c15);
float4 FogParam : register(c14);
float4 LightData[10] : register(c25);
#ifndef SKIN
row_major float4x4 ModelViewProj : register(c0);
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
#endif
float4 EyePosition : register(c16);
#ifndef OPT
    float4 fvars0 : register(c17);

    #define lightOffset 0
#else
    #define lightOffset 1
#endif

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;
    
    OUT.uv.xy = IN.uv.xy;
    
    float4 position = IN.position.xyzw;
    
    #ifndef SKIN
    float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);
    #ifdef CLOTH_WIND
        ClothWindTBN(position.xyz, tbn, IN.vertexColor, IN.position.xyz, IN.clothWeight.x, float3(IN.clothWeight.y, 0.0f, 0.0f), 0.0f, 0.0f, 0.0f);
    #endif
    #ifdef PLANT_WIND
        [branch] if (PlantA.x < -0.5f) position.xyz += PlantWindMove(position.xyz);
    #endif
    
    OUT.sPosition.xyzw = mul(ModelViewProj, position.xyzw);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
    
        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
        #ifdef CLOTH_WIND
            float3 clothOut = 0.0f;
            float4 clothLegL = 0.0f, clothLegR = 0.0f;
            float3 clothUp = 0.0f;
            [branch] if (ClothIsMask5()) {
                [branch] if (ClothWalk.x > 0.5f) {
                    const float hw = saturate(IN.clothWeight.x) * ClothWalk.y;
                    position.xyz = lerp(position.xyz, ClothPelvisPoint(IN.position.xyz), hw);
                    tbn = float3x3(normalize(lerp(tbn[0], normalize(ClothPelvisVector(IN.tangent)), hw)),
                                   normalize(lerp(tbn[1], normalize(ClothPelvisVector(IN.binormal)), hw)),
                                   normalize(lerp(tbn[2], normalize(ClothPelvisVector(IN.normal)), hw)));
                    clothOut = ClothPelvisVector(ClothOutwardBind(IN.position.xyz));
                    clothUp = ClothPelvisVector(float3(0.0f, 0.0f, 1.0f));
                    clothLegL = ClothLegBind(position.xyz, ClothLegNow[0], ClothLegNow[1], ClothLegNow[2]);
                    clothLegR = ClothLegBind(position.xyz, ClothLegNow[3], ClothLegNow[4], ClothLegNow[5]);
                }
                else {
                    clothOut = BonesTransformVector(Bones, offset, blend, ClothOutwardBind(IN.position.xyz));
                    const float4 legL = ClothLegBind(IN.position.xyz, ClothLimb[0], ClothLimb[1], ClothLimb[2]);
                    const float4 legR = ClothLegBind(IN.position.xyz, ClothLimb[4], ClothLimb[5], ClothLimb[6]);
                    clothLegL = float4(BonesTransformVector(Bones, offset, blend, legL.xyz), legL.w);
                    clothLegR = float4(BonesTransformVector(Bones, offset, blend, legR.xyz), legR.w);
                    clothUp = BonesTransformVector(Bones, offset, blend, float3(0.0f, 0.0f, 1.0f));
                }
            }
            ClothWindTBN(position.xyz, tbn, IN.vertexColor, IN.position.xyz, IN.clothWeight.x, clothOut, clothLegL, clothLegR, clothUp);
            [branch] if (ClothIsMask5() && ClothWalk.x > 0.5f) {
                position.xyz += ClothRingAt(IN.clothWeight.y) * IN.clothWeight.z;
                position.xyz += ClothLegPush(position.xyz, clothOut);
            }
        #endif
        OUT.sPosition.xyzw = mul(SkinModelViewProj, position.xyzw);
    #endif
    
    float3 viewDir = mul(tbn, EyePosition.xyz - position.xyz);
    
    OUT.lPosition.xyz = position.xyz;
    OUT.lPosition.w = LightData[0].w;
    
    #ifndef OPT
        float lights = min(MAX_LIGHTS, fvars0.z);
    #elif defined(SPECULAR)
        float lights = min(MAX_LIGHTS, EyePosition.w);
    #else
        float lights = min(MAX_LIGHTS, LightData[0].w);
    #endif
    float lightsFrac = frac(lights);
    float lightsThreshold = (lights < 0.0 ? (-lightsFrac < lightsFrac ? 1.0 : 0.0) : 0) + (lights - lightsFrac);
    float lightUsed;
    
    #ifndef OPT
        OUT.lightDir.w = viewDir.x;
        OUT.lightDir.xyz = mul(tbn, LightData[0].xyz);
    #else
        lightUsed = 0 < lightsThreshold ? 1.0 : 0.0;
        OUT.lightDir.xyz = lightUsed * mul(tbn, LightData[lightOffset + 0].xyz - position.xyz);
        OUT.lightDir.w = viewDir.x;
    #endif
    
    #ifdef MANY_LAMPS
        OUT.light2 = float4(0.0f, 0.0f, 0.0f, viewDir.y);
        OUT.light3 = float4(0.0f, 0.0f, 0.0f, viewDir.z);
        OUT.light4 = 0.0f;
    #else
    lightUsed = 1 < lightsThreshold ? 1.0 : 0.0;
    OUT.light2.xyz = lightUsed * mul(tbn, LightData[lightOffset + 1].xyz - position.xyz);
    OUT.light2.w = viewDir.y;

    lightUsed = 2 < lightsThreshold ? 1.0 : 0.0;
    OUT.light3.xyz = lightUsed * mul(tbn, LightData[lightOffset + 2].xyz - position.xyz);
    OUT.light3.w = viewDir.z;

    #if MAX_LIGHTS > 3
        lightUsed = 3 < lightsThreshold ? 1.0 : 0.0;
        OUT.light4.xyz = lightUsed * mul(tbn, LightData[lightOffset + 3].xyz - position.xyz);
        OUT.light4.w = lightUsed * LightData[lightOffset + 3].w;
    #endif
    #endif
    
    #if MAX_LIGHTS > 4
        lightUsed = 4 < lightsThreshold ? 1.0 : 0.0;
        OUT.light5.xyz = lightUsed * mul(tbn, LightData[lightOffset + 4].xyz - position.xyz);
        OUT.light5.w = lightUsed * LightData[lightOffset + 4].w;
        
        lightUsed = 5 < lightsThreshold ? 1.0 : 0.0;
        OUT.light6.xyz = lightUsed * mul(tbn, LightData[lightOffset + 5].xyz - position.xyz);
        OUT.light6.w = lightUsed * LightData[lightOffset + 5].w;
    #endif
    
    OUT.vertexColor = clamp(IN.vertexColor, 0.0f, 1.0f);

    float3 fogPos = OUT.sPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = OUT.sPosition.w - fogPos.z;
    #endif
    float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
    fogStrength = log2(fogStrength);
    OUT.fogColor.a = exp2(fogStrength * FogParam.z);
    OUT.fogColor.rgb = FogColor.rgb;

    // Through SHADOW_WP_STORE: at MAX_LIGHTS 6 this overwrites light4/5/6's .w, which the PS
    // ignores.
    float3 shadowWorldPos = GetShadowWorldPos(OUT.sPosition);
    SHADOW_WP_STORE(OUT, shadowWorldPos);
    #if OBJECT_SMOOTH_NORMALS
        #ifndef SKIN
            const float3 smoothN = GetShadowSmoothNormal(ModelViewProj, position.xyz, tbn[2], shadowWorldPos).xyz;
        #else
            const float3 smoothN = GetShadowSmoothNormal(SkinModelViewProj, position.xyz, tbn[2], shadowWorldPos).xyz;
        #endif
        OUT.uv.zw = smoothN.xy;
        OUT.vertexColor.a = smoothN.z * 0.5f + 0.5f;
    #endif
    #ifdef MANY_LAMPS
        OUT.light2.xyz = tbn[0];
        OUT.light3.xyz = tbn[1];
        OUT.light4.xyz = tbn[2];
    #endif

    return OUT;
};
#endif // Vertex shaders.

#if defined(PS) && (!defined(LIGHTS) || LIGHTS < 4)

struct PS_INPUT {
#if defined(NVR_FOLD) && defined(NO_VERTEX_COLOR)
    float4 foldColor : COLOR0;
#endif
#ifdef NVR_LAMP_SUMS
#if defined(NVR_SM4) && NVR_SM4
    float4 vpos4 : VPOS;
    #define NVR_IN_VPOS (IN.vpos4.xy - 0.5f)
#else
    float2 vpos : VPOS;
    #define NVR_IN_VPOS IN.vpos
#endif
#endif
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 uv : TEXCOORD0;
    float4 lightDir : TEXCOORD1_centroid;
#if LIGHTS > 1 || NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD2_centroid;
#endif
#if LIGHTS > 2 || NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD3_centroid;
#endif
    float4 viewDir : TEXCOORD6_centroid;
    float4 lightDistSq : TEXCOORD5;
    float4 shadowWorldPos : TEXCOORD4;
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef OBJ_SMOOTH_NORMAL_INTERP
    float4 smoothNormal : TEXCOORD8;
#endif
};

struct PS_OUTPUT {
    float4 color : COLOR0;
#if defined(ONLY_SPECULAR)
    float4 highlightCopy : COLOR1;
#endif
#if CAPTURED_NORMALS_ON
    #if !defined(ONLY_SPECULAR)
    float4 capturedPad1 : COLOR1;
    #endif
    float4 capturedPad2 : COLOR2;
    float4 capturedNormal : COLOR3;
#endif
};

#if defined(DIFFUSE) || defined(ONLY_SPECULAR)
    sampler2D NormalMap : register(s0);
    #if defined(NVR_FOLD) && defined(DIFFUSE)
        sampler2D FoldBaseMap : register(s7);
    #endif
#else
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
#endif

#ifdef LINEAR_LIGHTING
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 AmbientColorIn : register(c1);
        static float4 AmbientColor;
    #endif
    float4 PSLightColorIn[10] : register(c3);
    static float4 PSLightColor[10];
    #ifdef LIN_PREDECODED
        float4 AmbientColorDec : register(c64);
        float4 PSLightColorDec[10] : register(c66);
    #endif
#else
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 AmbientColor : register(c1);
    #endif
    float4 PSLightColor[10] : register(c3);
#endif

#if (defined(SI) || defined(HAIR)) && !defined(ONLY_SPECULAR)
    #ifdef ONLY_LIGHT
        sampler2D GlowMap : register(s3);
    #else
        sampler2D GlowMap : register(s4);
    #endif
    float4 EmittanceColor : register(c2);
#endif

#ifdef PROJ_SHADOW
    #if defined(ONLY_SPECULAR)
        sampler2D ShadowMap : register(s4);
        sampler2D ShadowMaskMap : register(s5);
    #elif defined(ONLY_LIGHT)
        sampler2D ShadowMap : register(s5);
        sampler2D ShadowMaskMap : register(s6);
    #else
        sampler2D ShadowMap : register(s6);
        sampler2D ShadowMaskMap : register(s7);
    #endif
#endif

#ifndef OPT
    float4 Toggles : register(c27);
#endif

#if defined(DIFFUSE) || defined(POINT)
    float4 TESR_LampPassTrim : register(c196);
#endif


PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;
    
#ifdef LINEAR_LIGHTING
    #ifdef LIN_PREDECODED
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++) PSLightColor[lightIndex] = float4(PSLightColorDec[lightIndex].rgb, PSLightColorIn[lightIndex].a);
    #else
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++)
        PSLightColor[lightIndex] = float4(LinColor(PSLightColorIn[lightIndex].rgb), PSLightColorIn[lightIndex].a);
    #endif
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        #ifdef LIN_PREDECODED
            AmbientColor = float4(AmbientColorDec.rgb, AmbientColorIn.a);
        #else
        AmbientColor = float4(LinColor(AmbientColorIn.rgb), AmbientColorIn.a);
        #endif
    #endif
#endif

    HudRain(IN.uv.xy, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w));

#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    float3 heatMaterialFactor = 1.0f;
#endif
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
    
        #if defined(ONLY_LIGHT)
            heatMaterialFactor = max(baseColor.rgb, 0.03f);
            #ifdef NVR_FOLD
                const float3 foldTexture = baseColor.rgb;
            #endif
            baseColor.rgb = 1;
        #endif
    #else
        float4 baseColor = 1;
    #endif
    
    #if !defined(OPT) && !defined(ONLY_SPECULAR)
        clip(AmbientColor.a >= 1 ? 0 : (baseColor.a - alphaTestRef));
    #endif
    
    float4 normal = HUD_RAIN_TEX(NormalMap, IN.uv.xy);
    normal.xyz = normalize(expand(normal.xyz));
    
    #ifdef RMAOS
        RmaosRead(IN.uv.xy);
    #endif
    float roughness = getRoughness(normal.a);

    // Geometric specular AA -- see the comment on SpecularAA itself. Unconditional: this is a
    // quality fix for high-frequency normal maps (hair chief among them), not a debug toggle,
    // and ddx/ddy have to run here at top level regardless of anything below.
    roughness = SpecularAA(normal.xyz, roughness);
    HandBlood(IN.uv.xy, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w));
    roughness = HandBloodRoughness(roughness);
    normal.xyz = HandBloodNormal(normal.xyz);
    roughness = HudRainRoughness(roughness);
    normal.xyz = HudRainNormal(normal.xyz);
    SetDiffuseEnergyKeep(normal.xyz, IN.viewDir.xyz, roughness);
    #ifdef NVR_FOLD
        const float foldMetalRoughness = SpecularAA(normal.xyz, getMetalRoughness(normal.a));
        #if defined(DIFFUSE)
            const float2 foldUvDx = ddx(IN.uv.xy), foldUvDy = ddy(IN.uv.xy);
        #endif
    #endif

    //if (TESR_DebugVar.y > 0.0) {
    //    OUT.color.a = 1;
    //    if (TESR_DebugVar.y > 0.1)
    //        OUT.color.rgb = roughness.xxx;
    //    else
    //        OUT.color.rgb = normal.aaa;
    //    return OUT;
    //}
    
    #ifndef NO_VERTEX_COLOR
        #if defined(HAIR)
            float4 glow = tex2D(GlowMap, IN.uv.xy);
            baseColor.rgb = (2 * ((IN.vertexColor.g * (EmittanceColor.rgb - 0.5)) + 0.5)) * lerp(baseColor.rgb, glow.rgb, glow.a);
        #elif !defined(OPT)
            baseColor.rgb = useVertexColor <= 0 ? baseColor.rgb : (baseColor.rgb * IN.vertexColor.rgb);
        #else
            baseColor.rgb = baseColor.rgb * IN.vertexColor.rgb;
        #endif
    #endif
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR) && !defined(ONLY_LIGHT)
        baseColor.rgb = HandBloodAlbedo(baseColor.rgb);
    #elif HAND_BLOOD && !defined(ONLY_SPECULAR)
        #if defined(DIFFUSE)
            baseColor.rgb = handBloodCover > 0.0f ? HandBloodAlbedo((0.25f).xxx) * 4.0f : baseColor.rgb;
        #else
            baseColor.rgb = handBloodCover > 0.0f ? HandBloodAlbedo(heatMaterialFactor) / heatMaterialFactor : baseColor.rgb;
        #endif
    #endif
    const float3 albedoStored = baseColor.rgb;
    baseColor.rgb = LinColor(baseColor.rgb);
    baseColor.rgb *= HudRainLens(float3(0.0f, 0.0f, 1.0f)).x;

    #ifdef METAL
        float metalRoughness = SpecularAA(normal.xyz, getMetalRoughness(normal.a));
        float metallic = 0.0f;
    #endif

    // Vanilla shadows.
    float3 shadowMultiplier = 1.0;
    #if defined(STBB)
        shadowMultiplier = 0.85;
    #elif defined(PROJ_SHADOW)
        float3 shadow = tex2D(ShadowMap, IN.shadowUVs.xy).xyz;
        float shadowMask = tex2D(ShadowMaskMap, IN.shadowUVs.zw).x;
        shadowMultiplier = lerp(1, shadow, shadowMask);
    #endif
    
    // Applied to PSLightColor[0], the sun, only: ambient, emittance and point lights are
    // untouched. ddx/ddy must stay at top level, outside any dynamic branch.
    //
    // Computed once here and reused by the ambient block further down (that block's condition,
    // !DIFFUSE && !ONLY_SPECULAR, is a strict subset of this one, since POINT implies
    // ONLY_SPECULAR) -- two separate ddx/ddy evaluations of the identical
    // GetShadowGeometricNormal(IN.shadowWorldPos.xyz) call previously coexisted in the same
    // pixel shader whenever FORWARD_SHADOWS was compiled in, which is fragile enough on its own
    // to corrupt unrelated interpolator reads placed nearby.
    #if !defined(DIFFUSE) && !defined(POINT)
        #ifdef OBJ_SMOOTH_NORMAL_INTERP
            float3 shadowGeometricNormal = ShadowFacingNormal(IN.smoothNormal.xyz, IN.shadowWorldPos.xyz);
        #elif defined(OBJ_SMOOTH_NORMAL_W)
            float3 shadowGeometricNormal = ShadowFacingNormal(ShadowPlainNormal(float2(IN.lightDistSq.w, IN.lightDir.w), IN.vertexColor.a), IN.shadowWorldPos.xyz);
        #else
            float3 shadowGeometricNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
        #endif
        // Decline to shadow if a vanilla vertex shader ran: the interpolator is undefined.
        float sunShadow = 1.0f;
        #if FORWARD_SHADOWS
        #if UNLIT_SHADOW_SKIP
        [branch] if (SHADOW_VS_PRESENT(IN.shadowWorldPos.w) && SunShadowWanted(normal.xyz, IN.lightDir.xyz))
            sunShadow = GetSunShadow(IN.shadowWorldPos.xyz, shadowGeometricNormal);
        #else
        sunShadow = SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
                  ? GetSunShadow(IN.shadowWorldPos.xyz, shadowGeometricNormal)
                  : 1.0f;
        #endif
        #endif
        shadowMultiplier *= sunShadow;
    #endif

    #if INTERIOR_SHADOWS
        float3 ptWorldPos = IN.shadowWorldPos.xyz;
        #if !defined(DIFFUSE) && !defined(POINT)
            float3 ptNormal = shadowGeometricNormal;
        #elif defined(OBJ_SMOOTH_NORMAL_INTERP)
            float3 ptNormal = ShadowFacingNormal(IN.smoothNormal.xyz, IN.shadowWorldPos.xyz);
        #else
            float3 ptNormal = 0.0f;
        #endif
        float ptValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;
        float ptShadow0 = 1.0f, ptShadow1 = 1.0f, ptShadow2 = 1.0f;
        [branch] if (NVR_PointShadowParams.x > 0.0f) {
            #if defined(DIFFUSE) || defined(POINT)
                ptShadow0 = POINT_SHADOW_LOOKUP(0, LAMP_ATT_SQ(IN.lightDistSq.x, IN.lightDir));
            #endif
            #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
                ptShadow1 = POINT_SHADOW_LOOKUP(1, LAMP_ATT_SQ(IN.lightDistSq.y, IN.light2Dir));
            #endif
            #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
                ptShadow2 = POINT_SHADOW_LOOKUP(2, LAMP_ATT_SQ(IN.lightDistSq.z, IN.light3Dir));
            #endif
        }
        float pointWeight = 0.0f;
    #endif

    #ifdef NVR_FOLD
    float3 lighting;
    [branch] if (NVR_SpecFade.y > 0.5f) {
        #if defined(DIFFUSE)
            float3 foldStored = tex2Dgrad(FoldBaseMap, IN.uv.xy, foldUvDx, foldUvDy).rgb;
        #else
            float3 foldStored = foldTexture;
        #endif
        foldStored *= NVR_SpecFade.z > 0.5f ? IN.foldColor.rgb : 1.0f;
        const float3 foldAlbedo = LinColor(foldStored);
        const float foldValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;
        float foldMetal = 0.0f;
        [branch] if (getMetalStrength() > 0.0f)
            foldMetal = getDerivedMetallic(normal.a, foldStored) * (foldValid > 0.0f ? getMetalFade(IN.shadowWorldPos.xyz) : 1.0f);
        const float foldRough = lerp(roughness, foldMetalRoughness, foldMetal);
        #if !defined(DIFFUSE)
            lighting = getSunLightingFull(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, foldAlbedo, foldRough, foldMetal);
            lighting += getAmbientLighting(AmbientColor.rgb, foldAlbedo * (1.0f - foldMetal), shadowGeometricNormal, foldValid);
            [branch] if (foldMetal > 0.0f)
                lighting += foldMetal * getMetalAmbient(AmbientColor.rgb, foldAlbedo, foldRough, saturate(dot(normal.xyz, normalize(IN.viewDir.xyz))),
                                                        0.0f, 0.0f, IN.shadowWorldPos.xyz, shadowGeometricNormal);
            float foldReach = 1.0f;
        #else
            const float foldAtt0 = LAMP_ATT_SQ(IN.lightDistSq.x, IN.lightDir);
            lighting = 0.0f;
            LAMP_IF(foldAtt0)
                lighting = getPointLightLightingAttFull(IN.lightDir.xyz, foldAtt0, SHADOWED(PSLightColor[0].rgb, 0, foldAtt0) * shadowMultiplier, IN.viewDir.xyz, normal.xyz, foldAlbedo, foldRough, foldMetal);
            float foldReach = foldAtt0;
        #endif
        #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
            const float foldAtt2 = LAMP_ATT_SQ(IN.lightDistSq.y, IN.light2Dir);
            LAMP_IF(foldAtt2)
                lighting += getPointLightLightingAttFull(IN.light2Dir.xyz, foldAtt2, SHADOWED(PSLightColor[1].rgb, 1, foldAtt2), IN.viewDir.xyz, normal.xyz, foldAlbedo, foldRough, foldMetal);
            foldReach = max(foldReach, foldAtt2);
        #endif
        #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
            const float foldAtt3 = LAMP_ATT_SQ(IN.lightDistSq.z, IN.light3Dir);
            LAMP_IF(foldAtt3)
                lighting += getPointLightLightingAttFull(IN.light3Dir.xyz, foldAtt3, SHADOWED(PSLightColor[2].rgb, 2, foldAtt3), IN.viewDir.xyz, normal.xyz, foldAlbedo, foldRough, foldMetal);
            foldReach = max(foldReach, foldAtt3);
        #endif
        lighting = min(lighting / max(foldAlbedo, 1e-6f), 60000.0f);
        #if defined(DIFFUSE)
            clip((TESR_LampPassTrim.x > 0.0f && foldReach <= 0.0f && BarrelHeatMuzzle.w <= 0.0f) ? -1.0f : 0.0f);
        #endif
    }
    else {
    #endif
    #if defined(METAL)
        #ifdef SI
            float3 glow = tex2D(GlowMap, IN.uv.xy).rgb;
        #endif
        float skyValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;
        #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
            float att2 = LAMP_ATT_SQ(IN.lightDistSq.y, IN.light2Dir);
            float3 light2Color = SHADOWED(PSLightColor[1].rgb, 1, att2);
        #endif
        #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
            float att3 = LAMP_ATT_SQ(IN.lightDistSq.z, IN.light3Dir);
            float3 light3Color = SHADOWED(PSLightColor[2].rgb, 2, att3);
        #endif
        float3 lighting;
        [branch] if (getMetalStrength() > 0.0f) {
            metallic = HAND_BLOOD_METAL(getDerivedMetallic(normal.a, albedoStored) * (skyValid > 0.0f ? getMetalFade(IN.shadowWorldPos.xyz) : 1.0f));
            metalRoughness = lerp(roughness, metalRoughness, metallic);
            lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, metalRoughness, metallic);
            #ifdef SI
                lighting += baseColor.rgb * LinColor(glow.rgb * EmittanceColor.rgb);
            #endif
            lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb * (1.0f - metallic), shadowGeometricNormal, skyValid);
            [branch] if (metallic > 0.0f) {
                float3 metalView = normalize(IN.viewDir.xyz);
                #ifdef OBJ_SMOOTH_NORMAL_W
                bool upKnown = skyValid > 0.0f;
                float upReflect = upKnown ? reflect(normalize(IN.shadowWorldPos.xyz), shadowGeometricNormal).z : 0.0f;
                #else
                float3 upTangent = float3(IN.lightDistSq.w, IN.lightDir.w, IN.vertexColor.a * 2.0f - 1.0f);
                float upLength = length(upTangent);
                bool upKnown = upLength > 0.5f && skyValid > 0.0f;
                float upReflect = upKnown ? dot(reflect(-metalView, normal.xyz), upTangent / upLength) : 0.0f;
                #endif
                lighting += metallic * getMetalAmbient(AmbientColor.rgb, baseColor.rgb, metalRoughness, saturate(dot(normal.xyz, metalView)),
                                                       upReflect, upKnown ? 1.0f : 0.0f,
                                                       IN.shadowWorldPos.xyz, shadowGeometricNormal);
            }
            #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
                LAMP_IF(att2)
                    lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, light2Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, metalRoughness, metallic);
            #endif
            #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
                LAMP_IF(att3)
                    lighting += getPointLightLightingAtt(IN.light3Dir.xyz, att3, light3Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, metalRoughness, metallic);
            #endif
        }
        else {
            lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            #ifdef SI
                lighting += baseColor.rgb * LinColor(glow.rgb * EmittanceColor.rgb);
            #endif
            lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, shadowGeometricNormal, skyValid);
            #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
                LAMP_IF(att2)
                    lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, light2Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            #endif
            #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
                LAMP_IF(att3)
                    lighting += getPointLightLightingAtt(IN.light3Dir.xyz, att3, light3Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            #endif
        }
    #else
    #if !defined(DIFFUSE) && !defined(POINT)
        FOLD_DECL lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
    #else
        // Pointlights only. Attenuate from the object-space lightDistSq.x carried from the VS,
        // not length(IN.lightDir.xyz) -- that vector is tangent-space (TBN-transformed) and its
        // length is only correct if the TBN basis is orthonormal.
        float att0 = LAMP_ATT_SQ(IN.lightDistSq.x, IN.lightDir);
        #if INTERIOR_SHADOWS && defined(POINT)
            FOLD_DECL lighting = 0.0f;
            LAMP_IF(att0) {
                lighting = getPointLightLightingAtt(IN.lightDir.xyz, att0, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
                pointWeight += weight(lighting);
                lighting *= POINT_SHADOW(0, att0);
            }
        #else
            FOLD_DECL lighting = 0.0f;
            LAMP_IF(att0)
                lighting = getPointLightLightingAtt(IN.lightDir.xyz, att0, SHADOWED(PSLightColor[0].rgb, 0, att0) * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
        #endif
    #endif
    
    // Self emmitance.
    #ifdef SI
        float3 glow = tex2D(GlowMap, IN.uv.xy).rgb;
        lighting += baseColor.rgb * LinColor(glow.rgb * EmittanceColor.rgb);
    #endif
    
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        // Reuses shadowGeometricNormal computed above -- see the comment there. Always in scope
        // here: POINT implies ONLY_SPECULAR, so !ONLY_SPECULAR implies !POINT.
        lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, shadowGeometricNormal,
                                       SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);
    #endif

    // Other light sources. Same object-space attenuation fix as light0 above.
    #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
        float att2 = LAMP_ATT_SQ(IN.lightDistSq.y, IN.light2Dir);
        #if INTERIOR_SHADOWS && defined(POINT)
            LAMP_IF(att2) {
                float3 light2Term = getPointLightLightingAtt(IN.light2Dir.xyz, att2, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
                pointWeight += weight(light2Term);
                lighting += light2Term * POINT_SHADOW(1, att2);
            }
        #else
            LAMP_IF(att2)
                lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, SHADOWED(PSLightColor[1].rgb, 1, att2), IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
        #endif
    #endif

    #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
        float att3 = LAMP_ATT_SQ(IN.lightDistSq.z, IN.light3Dir);
        #if INTERIOR_SHADOWS && defined(POINT)
            LAMP_IF(att3) {
                float3 light3Term = getPointLightLightingAtt(IN.light3Dir.xyz, att3, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
                pointWeight += weight(light3Term);
                lighting += light3Term * POINT_SHADOW(2, att3);
            }
        #else
            LAMP_IF(att3)
                lighting += getPointLightLightingAtt(IN.light3Dir.xyz, att3, SHADOWED(PSLightColor[2].rgb, 2, att3), IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
        #endif
    #endif

    #if defined(DIFFUSE) || defined(POINT)
        float lampReach = att0;
        #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
            lampReach = max(lampReach, att2);
        #endif
        #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
            lampReach = max(lampReach, att3);
        #endif
        clip((TESR_LampPassTrim.x > 0.0f && lampReach <= 0.0f && BarrelHeatMuzzle.w <= 0.0f) ? -1.0f : 0.0f);
    #endif

    #endif
    #ifdef NVR_FOLD
    }
    #endif

    #if !defined(DIFFUSE) && !defined(POINT)
        lighting += PSLightColor[0].rgb * shadowMultiplier * baseColor.rgb * HudRainLens(IN.lightDir.xyz).y;
    #endif
    float3 finalColor = LinEncode(lighting.rgb);
    #if defined(ONLY_LIGHT)
        [flatten] if (TESR_PassRedirect.x > 0.5f) finalColor = max(lighting.rgb, 0.0f);
    #endif
    #ifdef NVR_LAMP_SUMS
        [branch] if (TESR_PassRedirect.y > 0.5f) {
            const float3 runStart = max(tex2Dlod(NVR_LampRunStart, float4((NVR_IN_VPOS + 0.5f) * TESR_PassRedirect.zw, 0.0f, 0.0f)).rgb, 0.0f);
            finalColor = LinEncode(LinColor(runStart) + max(lighting.rgb, 0.0f)) - runStart;
        }
    #endif
    finalColor = MaterialRulesDebug(finalColor, albedoStored);
    finalColor = RmaosDebug(finalColor, albedoStored);

    #ifdef METAL
        [branch] if (TESR_PBRMetalLook.w > 0.5f) finalColor = lerp(luma(albedoStored).xxx * 0.35f, float3(1.0f, 0.45f, 0.05f), metallic);
    #endif
    
    // Fog.
    #ifndef NO_FOG
        #ifndef OPT
            finalColor.rgb = (useFog <= 0.0 ? finalColor.rgb : lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a));
        #else
            finalColor.rgb = lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a);
        #endif
    #endif
    

#if SHADOW_FORCE_MARKER
    finalColor.rgb = float3(1.0f, 0.0f, 1.0f);   // unconditional: proves this shader ran
#endif

#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    OUT.color.rgb = ApplyBarrelHeat(finalColor.rgb, BarrelHeatMask(float3(IN.uv.zw, IN.viewDir.w)), heatMaterialFactor);
#else
    OUT.color.rgb = ApplyBarrelHeat(finalColor.rgb, BarrelHeatMask(float3(IN.uv.zw, IN.viewDir.w)));
#endif

#if INTERIOR_SHADOWS
    [branch] if (NVR_PointShadowDebug.x > 2.5f) {
        #if defined(DIFFUSE) || defined(ONLY_SPECULAR)
            OUT.color.rgb = 0.0f;
        #else
            OUT.color.rgb = PointShadowTrace(ptWorldPos, ptNormal, ptValid);
        #endif
    }
    #if POINT_SHADOW_RT
        [branch] if (NVR_PointShadowRT.x > 2.5f) {
            #if defined(DIFFUSE) || defined(ONLY_SPECULAR)
                OUT.color.rgb = 0.0f;
            #else
                OUT.color.rgb = PointShadowRtCoverage(ptWorldPos);
            #endif
        }
    #endif
#endif

    #if defined(DIFFUSE)
        OUT.color.a = 1;
    #elif defined(ONLY_SPECULAR)
        #if !defined(DIFFUSE) && !defined(POINT)
            // Alpha here is a blend WEIGHT (this pass's own brightness, used to fade
            // its additive specular/sheen layer smoothly), not a darkness value --
            // unlike the colour output, it must not shrink with sun shadow, or the
            // whole layer fades toward invisible in shadow instead of just going
            // dark. That read as "alpha blending broken" under Forward Shadows
            // specifically, because sunShadow is the one new factor it added to
            // shadowMultiplier here (STBB/PROJ_SHADOW's own contribution predates
            // this and is left alone). Dividing it back out recovers the weight
            // this pass would have produced without Forward Shadows; harmless and
            // exact when Forward Shadows is compiled out, since sunShadow is then
            // fixed at 1.0.
            OUT.color.a = weight(finalColor.rgb) / max(sunShadow, 0.05f);
        #elif INTERIOR_SHADOWS
            OUT.color.a = pointWeight;
        #else
            OUT.color.a = weight(finalColor.rgb);
        #endif
    #elif defined(ONLY_LIGHT)
        OUT.color.a = ReflMaterialAlpha(baseColor.a);
    #else
        OUT.color.a = ReflMaterialAlpha(baseColor.a * AmbientColor.a);
    #endif
    #if defined(ONLY_SPECULAR)
        OUT.highlightCopy = OUT.color;
    #endif
#if CAPTURED_NORMALS_ON
    #if !defined(ONLY_SPECULAR)
        OUT.capturedPad1 = 0.0f;
    #endif
    OUT.capturedPad2 = 0.0f;
    #if defined(DIFFUSE) || defined(ONLY_SPECULAR) || defined(NVR_SLOT_TIGHT)
        OUT.capturedNormal = CAPTURED_NORMAL_INVALID;
    #elif defined(OBJ_SMOOTH_NORMAL_INTERP)
        OUT.capturedNormal = CapturedNormalOut(IN.smoothNormal.xyz, IN.shadowWorldPos.xyz);
    #elif defined(OBJ_SMOOTH_NORMAL_W)
        OUT.capturedNormal = CapturedNormalOut(ShadowPlainNormal(float2(IN.lightDistSq.w, IN.lightDir.w), IN.vertexColor.a), IN.shadowWorldPos.xyz);
    #else
        OUT.capturedNormal = CAPTURED_NORMAL_INVALID;
    #endif
#endif
    OUT.color.rgb = LinearSceneOut(OUT.color.rgb);

    return OUT;
}

#elif defined(PS) && LIGHTS >= 4

#ifndef SPECULAR_LAMPS6
    #define SPECULAR_LAMPS6 0
#endif
#if defined(MANY_LAMPS_ON) && MANY_LAMPS_ON && LIGHTS == 4 && defined(SPECULAR) && SPECULAR_LAMPS6
    #define MANY_LAMPS 1
#endif
#ifdef MANY_LAMPS
    #define MAX_LIGHTS 4
#elif LIGHTS > 4 || (LIGHTS > 3 && defined(SPECULAR) && SPECULAR_LAMPS6)
    #define MAX_LIGHTS 6
#elif LIGHTS > 3 && !defined(SPECULAR)
    #define MAX_LIGHTS 4
#else
    #define MAX_LIGHTS 3
#endif

// Camera-relative world position carrier. At MAX_LIGHTS 6 every TEXCOORD is taken, so it
// rides in light4/5/6's .w -- a light radius the PS never reads, since attenuation uses
// PSLightPosition[i].w. Below that, a free interpolator.
#if MAX_LIGHTS > 4
    // No fourth channel spare here, so the sentinel rides in lPosition.w. The vertex shader
    // normally puts LightData[0].w there and this pixel shader never reads it.
    #define SHADOW_WP_STORE(O, v) O.light4.w = (v).x; O.light5.w = (v).y; O.light6.w = (v).z; O.lPosition.w = SHADOW_VS_SENTINEL
    #define SHADOW_WP_LOAD(I)     float3((I).light4.w, (I).light5.w, (I).light6.w)
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).lPosition.w)
#else
    #define SHADOW_WP_DEDICATED
    #define SHADOW_WP_STORE(O, v) O.shadowWorldPos = float4((v), SHADOW_VS_SENTINEL)
    #define SHADOW_WP_LOAD(I)     (I).shadowWorldPos.xyz
    #define SHADOW_WP_VALID(I)    SHADOW_VS_PRESENT((I).shadowWorldPos.w)
#endif

#ifdef MANY_LAMPS
    #define MANY_LAMPS_MAX 13
    float4 NVR_ManyLamps[1 + 2 * MANY_LAMPS_MAX] : register(c37);
    #define MANY_LAMPS_B 6
    #define MANY_LAMPS_C 5
    float4 NVR_ManyLampsB[2 * MANY_LAMPS_B] : register(c76);
    float4 NVR_ManyLampsC[2 * MANY_LAMPS_C] : register(c98);
#endif

struct PS_INPUT {
    float4 vertexColor : COLOR0;
    float4 fogColor : COLOR1;
    float4 sPosition : POSITION;
#if OBJECT_SMOOTH_NORMALS
    float4 uv : TEXCOORD0;
#else
    float2 uv : TEXCOORD0;
#endif
    float4 lPosition : TEXCOORD1;
    float4 lightDir : TEXCOORD2_centroid;  // .w = .x of viewDir
    float4 light2 : TEXCOORD3_centroid; // .w = .y of viewDir
    float4 light3 : TEXCOORD4_centroid; // .w = .z of viewDir
#if MAX_LIGHTS > 3
    float4 light4 : TEXCOORD5_centroid;
#endif
#if MAX_LIGHTS > 4
    float4 light5 : TEXCOORD6_centroid;
    float4 light6 : TEXCOORD7_centroid;
#endif
#ifdef SHADOW_WP_DEDICATED
    #if MAX_LIGHTS > 3
        float4 shadowWorldPos : TEXCOORD6;
    #else
        float4 shadowWorldPos : TEXCOORD5;
    #endif
#endif
};

#if OBJECT_SMOOTH_NORMALS
    #define OBJ_WORLD_NORMAL(p) ShadowFacingNormal(ShadowPlainNormal(IN.uv.zw, IN.vertexColor.a), (p))
#else
    #define OBJ_WORLD_NORMAL(p) GetShadowGeometricNormal(p)
#endif

struct PS_OUTPUT {
    float4 color : COLOR0;
#if CAPTURED_NORMALS_ON
    float4 capturedPad1 : COLOR1;
    float4 capturedPad2 : COLOR2;
    float4 capturedNormal : COLOR3;
#endif
};

sampler2D BaseMap : register(s0);
sampler2D NormalMap : register(s1);
#if defined(SI) && !defined(OPT) && defined(MANY_LAMPS_ON) && MANY_LAMPS_ON
    sampler2D GlowMap : register(s4);
#endif

#ifdef LINEAR_LIGHTING
    float4 AmbientColorIn : register(c1);
    static float4 AmbientColor;
    float4 PSLightColorIn[10] : register(c3);
    static float4 PSLightColor[10];
    #ifdef LIN_PREDECODED
        float4 AmbientColorDec : register(c64);
        float4 PSLightColorDec[10] : register(c66);
    #endif
#else
    float4 AmbientColor : register(c1);
    float4 PSLightColor[10] : register(c3);
#endif
float4 PSLightPosition[8] : register(c19);


#ifndef OPT
    float4 EmittanceColor : register(c2);

    float4 Toggles : register(c27);

    #define lightsUsed EmittanceColor.a
    #define lightOffset 0
    #define glossPow Toggles.z
#else
    #define lightsUsed PSLightColor[0].a
    #define lightOffset 1
    #define glossPow PSLightColor[1].w
#endif

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

#ifdef LINEAR_LIGHTING
    #ifdef LIN_PREDECODED
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++) PSLightColor[lightIndex] = float4(PSLightColorDec[lightIndex].rgb, PSLightColorIn[lightIndex].a);
    #else
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++)
        PSLightColor[lightIndex] = float4(LinColor(PSLightColorIn[lightIndex].rgb), PSLightColorIn[lightIndex].a);
    #endif
    #ifdef LIN_PREDECODED
        AmbientColor = float4(AmbientColorDec.rgb, AmbientColorIn.a);
    #else
    AmbientColor = float4(LinColor(AmbientColorIn.rgb), AmbientColorIn.a);
    #endif
#endif

    HudRain(IN.uv.xy, SHADOW_WP_LOAD(IN), SHADOW_WP_VALID(IN));
    float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
    
    #ifndef OPT
        clip(AmbientColor.a >= 1 ? 0 : (baseColor.a - alphaTestRef));
    #endif
    
    #ifndef OPT
        baseColor.rgb = useVertexColor <= 0 ? baseColor.rgb : (baseColor.rgb * IN.vertexColor.rgb);
    #else
        baseColor.rgb = baseColor.rgb * IN.vertexColor.rgb;
    #endif
    
    float4 normal = HUD_RAIN_TEX(NormalMap, IN.uv.xy);
    normal.xyz = normalize(expand(normal.xyz));
    
    #ifdef RMAOS
        RmaosRead(IN.uv.xy);
    #endif
    float roughness = getRoughness(normal.a);
    roughness = SpecularAA(normal.xyz, roughness);
    HandBlood(IN.uv.xy, SHADOW_WP_LOAD(IN), SHADOW_WP_VALID(IN));
    roughness = HandBloodRoughness(roughness);
    normal.xyz = HandBloodNormal(normal.xyz);
    roughness = HudRainRoughness(roughness);
    normal.xyz = HudRainNormal(normal.xyz);
    SetDiffuseEnergyKeep(normal.xyz, float3(IN.lightDir.w, IN.light2.w, IN.light3.w), roughness);
    baseColor.rgb = HandBloodAlbedo(baseColor.rgb);
    const float3 albedoStored = baseColor.rgb;
    baseColor.rgb = LinColor(baseColor.rgb);
    baseColor.rgb *= HudRainLens(float3(0.0f, 0.0f, 1.0f)).x;

    // Lighting.
    float3 viewDir = { IN.lightDir.w, IN.light2.w, IN.light3.w };

    #ifdef METAL
        float metalRoughness = SpecularAA(normal.xyz, getMetalRoughness(normal.a));
        float metallic = 0.0f;
        [branch] if (getMetalStrength() > 0.0f)
            metallic = HAND_BLOOD_METAL(getDerivedMetallic(normal.a, albedoStored) * (SHADOW_WP_VALID(IN) ? getMetalFade(SHADOW_WP_LOAD(IN)) : 1.0f));
        float lightRoughness = lerp(roughness, metalRoughness, metallic);
    #endif

    float att;

    #if INTERIOR_SHADOWS
        float3 ptWorldPos = SHADOW_WP_LOAD(IN);
        float3 ptNormal = OBJ_WORLD_NORMAL(ptWorldPos);
        float ptValid = SHADOW_WP_VALID(IN) ? 1.0f : 0.0f;
        #define PT_ATT(i) vanillaAtt(PSLightPosition[i].xyz - IN.lPosition.xyz, PSLightPosition[i].w)
        float ptShadow0 = 1.0f, ptShadow1 = 1.0f, ptShadow2 = 1.0f, ptShadow3 = 1.0f, ptShadow4 = 1.0f, ptShadow5 = 1.0f;
        #if defined(LAMP_SLOT_SKIP) || defined(MANY_LAMPS)
        [branch] if (NVR_PointShadowParams.x > 0.0f) {
            float a;
            #ifdef OPT
                a = PT_ATT(0);				[branch] if (a > 0.0f && 0 < lightsUsed) ptShadow0 = POINT_SHADOW_LOOKUP(0, a);
            #endif
            a = PT_ATT(lightOffset + 0);	[branch] if (a > 0.0f && 1 < lightsUsed) ptShadow1 = POINT_SHADOW_LOOKUP(1, a);
            a = PT_ATT(lightOffset + 1);	[branch] if (a > 0.0f && 2 < lightsUsed) ptShadow2 = POINT_SHADOW_LOOKUP(2, a);
            a = PT_ATT(lightOffset + 2);	[branch] if (a > 0.0f && 3 < lightsUsed) ptShadow3 = POINT_SHADOW_LOOKUP(3, a);
            a = PT_ATT(lightOffset + 3);	[branch] if (a > 0.0f && 4 < lightsUsed) ptShadow4 = POINT_SHADOW_LOOKUP(4, a);
            a = PT_ATT(lightOffset + 4);	[branch] if (a > 0.0f && 5 < lightsUsed) ptShadow5 = POINT_SHADOW_LOOKUP(5, a);
        }
        #else
        [branch] if (NVR_PointShadowParams.x > 0.0f) {
            #ifdef OPT
                ptShadow0 = POINT_SHADOW_LOOKUP(0, PT_ATT(0));
            #endif
            ptShadow1 = POINT_SHADOW_LOOKUP(1, PT_ATT(lightOffset + 0));
            ptShadow2 = POINT_SHADOW_LOOKUP(2, PT_ATT(lightOffset + 1));
            #if MAX_LIGHTS > 3
                ptShadow3 = POINT_SHADOW_LOOKUP(3, PT_ATT(lightOffset + 2));
            #endif
            #if MAX_LIGHTS > 4
                ptShadow4 = POINT_SHADOW_LOOKUP(4, PT_ATT(lightOffset + 3));
                ptShadow5 = POINT_SHADOW_LOOKUP(5, PT_ATT(lightOffset + 4));
            #endif
        }
        #endif
        #undef PT_ATT
    #endif

    // Forward sun shadows -- see the LIGHTS < 4 variant. Only the OPT-off path has a sun
    // term; with OPT the first slot is a point light and must not be shadowed by the sun.
    #ifndef OPT
        float3 sunShadowWorldPos = SHADOW_WP_LOAD(IN);
        #if INTERIOR_SHADOWS
            float3 sunShadowNormal = ptNormal;
        #else
            float3 sunShadowNormal = OBJ_WORLD_NORMAL(sunShadowWorldPos);
        #endif
        // Decline to shadow if a vanilla vertex shader ran: the interpolator is undefined.
        float sunShadow = 1.0f;
        #if FORWARD_SHADOWS
        #if UNLIT_SHADOW_SKIP
        [branch] if (SHADOW_WP_VALID(IN) && SunShadowWanted(normal.xyz, IN.lightDir.xyz))
            sunShadow = GetSunShadow(sunShadowWorldPos, sunShadowNormal);
        #else
        sunShadow = SHADOW_WP_VALID(IN)
                  ? GetSunShadow(sunShadowWorldPos, sunShadowNormal)
                  : 1.0f;
        #endif
        #endif
    #endif

    // ddx/ddy must stay at pixel-shader top level.
    #if INTERIOR_SHADOWS
        float3 ambNormal = ptNormal;
    #else
        float3 ambNormal = OBJ_WORLD_NORMAL(SHADOW_WP_LOAD(IN));
    #endif

    float3 lighting;
    #ifdef METAL
    [branch] if (metallic > 0.0f) {
        #define LIGHT_FINISH lightRoughness, metallic
        #include "includes/ManyLightsDirect.hlsl"
        #undef LIGHT_FINISH
        lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb * (1.0f - metallic), ambNormal,
                                       SHADOW_WP_VALID(IN) ? 1.0f : 0.0f);
        lighting += metallic * getMetalAmbient(AmbientColor.rgb, baseColor.rgb, lightRoughness,
                                               saturate(dot(normal.xyz, normalize(viewDir))), 0.0f, 0.0f,
                                               (0.0f).xxx, (0.0f).xxx);
    }
    else
    #endif
    {
        #define LIGHT_FINISH roughness
        #include "includes/ManyLightsDirect.hlsl"
        #undef LIGHT_FINISH
        lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, ambNormal,
                                       SHADOW_WP_VALID(IN) ? 1.0f : 0.0f);
    }

    #if defined(SI) && !defined(OPT) && defined(MANY_LAMPS_ON) && MANY_LAMPS_ON
        lighting += baseColor.rgb * LinColor(tex2D(GlowMap, IN.uv.xy).rgb * EmittanceColor.rgb);
    #endif

    // TODO: Vanilla attenuates the full specular term by IN.lPosition.w for some reason. Is this a problem?
    float3 finalColor = LinEncode(lighting);
    finalColor = MaterialRulesDebug(finalColor, albedoStored);
    finalColor = RmaosDebug(finalColor, albedoStored);

    #ifdef METAL
        [branch] if (TESR_PBRMetalLook.w > 0.5f) finalColor = lerp(luma(albedoStored).xxx * 0.35f, float3(1.0f, 0.45f, 0.05f), metallic);
    #endif
    
    #ifndef OPT
        finalColor.rgb = (useFog <= 0.0 ? finalColor.rgb : lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a));
    #else
        finalColor.rgb = lerp(finalColor.rgb, IN.fogColor.rgb, IN.fogColor.a);
    #endif
    

#if SHADOW_FORCE_MARKER
    finalColor.rgb = float3(1.0f, 0.0f, 1.0f);   // unconditional: proves this shader ran
#endif

    OUT.color.rgb = ApplyBarrelHeat(finalColor.rgb, BarrelHeatMask(IN.lPosition.xyz));
    #if INTERIOR_SHADOWS
        [branch] if (NVR_PointShadowDebug.x > 2.5f) OUT.color.rgb = PointShadowTrace(ptWorldPos, ptNormal, ptValid);
        #if POINT_SHADOW_RT
            [branch] if (NVR_PointShadowRT.x > 2.5f) OUT.color.rgb = PointShadowRtCoverage(ptWorldPos);
        #endif
    #endif
    OUT.color.a = ReflMaterialAlpha(baseColor.a * AmbientColor.a);
#if CAPTURED_NORMALS_ON
    OUT.capturedPad1 = 0.0f;
    OUT.capturedPad2 = 0.0f;
    #if OBJECT_SMOOTH_NORMALS && !defined(NVR_SLOT_TIGHT)
        OUT.capturedNormal = CapturedNormalOut(ShadowPlainNormal(IN.uv.zw, IN.vertexColor.a), SHADOW_WP_LOAD(IN));
    #else
        OUT.capturedNormal = CAPTURED_NORMAL_INVALID;
    #endif
#endif
    OUT.color.rgb = LinearSceneOut(OUT.color.rgb);

    return OUT;
}

#endif // Pixel shaders.
