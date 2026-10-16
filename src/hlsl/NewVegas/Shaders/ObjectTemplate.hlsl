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
    // UNOFFICIAL: metal (Object.hlsl getDerivedMetallic) in the passes that light an object fully at once (colour, the
    // lights and the ambient) with a highlight: METAL_UP, the vertex shaders send world up for its reflection; METAL,
    // the pixel shaders use it. Not on objects without specular (metal without a highlight is only darker), nor hair
    // (grey hair is no metal); the lighting-only and specular-only passes stay as they were.
    #define METAL_UP
    #if defined(SPECULAR) && !defined(HAIR)
        #define METAL
    #endif
#endif

#include "includes/Helpers.hlsl"
// UNOFFICIAL: the live surroundings cube for metal (includes/LiveCube.hlsl), before Object.hlsl, whose getMetalAmbient reads
// it. Compiled in only when a CubeReflections setting is above 0 at startup (LIVE_CUBE, ShaderRecord::LoadShader), and not with
// the sun cross-fade, whose old atlas takes the same sampler.
#ifndef LIVE_CUBE
    #define LIVE_CUBE 0
#endif
#if defined(PS) && LIVE_CUBE && !(defined(SUN_CROSSFADE) && SUN_CROSSFADE)
    #define NVR_LIVE_CUBE
    #include "includes/LiveCube.hlsl"
#endif
// UNOFFICIAL material rules (Object.hlsl): the per-texture class the pixel shaders read (c197-c199). Compiled in only when
// [Shaders.PBR.MaterialRules] Enabled is on at startup (MATERIAL_RULES_ON, ShaderRecord::LoadShader).
#ifndef MATERIAL_RULES_ON
    #define MATERIAL_RULES_ON 0
#endif
#if defined(PS) && MATERIAL_RULES_ON
    #define MATERIAL_RULES
#endif
// UNOFFICIAL _rmaos maps (Object.hlsl): compiled in only when [Shaders.PBR.Rmaos] Enabled is on at startup (RMAOS_ON,
// ShaderRecord::LoadShader).
#ifndef RMAOS_ON
    #define RMAOS_ON 0
#endif
#if defined(PS) && RMAOS_ON
    #define RMAOS
#endif
// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl, [Shaders.PBR.Linear]): the pixel shaders light in linear space while
// it is on. Compiled in only when on at startup (LINEAR_LIGHTING_ON, ShaderRecord::LoadShader). The game's colour constants
// are then read under new names (...In) and decoded into the old names at the start of main.
#ifndef LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING_ON 0
#endif
#if defined(PS) && LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING
#endif
#include "includes/Object.hlsl"
// UNOFFICIAL: the camera matrices Shadow.hlsl rebuilds world positions with move out of c100-c107 in skinned vertex
// shaders: the game's bone upload reaches past Bones[54] (c97) and overwrote them, so actors sampled the sun shadow
// at the wrong place. c240-c247 is beyond any bone write.
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#include "includes/Shadow.hlsl"
#ifdef PS
    // UNOFFICIAL forward point-light shadows (INTERIOR_SHADOWS): SHADOWED(colour, k, att) gives light k of the draw
    // its own cube-map shadow (looked up once per pixel near the top of main); without INTERIOR_SHADOWS it is just the
    // colour. POINT_SHADOW_SCALAR (2026-10-14): ptShadow0..5 held as plain numbers, the debug views' tint applied where
    // each is used: six float3 kept alive across the lighting put the 4+ lamp shader (SLS2035 with _rmaos maps, hand blood
    // and MoreLampsPerObject on) one temp register over ps_3_0's 32, and its interior record failed to compile.
    #define POINT_SHADOW_SCALAR
    #include "includes/PointShadowForward.hlsl"
    // UNOFFICIAL 2026-10-13: blood on the first-person weapon and gloves (c218-c219, set per draw; off for every other draw).
    #include "includes/HandBlood.hlsl"
    // UNOFFICIAL 2026-10-14: rain on the first-person weapon (Wet World HudDrops, includes/HudRain.hlsl, c221-c222 set per draw;
    // compiled in only when HudDrops is on at startup: HUD_RAIN_ON, ShaderRecord::LoadShader).
    #if defined(HUD_RAIN_ON) && HUD_RAIN_ON
        #define HUD_RAIN 1
    #endif
    #include "includes/HudRain.hlsl"
#endif

// GunFX barrel glow (P67). Heat bridge from GunFX.dll. Coordinates are local to the drawn gun mesh, so
// viewmodel projection and camera motion cannot move the mask off the barrel.
float4 BarrelHeatMuzzle : register(c171); // local xyz, strength
float4 BarrelHeatAxis : register(c172);   // local unit vector toward the breech, length
float4 BarrelHeatShape : register(c173);  // local radius, forward reach

float BarrelHeatMask(float3 localPos) {
    float3 offset = localPos - BarrelHeatMuzzle.xyz;
    float along = dot(offset, BarrelHeatAxis.xyz);
    float radial = length(offset - BarrelHeatAxis.xyz * along);
    float span = max(BarrelHeatAxis.w, 1.0f);
    float radius = max(BarrelHeatShape.x, 1.0f);
    float forward = max(BarrelHeatShape.y, 1.0f);
    // Keep the muzzle hottest and taper continuously toward the breech.
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
    // Lighting-only passes are later modulated by the material texture.
    // Undo that modulation for the emissive term, keeping ordinary lighting intact.
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
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

#if defined(VS) && LIGHTS < 4

struct VS_OUTPUT {
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 sPosition : POSITION;
    float4 uv : TEXCOORD0; // .zw = local barrel position xy
    float4 lightDir : TEXCOORD1;

#if LIGHTS > 1 || NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD2;
#endif
    
#if LIGHTS > 2 || NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD3;
#endif
    
    float4 viewDir : TEXCOORD6; // .w = local barrel position z

    // Object-space squared distances for point-light attenuation (vanillaAttSq), bypassing
    // lightDir/light2Dir/light3Dir above -- those are tangent-space (TBN-transformed) and their
    // length is only correct if the TBN basis is orthonormal. .x = light0 (DIFFUSE/POINT only,
    // where light0 is itself a point light rather than the sun), .y = light2, .z = light3.
    // METAL_UP: .w = x of world up in tangent space (with lightDir.w and vertexColor.a, see main).
    float4 lightDistSq : TEXCOORD5;

    // TEXCOORD4 is free at LIGHTS < 4. .w carries SHADOW_VS_SENTINEL.
    float4 shadowWorldPos : TEXCOORD4;

#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD7;
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
    
        OUT.sPosition.xyzw = mul(ModelViewProj, position.xyzw);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
    
        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
    
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
    OUT.lightDir.xyz = mul(tbn, light);

    OUT.viewDir.xyz = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.viewDir.w = position.z;

    #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
        light = LightData[1].xyz - position.xyz;
        OUT.light2Dir.w = LightData[1].w;
        OUT.light2Dir.xyz = mul(tbn, light);
        OUT.lightDistSq.y = dot(light, light);
    #endif

    #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
        light = LightData[2].xyz - position.xyz;
        OUT.light3Dir.w = LightData[2].w;
        OUT.light3Dir.xyz = mul(tbn, light);
        OUT.lightDistSq.z = dot(light, light);
    #endif
    
    #ifndef NO_VERTEX_COLOR
        OUT.vertexColor = clamp(IN.vertexColor, 0.0f, 1.0f);
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

    #ifdef METAL_UP
        // UNOFFICIAL: which way is world up in this vertex's tangent space, so the pixel shader can tell how far a
        // reflected view points at the sky (Object.hlsl getMetalAmbient). The world z of an object-space direction d is
        // d . objUp, objUp = upView x ModelViewProj, with upView the camera's inverse projection times the up column of
        // its inverse view (GetShadowWorldPos's matrices; exact for the world, a little off for the first-person model
        // when its field of view differs). The spare channels carry it: lightDir.w (read only by the DIFFUSE and POINT
        // passes), lightDistSq.w and vertexColor.a (unused by the pixel shader; COLOR0 is clamped, so encoded).
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

    return OUT;
};

#elif defined(VS) && LIGHTS >= 4

// UNOFFICIAL MoreLampsPerObject (SPECULAR_LAMPS6, ShaderRecord): the specular LIGHTS 4 variants take 6 lamps, as many as the
// game then gives their passes (it capped them at 2-3 and dropped the rest: lamps popped on and off objects).
#ifndef SPECULAR_LAMPS6
    #define SPECULAR_LAMPS6 0
#endif
#if LIGHTS > 4 || (LIGHTS > 3 && defined(SPECULAR) && SPECULAR_LAMPS6)
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
    float2 uv : TEXCOORD0;
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
    
    OUT.uv = IN.uv.xy;
    
    float4 position = IN.position.xyzw;
    
    #ifndef SKIN
    float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);
    
    OUT.sPosition.xyzw = mul(ModelViewProj, position.xyzw);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
    
        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
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

    return OUT;
};
#endif // Vertex shaders.

#if defined(PS) && (!defined(LIGHTS) || LIGHTS < 4)

struct PS_INPUT {
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;    // METAL: .a = world up's z in tangent space, encoded (see the VS)
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
    float4 lightDistSq : TEXCOORD5;    // METAL: .w = world up's x in tangent space
    float4 shadowWorldPos : TEXCOORD4;
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD7;
#endif
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

#if defined(DIFFUSE) || defined(ONLY_SPECULAR)
    sampler2D NormalMap : register(s0);
#else
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
#endif

#ifdef LINEAR_LIGHTING
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 AmbientColorIn : register(c1);
        static float4 AmbientColor;	// UNOFFICIAL linear lighting: AmbientColorIn decoded (rgb), at the start of main
    #endif
    float4 PSLightColorIn[10] : register(c3);
    static float4 PSLightColor[10];	// UNOFFICIAL linear lighting: PSLightColorIn decoded (rgb; .a as it is), at the start of main
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
    float4 TESR_LampPassTrim : register(c196);   // UNOFFICIAL TrimLampPasses: x > 0 = on (pinned clear of the game's own registers)
#endif


PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;
    
#ifdef LINEAR_LIGHTING
    // UNOFFICIAL linear lighting: the light colours decoded.
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++)
        PSLightColor[lightIndex] = float4(LinColor(PSLightColorIn[lightIndex].rgb), PSLightColorIn[lightIndex].a);
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        AmbientColor = float4(LinColor(AmbientColorIn.rgb), AmbientColorIn.a);
    #endif
#endif

    // UNOFFICIAL: rain drops on the first-person weapon (includes/HudRain.hlsl), before the textures are read: they are read
    // through the drops (HUD_RAIN_TEX).
    HudRain(IN.uv.xy, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w));

#if defined(ONLY_LIGHT) && !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    float3 heatMaterialFactor = 1.0f;
#endif
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
    
        #if defined(ONLY_LIGHT)
            // Keep the sampled albedo before the lighting-only path discards it.
            // Bound the reciprocal on black texels to avoid extreme HDR values.
            heatMaterialFactor = max(baseColor.rgb, 0.03f);
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
        RmaosRead(IN.uv.xy);	// UNOFFICIAL _rmaos map (Object.hlsl): read once, here at top level
    #endif
    float roughness = getRoughness(normal.a);

    // Geometric specular AA -- see the comment on SpecularAA itself. Unconditional: this is a
    // quality fix for high-frequency normal maps (hair chief among them), not a debug toggle,
    // and ddx/ddy have to run here at top level regardless of anything below.
    roughness = SpecularAA(normal.xyz, roughness);
    // UNOFFICIAL 2026-10-13: blood on first-person geometry (Includes/HandBlood.hlsl); derivatives, so at top level.
    HandBlood(IN.uv.xy, IN.shadowWorldPos.xyz, SHADOW_VS_PRESENT(IN.shadowWorldPos.w));
    roughness = HandBloodRoughness(roughness);
    normal.xyz = HandBloodNormal(normal.xyz);
    roughness = HudRainRoughness(roughness);	// UNOFFICIAL: the rain drops, wet and rounded
    normal.xyz = HudRainNormal(normal.xyz);
    SetDiffuseEnergyKeep(normal.xyz, IN.viewDir.xyz, roughness);	// UNOFFICIAL 2026-10-16 EnergySplit (Includes/PBR.hlsl): once per pixel, every lamp reuses it

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
        baseColor.rgb = HandBloodAlbedo(baseColor.rgb);	// UNOFFICIAL: the blood's colour (after the vertex colour: blood is not tinted by it)
    #elif HAND_BLOOD && !defined(ONLY_SPECULAR)
        // UNOFFICIAL 2026-10-13 (user: the blood on the gun looks clear in some lighting): the lighting-only passes (ONLY_LIGHT,
        // and the DIFFUSE lamp passes added to them) draw the light alone, and a later pass multiplies in the gun's texture, so
        // the blood's colour above never reached them: only its gloss and bumps showed. Here the light is scaled so that,
        // once multiplied by the texture, the blood comes out its own colour.
        #if defined(DIFFUSE)
            // (No texture in these passes: the gun's texture taken as a dark grey, 0.25.)
            baseColor.rgb = handBloodCover > 0.0f ? HandBloodAlbedo((0.25f).xxx) * 4.0f : baseColor.rgb;
        #else
            baseColor.rgb = handBloodCover > 0.0f ? HandBloodAlbedo(heatMaterialFactor) / heatMaterialFactor : baseColor.rgb;	// (exactly as before where there is no blood)
        #endif
    #endif
    // UNOFFICIAL linear lighting: the albedo decoded for the lighting (where the texture is multiplied in by a later pass,
    // the factor above is, which comes out the same). The metal guess and the debug views keep the stored colour.
    const float3 albedoStored = baseColor.rgb;
    baseColor.rgb = LinColor(baseColor.rgb);
    baseColor.rgb *= HudRainLens(float3(0.0f, 0.0f, 1.0f)).x;	// UNOFFICIAL: a drop's rim bends the light away (darker)

    #ifdef METAL
        // UNOFFICIAL: metal (Object.hlsl), only while [Shaders.PBR.Metal] is on for this pass; see the lighting below.
        // Its finish, specular-anti-aliased here at top level (ddx/ddy).
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
        float3 shadowGeometricNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
        // Decline to shadow if a vanilla vertex shader ran: the interpolator is undefined.
        float sunShadow = 1.0f;
        #if FORWARD_SHADOWS
        sunShadow = SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
                  ? GetSunShadow(IN.shadowWorldPos.xyz, shadowGeometricNormal)
                  : 1.0f;
        #endif
        shadowMultiplier *= sunShadow;
    #endif

    #if INTERIOR_SHADOWS
        // UNOFFICIAL forward point-light shadows (Includes/PointShadowForward.hlsl). The normal offset uses the sun's
        // geometric normal where the shader has one. The point-light-only passes (DIFFUSE, POINT) have none and do not
        // get one: its ddx/ddy would cost more than these short passes' own work, so they rely on the depth bias alone,
        // as the screen-space pass always did.
        float3 ptWorldPos = IN.shadowWorldPos.xyz;
        #if !defined(DIFFUSE) && !defined(POINT)
            float3 ptNormal = shadowGeometricNormal;
        #else
            float3 ptNormal = 0.0f;
        #endif
        float ptValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;
        float ptShadow0 = 1.0f, ptShadow1 = 1.0f, ptShadow2 = 1.0f;
        [branch] if (NVR_PointShadowParams.x > 0.0f) {
            // Light k of the draw is PSLightColor[k]; light 0 is a point light only in the DIFFUSE and POINT passes.
            #if defined(DIFFUSE) || defined(POINT)
                ptShadow0 = POINT_SHADOW_LOOKUP(0, vanillaAttSq(IN.lightDistSq.x, IN.lightDir.w));
            #endif
            #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
                ptShadow1 = POINT_SHADOW_LOOKUP(1, vanillaAttSq(IN.lightDistSq.y, IN.light2Dir.w));
            #endif
            #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
                ptShadow2 = POINT_SHADOW_LOOKUP(2, vanillaAttSq(IN.lightDistSq.z, IN.light3Dir.w));
            #endif
        }
        float pointWeight = 0.0f;   // ONLY_SPECULAR POINT: this pass's blend weight without the point shadows (see the alpha)
    #endif

    #if defined(METAL)
        // UNOFFICIAL metal (Object.hlsl). getMetalStrength() (TESR_PBRMetal.x, or the material rule's) is the same for every pixel of a draw, so this is one cheap
        // branch: off ([Shaders.PBR.Metal] 0 for this pass) runs the old lighting below exactly. On: how much of the pixel
        // is metal; the metal share has a tinted highlight (F0 = its colour) and no diffuse light, and reflects its
        // surroundings instead of the diffuse ambient (the sky along the reflected view, by world up from the VS).
        #ifdef SI
            float3 glow = tex2D(GlowMap, IN.uv.xy).rgb;
        #endif
        float skyValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;
        #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
            float att2 = vanillaAttSq(IN.lightDistSq.y, IN.light2Dir.w);
            float3 light2Color = SHADOWED(PSLightColor[1].rgb, 1, att2);
        #endif
        #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
            float att3 = vanillaAttSq(IN.lightDistSq.z, IN.light3Dir.w);
            float3 light3Color = SHADOWED(PSLightColor[2].rgb, 2, att3);
        #endif
        float3 lighting;
        [branch] if (getMetalStrength() > 0.0f) {
            // Faded out with distance (getMetalFade); at full without the position (a vanilla vertex shader ran).
            metallic = HAND_BLOOD_METAL(getDerivedMetallic(normal.a, albedoStored) * (skyValid > 0.0f ? getMetalFade(IN.shadowWorldPos.xyz) : 1.0f));
            metalRoughness = lerp(roughness, metalRoughness, metallic);
            lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, metalRoughness, metallic);
            #ifdef SI
                lighting += baseColor.rgb * LinColor(glow.rgb * EmittanceColor.rgb);
            #endif
            lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb * (1.0f - metallic), shadowGeometricNormal, skyValid);
            [branch] if (metallic > 0.0f) {
                float3 metalView = normalize(IN.viewDir.xyz);
                float3 upTangent = float3(IN.lightDistSq.w, IN.lightDir.w, IN.vertexColor.a * 2.0f - 1.0f);
                float upLength = length(upTangent);
                bool upKnown = upLength > 0.5f && skyValid > 0.0f;
                float upReflect = upKnown ? dot(reflect(-metalView, normal.xyz), upTangent / upLength) : 0.0f;
                lighting += metallic * getMetalAmbient(AmbientColor.rgb, baseColor.rgb, metalRoughness, saturate(dot(normal.xyz, metalView)),
                                                       upReflect, upKnown ? 1.0f : 0.0f,
                                                       IN.shadowWorldPos.xyz, shadowGeometricNormal);
            }
            #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
                lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, light2Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, metalRoughness, metallic);
            #endif
            #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
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
                lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, light2Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            #endif
            #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
                lighting += getPointLightLightingAtt(IN.light3Dir.xyz, att3, light3Color, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            #endif
        }
    #else
    #if !defined(DIFFUSE) && !defined(POINT)
        float3 lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
    #else
        // Pointlights only. Attenuate from the object-space lightDistSq.x carried from the VS,
        // not length(IN.lightDir.xyz) -- that vector is tangent-space (TBN-transformed) and its
        // length is only correct if the TBN basis is orthonormal.
        float att0 = vanillaAttSq(IN.lightDistSq.x, IN.lightDir.w);
        #if INTERIOR_SHADOWS && defined(POINT)
            float3 lighting = getPointLightLightingAtt(IN.lightDir.xyz, att0, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            pointWeight += weight(lighting);
            lighting *= POINT_SHADOW(0, att0);
        #else
            float3 lighting = getPointLightLightingAtt(IN.lightDir.xyz, att0, SHADOWED(PSLightColor[0].rgb, 0, att0) * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
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
        float att2 = vanillaAttSq(IN.lightDistSq.y, IN.light2Dir.w);
        #if INTERIOR_SHADOWS && defined(POINT)
            float3 light2Term = getPointLightLightingAtt(IN.light2Dir.xyz, att2, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            pointWeight += weight(light2Term);
            lighting += light2Term * POINT_SHADOW(1, att2);
        #else
            lighting += getPointLightLightingAtt(IN.light2Dir.xyz, att2, SHADOWED(PSLightColor[1].rgb, 1, att2), IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
        #endif
    #endif

    #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
        float att3 = vanillaAttSq(IN.lightDistSq.z, IN.light3Dir.w);
        #if INTERIOR_SHADOWS && defined(POINT)
            float3 light3Term = getPointLightLightingAtt(IN.light3Dir.xyz, att3, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            pointWeight += weight(light3Term);
            lighting += light3Term * POINT_SHADOW(2, att3);
        #else
            lighting += getPointLightLightingAtt(IN.light3Dir.xyz, att3, SHADOWED(PSLightColor[2].rgb, 2, att3), IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
        #endif
    #endif

    #if defined(DIFFUSE) || defined(POINT)
        // UNOFFICIAL TrimLampPasses ([Main.Main.Performance], TESR_LampPassTrim.x, 2026-10-11): these passes add lamp light
        // on top of an object already drawn (additive, no fog, no depth write). Where the pixel is outside every lamp's
        // reach each lamp adds exactly 0 (vanillaAttSq), so the pixel is dropped instead of being blended in as 0
        // (POINT: alpha, the blend weight, is 0 there too). Not while the GunFX barrel glow is on (it adds its own colour).
        float lampReach = att0;
        #if LIGHTS > 1 || NUM_PT_LIGHTS > 1
            lampReach = max(lampReach, att2);
        #endif
        #if LIGHTS > 2 || NUM_PT_LIGHTS > 2
            lampReach = max(lampReach, att3);
        #endif
        clip((TESR_LampPassTrim.x > 0.0f && lampReach <= 0.0f && BarrelHeatMuzzle.w <= 0.0f) ? -1.0f : 0.0f);
    #endif

    #endif // METAL

    #if !defined(DIFFUSE) && !defined(POINT)
        // UNOFFICIAL: the sun focused through each rain drop into a bright spot on its far side (includes/HudRain.hlsl).
        lighting += PSLightColor[0].rgb * shadowMultiplier * baseColor.rgb * HudRainLens(IN.lightDir.xyz).y;
    #endif
    float3 finalColor = LinEncode(lighting.rgb);	// UNOFFICIAL linear lighting: back to the stored (gamma) space
    finalColor = MaterialRulesDebug(finalColor, albedoStored);   // UNOFFICIAL [Shaders.PBR.MaterialRules] DebugView
    finalColor = RmaosDebug(finalColor, albedoStored);           // UNOFFICIAL [Shaders.PBR.Rmaos] DebugView

    #ifdef METAL
        // [Shaders.PBR.Metal] DebugView: metal in orange over a dim grey image.
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
    // UNOFFICIAL DebugView 3 (PointShadowForward.hlsl PointShadowTrace): the base pass shows it, the passes that add more
    // light on top (DIFFUSE, ONLY_SPECULAR) add nothing.
    [branch] if (NVR_PointShadowDebug.x > 2.5f) {
        #if defined(DIFFUSE) || defined(ONLY_SPECULAR)
            OUT.color.rgb = 0.0f;
        #else
            OUT.color.rgb = PointShadowTrace(ptWorldPos, ptNormal, ptValid);
        #endif
    }
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
            // The same for the point lights' shadows: the weight is what the lights would give unshadowed.
            OUT.color.a = pointWeight;
        #else
            OUT.color.a = weight(finalColor.rgb);
        #endif
    #elif defined(ONLY_LIGHT)
        OUT.color.a = baseColor.a;
    #else
        OUT.color.a = baseColor.a * AmbientColor.a;
    #endif

    return OUT;
}

#elif defined(PS) && LIGHTS >= 4

// UNOFFICIAL MoreLampsPerObject (SPECULAR_LAMPS6, ShaderRecord): the specular LIGHTS 4 variants take 6 lamps, as many as the
// game then gives their passes (it capped them at 2-3 and dropped the rest: lamps popped on and off objects).
#ifndef SPECULAR_LAMPS6
    #define SPECULAR_LAMPS6 0
#endif
#if LIGHTS > 4 || (LIGHTS > 3 && defined(SPECULAR) && SPECULAR_LAMPS6)
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

struct PS_INPUT {
    float4 vertexColor : COLOR0;
    float4 fogColor : COLOR1;
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
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

struct PS_OUTPUT {
    float4 color : COLOR0;
};

sampler2D BaseMap : register(s0);
sampler2D NormalMap : register(s1);

#ifdef LINEAR_LIGHTING
    float4 AmbientColorIn : register(c1);
    static float4 AmbientColor;	// UNOFFICIAL linear lighting: as in the LIGHTS < 4 variant
    float4 PSLightColorIn[10] : register(c3);
    static float4 PSLightColor[10];
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
    // UNOFFICIAL linear lighting: as in the LIGHTS < 4 variant.
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++)
        PSLightColor[lightIndex] = float4(LinColor(PSLightColorIn[lightIndex].rgb), PSLightColorIn[lightIndex].a);
    AmbientColor = float4(LinColor(AmbientColorIn.rgb), AmbientColorIn.a);
#endif

    HudRain(IN.uv.xy, SHADOW_WP_LOAD(IN), SHADOW_WP_VALID(IN));	// UNOFFICIAL: rain drops, before the textures are read
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
        RmaosRead(IN.uv.xy);	// UNOFFICIAL _rmaos map (Object.hlsl): read once, here at top level
    #endif
    float roughness = getRoughness(normal.a);
    // UNOFFICIAL 2026-10-13 (user: polished marble walls in the casinos flickered round their lamps): the same geometric
    // specular AA as the other path above. This 4+ lamp path had none, so a glossy wall (material rules' stonepolished)
    // with a fine normal map sparkled from pixel to pixel as the view moved.
    roughness = SpecularAA(normal.xyz, roughness);
    // UNOFFICIAL 2026-10-13: blood on first-person geometry (Includes/HandBlood.hlsl); derivatives, so at top level.
    HandBlood(IN.uv.xy, SHADOW_WP_LOAD(IN), SHADOW_WP_VALID(IN));
    roughness = HandBloodRoughness(roughness);
    normal.xyz = HandBloodNormal(normal.xyz);
    roughness = HudRainRoughness(roughness);	// UNOFFICIAL: the rain drops, wet and rounded
    normal.xyz = HudRainNormal(normal.xyz);
    SetDiffuseEnergyKeep(normal.xyz, float3(IN.lightDir.w, IN.light2.w, IN.light3.w), roughness);	// UNOFFICIAL 2026-10-16 EnergySplit (Includes/PBR.hlsl): once per pixel, every lamp reuses it (view packed in .w)
    baseColor.rgb = HandBloodAlbedo(baseColor.rgb);
    const float3 albedoStored = baseColor.rgb;	// UNOFFICIAL linear lighting: as in the LIGHTS < 4 variant
    baseColor.rgb = LinColor(baseColor.rgb);
    baseColor.rgb *= HudRainLens(float3(0.0f, 0.0f, 1.0f)).x;	// UNOFFICIAL: a drop's rim bends the light away (darker)

    // Lighting.
    float3 viewDir = { IN.lightDir.w, IN.light2.w, IN.light3.w };

    #ifdef METAL
        // UNOFFICIAL metal (Object.hlsl), as in the LIGHTS < 4 variant. An object lit by four or more lamps at once is drawn
        // with this shader, which had none: metal (and its DebugView) came and went as lamps came into or out of an
        // object's reach, the player's armour walking through the TOPS casino above all (2026-10-08). Pixels with metal get
        // the lighting with the metal finish, the others the old lighting exactly (see the lighting below). The vertex
        // shaders here send no world up (no interpolator is spare at MAX_LIGHTS 6), so the reflection has no sky term;
        // indoors that term is off anyway. Finish specular-anti-aliased here at top level (ddx/ddy).
        float metalRoughness = SpecularAA(normal.xyz, getMetalRoughness(normal.a));
        float metallic = 0.0f;
        [branch] if (getMetalStrength() > 0.0f)
            metallic = HAND_BLOOD_METAL(getDerivedMetallic(normal.a, albedoStored) * (SHADOW_WP_VALID(IN) ? getMetalFade(SHADOW_WP_LOAD(IN)) : 1.0f));
        float lightRoughness = lerp(roughness, metalRoughness, metallic);
    #endif

    float att;

    #if INTERIOR_SHADOWS
        // UNOFFICIAL forward point-light shadows -- see the LIGHTS < 4 variant. One geometric normal (ddx/ddy) serves the
        // sun, the point lights and the ambient.
        float3 ptWorldPos = SHADOW_WP_LOAD(IN);
        float3 ptNormal = GetShadowGeometricNormal(ptWorldPos);
        float ptValid = SHADOW_WP_VALID(IN) ? 1.0f : 0.0f;
        // Light k of the draw is PSLightColor[k] (k = 0 is the sun's slot without OPT); the attenuations below are the
        // ones the lighting further down computes, so the compiler shares them.
        #define PT_ATT(i) vanillaAtt(PSLightPosition[i].xyz - IN.lPosition.xyz, PSLightPosition[i].w)
        float ptShadow0 = 1.0f, ptShadow1 = 1.0f, ptShadow2 = 1.0f, ptShadow3 = 1.0f, ptShadow4 = 1.0f, ptShadow5 = 1.0f;
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
                // UNOFFICIAL 2026-10-13: lightOffset, as for the lamps above (it was missing: in the OPT passes lamps 5 and 6
                // took the previous lamp's position).
                ptShadow4 = POINT_SHADOW_LOOKUP(4, PT_ATT(lightOffset + 3));
                ptShadow5 = POINT_SHADOW_LOOKUP(5, PT_ATT(lightOffset + 4));
            #endif
        }
        #undef PT_ATT
    #endif

    // Forward sun shadows -- see the LIGHTS < 4 variant. Only the OPT-off path has a sun
    // term; with OPT the first slot is a point light and must not be shadowed by the sun.
    #ifndef OPT
        float3 sunShadowWorldPos = SHADOW_WP_LOAD(IN);
        #if INTERIOR_SHADOWS
            float3 sunShadowNormal = ptNormal;
        #else
            float3 sunShadowNormal = GetShadowGeometricNormal(sunShadowWorldPos);
        #endif
        // Decline to shadow if a vanilla vertex shader ran: the interpolator is undefined.
        float sunShadow = 1.0f;
        #if FORWARD_SHADOWS
        sunShadow = SHADOW_WP_VALID(IN)
                  ? GetSunShadow(sunShadowWorldPos, sunShadowNormal)
                  : 1.0f;
        #endif
    #endif

    // ddx/ddy must stay at pixel-shader top level.
    #if INTERIOR_SHADOWS
        float3 ambNormal = ptNormal;
    #else
        float3 ambNormal = GetShadowGeometricNormal(SHADOW_WP_LOAD(IN));
    #endif

    // The sun's and the lamps' light (includes/ManyLightsDirect.hlsl), then the ambient.
    float3 lighting;
    #ifdef METAL
    [branch] if (metallic > 0.0f) {
        // UNOFFICIAL metal: the metal finish, no diffuse ambient on the metal share, which reflects its surroundings instead
        // (see the LIGHTS < 4 variant; no world up here, so no sky term).
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

    // TODO: Vanilla attenuates the full specular term by IN.lPosition.w for some reason. Is this a problem?
    float3 finalColor = LinEncode(lighting);	// UNOFFICIAL linear lighting
    finalColor = MaterialRulesDebug(finalColor, albedoStored);   // UNOFFICIAL [Shaders.PBR.MaterialRules] DebugView
    finalColor = RmaosDebug(finalColor, albedoStored);           // UNOFFICIAL [Shaders.PBR.Rmaos] DebugView

    #ifdef METAL
        // [Shaders.PBR.Metal] DebugView: metal in orange over a dim grey image (as in the LIGHTS < 4 variant).
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
        // UNOFFICIAL DebugView 3 (PointShadowForward.hlsl PointShadowTrace).
        [branch] if (NVR_PointShadowDebug.x > 2.5f) OUT.color.rgb = PointShadowTrace(ptWorldPos, ptNormal, ptValid);
    #endif
    OUT.color.a = baseColor.a * AmbientColor.a;

    return OUT;
}

#endif // Pixel shaders.
