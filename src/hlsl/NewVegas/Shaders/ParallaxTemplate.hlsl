// Template for PAR parallax shaders.
//
// VS
//
// AD
// PAR2008 - LIGHTS = 2, PARALLAX (AD)
// PAR2009 - LIGHTS = 2, PARALLAX, PROJ_SHADOW (AD)
// PAR2010 - LIGHTS = 3, PARALLAX (AD)
// PAR2011 - LIGHTS = 3, PARALLAX, PROJ_SHADOW (AD)
//
// ADTS
// PAR2000 - PARALLAX
// PAR2001 - PARALLAX, PROJ_SHADOW
// PAR2002 - PARALLAX, LIGHTS = 2
// PAR2003 - PARALLAX, LIGHTS = 2, PROJ_SHADOW
// PAR2004 - PARALLAX, SPECULAR
// PAR2005 - PARALLAX, SPECULAR, PROJ_SHADOW
// PAR2006 - PARALLAX, SPECULAR, LIGHTS = 2
// PAR2007 - PARALLAX, SPECULAR, LIGHTS = 2, PROJ_SHADOW
//
// Diffuse
// PAR2012 - LIGHTS = 2, PARALLAX (NO_FOG, DIFFUSE)
// PAR2013 - LIGHTS = 3, PARALLAX (NO_FOG, DIFFUSE)
//
// Specular
// PAR2015 - PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
// PAR2016 - PROJ_SHADOW, PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
// PAR2017 - POINT, PARALLAX (NO_FOG. NO_VERTEX_COLOR, SPECULAR)
// PAR2018 - POINT, NUM_PT_LIGHTS = 2, PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
// PAR2019 - POINT, NUM_PT_LIGHTS = 3, PARALLAX (NO_FOG, NO_VERTEX_COLOR, SPECULAR)
//
// Texture
// PAR2014 - PARALLAX (NO_LIGHT, NO_FOG)
//
// PS
//
// AD
// PAR2013 - LIGHTS = 2, PARALLAX (AD)
// PAR2014 - LIGHTS = 2, PARALLAX, SI (AD)
// PAR2015 - LIGHTS = 2, PARALLAX, PROJ_SHADOW (AD)
// PAR2016 - LIGHTS = 2, PARALLAX, SI, PROJ_SHADOW (AD)
// PAR2017 - LIGHTS = 3, PARALLAX (AD)
// PAR2018 - LIGHTS = 3, PARALLAX, SI (AD)
// PAR2019 - LIGHTS = 3, PARALLAX, PROJ_SHADOW (AD)
// PAR2020 - LIGHTS = 3, PARALLAX, SI, PROJ_SHADOW (AD)
//
// ADTS
// PAR2000 - PARALLAX
// PAR2001 - PARALLAX, OPT
// PAR2002 - PARALLAX, SI
// PAR2003 - PARALLAX, PROJ_SHADOW
// PAR2004 - PARALLAX, SI, PROJ_SHADOW
// PAR2005 - PARALLAX, LIGHTS = 2
// PAR2006 - PARALLAX, LIGHTS = 2, SI
// PAR2007 - PARALLAX, LIGHTS = 2, PROJ_SHADOW
// PAR2008 - PARALLAX, LIGHTS = 2, SI, PROJ_SHADOW
// PAR2009 - PARALLAX, SPECULAR
// PAR2010 - PARALLAX, SPECULAR, SI
// PAR2011 - PARALLAX, SPECULAR, PROJ_SHADOW
// PAR2012 - PARALLAX, SPECULAR, SI, PROJ_SHADOW
// PAR2029 - PARALLAX, LIGHTS = 2, SPECULAR
// PAR2030 - PARALLAX, LIGHTS = 2, SPECULAR, SI
// PAR2031 - PARALLAX, LIGHTS = 2, SPECULAR, PROJ_SHADOW
// PAR2032 - PARALLAX, LIGHTS = 2, SPECULAR, SI, PROJ_SHADOW
//
// Diffuse
// PAR2021 - LIGHTS = 2, PARALLAX (NO_FOG, NO_VERTEX_COLOR, DIFFUSE, ONLY_LIGHT, OPT)
// PAR2022 - LIGHTS = 3, PARALLAX (NO_FOG, NO_VERTEX_COLOR, DIFFUSE, ONLY_LIGHT, OPT)
//
// Specular
// PAR2024 - PARALLAX (ONLY_SPECULAR)
// PAR2025 - PARALLAX, PROJ_SHADOW (ONLY_SPECULAR)
// PAR2026 - PARALLAX, POINT (ONLY_SPECULAR)
// PAR2027 - PARALLAX, NUM_PT_LIGHTS = 2, POINT (ONLY_SPECULAR)
// PAR2028 - PARALLAX, NUM_PT_LIGHTS = 3, POINT (ONLY_SPECULAR)
//
// Texture
// PAR2023 - PARALLAX (NO_LIGHT)

#if defined(__INTELLISENSE__)
    #define VS
    #define REVERSED_DEPTH
#endif

#if defined(AD)
    #define ONLY_LIGHT
    #define OPT
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
#endif

#include "includes/Helpers.hlsl"
#include "includes/Parallax.hlsl"
// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl, [Shaders.PBR.Linear]): the pixel shaders light in linear space while
// it is on (as ObjectTemplate.hlsl). Compiled in only when on at startup (LINEAR_LIGHTING_ON, ShaderRecord::LoadShader).
#ifndef LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING_ON 0
#endif
#if defined(PS) && LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING
#endif
// UNOFFICIAL 2026-10-25 pass sums for parallax objects, as ObjectTemplate.hlsl: ExactPassSums' redirect (c210 x: the pass gives its
// linear light) and LampSums' / HighlightSums' increments (c210 y: enc(dec(s) + light) - s, s the run's copy of the frame) in the
// extra-lamp (DIFFUSE) and highlight (ONLY_SPECULAR) passes. A parallax object lit by 3 or more lamps is drawn in several passes like any
// other, but FoldHighlights never folds it and these shaders had none of the sum code, so every added pass was summed in the stored
// curve: lamp-rich parallax rubble and road pieces came out lighter than the pieces next to them (tester report, Novac, 2026-10-25).
// The copy is read on s9 in the lamp passes (the sun's atlas, which they never read) and s6 in the highlight passes (free there: the
// game's normal map s0, height map s2, attenuation s3-s5 or shadow maps s4/s5). Off (not compiled): the shaders exactly as before.
#ifndef LAMP_SUMS_ON
    #define LAMP_SUMS_ON 0
#endif
#ifndef HIGHLIGHT_SUMS_ON
    #define HIGHLIGHT_SUMS_ON 0
#endif
#if defined(PS) && defined(DIFFUSE) && LAMP_SUMS_ON && LINEAR_LIGHTING_ON
    #define NVR_LAMP_SUMS
    sampler2D NVR_LampRunStart : register(s9);
#elif defined(PS) && defined(ONLY_SPECULAR) && HIGHLIGHT_SUMS_ON && LINEAR_LIGHTING_ON
    #define NVR_LAMP_SUMS
    sampler2D NVR_LampRunStart : register(s6);
#endif
// UNOFFICIAL _rmaos maps (Object.hlsl): the main lit passes only (the lamp-only and specular-only passes take s2 for the height
// map). Compiled in only when [Shaders.PBR.Rmaos] Enabled is on at startup (RMAOS_ON, ShaderRecord::LoadShader).
#ifndef RMAOS_ON
    #define RMAOS_ON 0
#endif
#if defined(PS) && RMAOS_ON && !defined(ONLY_LIGHT) && !defined(ONLY_SPECULAR) && !defined(NO_LIGHT)
    #define RMAOS
#endif
// UNOFFICIAL 2026-10-18 SmithCorrelated: as ObjectTemplate.hlsl (Includes/PBR.hlsl). Off: exactly as before.
#if defined(PS) && defined(SMITH_CORRELATED_ON) && SMITH_CORRELATED_ON
    #define PBR_SMITH_CORRELATED SMITH_CORRELATED_ON	// 1 on, 2 the test mode (PBR.hlsl)
#endif
// UNOFFICIAL 2026-10-25 SpecularFade here too (as ObjectTemplate.hlsl; Includes/PBR.hlsl PBR_SPEC_FADE, per draw NVR_SpecFade.x from the
// pass hook, core/PbrPop.h). Users' report: crater cliffs (Mesquite Mountains crater, parallax rock) flicked between shiny black and
// matte grey as the camera moved; PbrPopLog showed them swapping BSSM_ADTS_Px <-> BSSM_ADT_Px / ADT_PxOpt at the game's highlight
// distance, and the parallax shaders kept the full highlight up to the swap. Off ([Shaders.PBR.Main] SpecularFade false): as before.
#if defined(PS) && defined(SPECULAR) && defined(SPEC_FADE_ON) && SPEC_FADE_ON
    #define PBR_SPEC_FADE
#endif
#include "includes/Object.hlsl"
#include "includes/Shadow.hlsl"

// Forward sun shadows -- see ObjectTemplate.hlsl. PAR shaders do full sun lighting but had
// no shadow term at all, so every parallax-material object rendered fully sunlit once the
// deferred sun was switched off.

struct VS_INPUT
{
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
#ifndef NO_VERTEX_COLOR
    float4 vertex_color : COLOR0;
#endif
};

struct VS_OUTPUT
{
#ifndef NO_VERTEX_COLOR
    float4 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
#ifndef NO_LIGHT
    float4 lightDir : TEXCOORD1;
    #ifdef DIFFUSE
        float4 lightAtt : TEXCOORD2;
    #endif
#endif
#if LIGHTS > 1
    float4 light2Dir : TEXCOORD3;
    float4 light2Att : TEXCOORD4;
#endif
#if LIGHTS > 2  // Only used for AD and diffuse, no need for specular check.
    float4 light3Dir : TEXCOORD5;
    float4 light3Att : TEXCOORD6;
#endif
#if NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD3;
#endif
#if NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD5;
#endif
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD8;
#endif
    float4 viewDir : TEXCOORD7;

    // TEXCOORD0-8 are taken by this template; 9 is the first free slot.
    float4 shadowWorldPos : TEXCOORD9;
};

#ifdef VS

float4 EyePosition : register(c16);
row_major float4x4 ModelViewProj : register(c0);

#ifndef NO_LIGHT
    float4 LightData[10] : register(c25);
#endif

#ifndef NO_FOG
    float3 FogColor : register(c15);
    float4 FogParam : register(c14);
#endif

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif

VS_OUTPUT main(VS_INPUT IN)
{
    VS_OUTPUT OUT;
 
    float3x3 tbn = float3x3(IN.tangent.xyz, IN.binormal.xyz, IN.normal.xyz);
    
    OUT.sPosition.xyzw = mul(ModelViewProj, IN.position.xyzw);
    OUT.uv = IN.uv.xy;
    
    float3 eye = EyePosition.xyz - IN.position.xyz;
    OUT.viewDir.xyz = mul(tbn, eye);
    OUT.viewDir.w = length(eye);
    
    #ifndef NO_VERTEX_COLOR
        OUT.vertexColor = clamp(IN.vertex_color, 0.0f, 1.0f);
    #endif
    
    #ifndef NO_LIGHT
        #ifndef POINT
            OUT.lightDir.w = LightData[0].w;
            #ifndef DIFFUSE
                OUT.lightDir.xyz = mul(tbn, LightData[0].xyz);
            #else
                float3 light = LightData[0].xyz - IN.position.xyz;
                OUT.lightDir.xyz = mul(tbn, light);
                OUT.lightAtt.w = 0.5;
                OUT.lightAtt.xyz = compress(light / LightData[0].w);
            #endif
            #if LIGHTS > 1
                float3 light2 = LightData[1].xyz - IN.position.xyz;
                OUT.light2Dir.w = LightData[1].w;
                OUT.light2Dir.xyz = mul(tbn, light2);
                OUT.light2Att.w = 0.5;
                OUT.light2Att.xyz = compress(light2 / LightData[1].w);
            #endif
            #if LIGHTS > 2
                float3 light3 = LightData[2].xyz - IN.position.xyz;
                OUT.light3Dir.w = LightData[2].w;
                OUT.light3Dir.xyz = mul(tbn, light3);
                OUT.light3Att.w = 0.5;
                OUT.light3Att.xyz = compress(light3 / LightData[2].w);
            #endif
        #else
            OUT.lightDir.w = LightData[0].w;
            OUT.lightDir.xyz = mul(tbn, LightData[0].xyz - IN.position.xyz);
            #if NUM_PT_LIGHTS > 1
                OUT.light2Dir.w = LightData[1].w;
                OUT.light2Dir.xyz = mul(tbn, LightData[1].xyz - IN.position.xyz);
            #endif
            #if NUM_PT_LIGHTS > 2
                OUT.light3Dir.w = LightData[2].w;
                OUT.light3Dir.xyz = mul(tbn, LightData[2].xyz - IN.position.xyz);
            #endif
        #endif
    #endif
    
    #ifndef NO_FOG
        float3 fogPos = OUT.sPosition.xyz;
        #ifdef REVERSED_DEPTH
            fogPos.z = OUT.sPosition.w - fogPos.z;
        #endif
        float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
        fogStrength = log2(fogStrength);  // Unclear.
        OUT.fogColor.a = exp2(fogStrength * FogParam.z);
        OUT.fogColor.rgb = FogColor.rgb;
    #endif
    
    #ifdef PROJ_SHADOW
        float shadowParam = dot(ShadowProj[3].xyzw, IN.position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, IN.position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, IN.position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.sPosition), SHADOW_VS_SENTINEL);

    return OUT;
};

#endif // Vertex shader.

struct PS_INPUT
{
#ifdef NVR_LAMP_SUMS
    float2 vpos : VPOS;	// UNOFFICIAL LampSums / HighlightSums: the pixel, to read the run's copy of the frame
#endif
#ifndef NO_VERTEX_COLOR
    float3 vertexColor : COLOR0;
#endif
#ifndef NO_FOG
    float4 fogColor : COLOR1;
#endif
    float2 uv : TEXCOORD0;
#ifndef NO_LIGHT
    float4 lightDir : TEXCOORD1_centroid;
#endif
#ifdef DIFFUSE
    float4 lightAtt : TEXCOORD2;
#endif
#if LIGHTS > 1
    float4 light2Dir : TEXCOORD3_centroid;
    float4 light2Att : TEXCOORD4;
#endif
#if LIGHTS > 2
    float4 light3Dir : TEXCOORD5_centroid;
    float4 light3Att : TEXCOORD6;
#endif
#if NUM_PT_LIGHTS > 1
    float4 light2Dir : TEXCOORD3_centroid;
#endif
#if NUM_PT_LIGHTS > 2
    float4 light3Dir : TEXCOORD5_centroid;
#endif
#ifdef PROJ_SHADOW
    float4 shadowUVs : TEXCOORD8;
#endif
    float4 viewDir : TEXCOORD7_centroid;
    float4 shadowWorldPos : TEXCOORD9;
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

#ifdef PS


#if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
    #if !defined(NO_LIGHT) && defined(LINEAR_LIGHTING)
        float4 AmbientColorIn : register(c1);
        static float4 AmbientColor;	// UNOFFICIAL linear lighting: AmbientColorIn decoded (rgb), at the start of main
    #elif !defined(NO_LIGHT)
        float4 AmbientColor : register(c1);
    #endif
    sampler2D BaseMap : register(s0);
#endif
#if !defined(NO_LIGHT)
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        sampler2D NormalMap : register(s1);
    #else
        sampler2D NormalMap : register(s0);
    #endif
#endif
#if !defined(ONLY_LIGHT) && !defined(ONLY_SPECULAR) && !defined(NO_LIGHT)
    sampler2D HeightMap : register(s3);
#else
    sampler2D HeightMap : register(s2);
#endif
#ifdef SI
float4 EmittanceColor : register(c2);
    #ifndef ONLY_LIGHT
        sampler2D GlowMap : register(s4);
    #else
        sampler2D GlowMap : register(s3);
    #endif
#endif
#if LIGHTS > 1
    #ifdef DIFFUSE
        sampler2D AttenuationMap : register(s3);
    #elif defined(ONLY_LIGHT)
        sampler2D AttenuationMap : register(s4);
    #else
        sampler2D AttenuationMap : register(s5);
    #endif
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
#if !defined(NO_LIGHT) && defined(LINEAR_LIGHTING)
    float4 PSLightColorIn[10] : register(c3);
    static float4 PSLightColor[10];	// UNOFFICIAL linear lighting: PSLightColorIn decoded (rgb; .a as it is), at the start of main
#elif !defined(NO_LIGHT)
    float4 PSLightColor[10] : register(c3);
#endif
#ifndef OPT
    float4 Toggles : register(c27);

    #define useVertexColor Toggles.x
    #define useFog Toggles.y
    #define glossPower Toggles.z
    #define alphaTestRef Toggles.w
#else
    #define glossPower 1  // OPT is never used in combination with specular in PAR.
#endif

#define	uvtile(w)		(((w) * 0.04) - 0.02)

PS_OUTPUT main(PS_INPUT IN)
{
    PS_OUTPUT OUT;

#if defined(LINEAR_LIGHTING) && !defined(NO_LIGHT)
    // UNOFFICIAL linear lighting: the light colours decoded.
    [unroll] for (int lightIndex = 0; lightIndex < 10; lightIndex++)
        PSLightColor[lightIndex] = float4(LinColor(PSLightColorIn[lightIndex].rgb), PSLightColorIn[lightIndex].a);
    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        AmbientColor = float4(LinColor(AmbientColorIn.rgb), AmbientColorIn.a);
    #endif
#endif
    
    #if !defined(ONLY_LIGHT) && !defined(ONLY_SPECULAR) && !defined(NO_LIGHT)
        float alpha = tex2D(BaseMap, IN.uv.xy).a;
    
        #ifndef OPT
            clip(AmbientColor.a >= 1 ? 0 : (alpha - alphaTestRef));
        #endif
    #endif
    
    // Parallax.
    float3 viewDir = normalize(IN.viewDir.xyz);
    float distance = IN.viewDir.w;
    
    float2 dx, dy;
    dx = ddx(IN.uv.xy);
    dy = ddy(IN.uv.xy);
    
    float2 offsetUV = getParallaxCoords(distance, IN.uv.xy, dx, dy, viewDir.xyz, HeightMap);

    #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
        float4 baseColor = tex2D(BaseMap, offsetUV.xy);
    
        #if defined(ONLY_LIGHT)
            baseColor.rgb = 1.f;
        #endif
    #else
        float4 baseColor = 1.f;
    #endif
    
    // Vertex color.
    #ifndef NO_VERTEX_COLOR
        #ifndef OPT
            // Apply vertex color if toggled.
            baseColor.xyz = (useVertexColor <= 0.0 ? baseColor.xyz : (baseColor.xyz * IN.vertexColor.rgb));
        #else
            baseColor.xyz = baseColor.xyz * IN.vertexColor.rgb;
        #endif
    #endif
    baseColor.rgb = LinColor(baseColor.rgb);	// UNOFFICIAL linear lighting: the albedo decoded for the lighting
    
    // Shadows.
    float3 shadowMultiplier = 1.0;
    #ifdef PROJ_SHADOW
        float3 shadow = tex2D(ShadowMap, IN.shadowUVs.xy).xyz;
        float shadowMask = tex2D(ShadowMaskMap, IN.shadowUVs.zw).x;
        shadowMultiplier = lerp(1, shadow, shadowMask);
    #endif
    
    #ifndef NO_LIGHT
        shadowMultiplier *= getParallaxShadowMultipler(distance, offsetUV, dx, dy, normalize(IN.lightDir.xyz), HeightMap);
    #endif

    // Forward sun shadows, folded into shadowMultiplier -- which scales PSLightColor[0]
    // (the sun) only, leaving ambient untouched. Skipped for DIFFUSE/POINT passes, which
    // carry a point light in slot 0 rather than the sun.
    // ddx/ddy must stay at top level, outside dynamic flow control.
    // Hoisted out of the shadow block below: the ambient term also needs it, and that runs for
    // DIFFUSE/POINT passes which the shadow block skips. ddx/ddy must stay at top level.
    float3 sunShadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
    float shadowWorldPosValid = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f;

    #if !defined(NO_LIGHT) && !defined(DIFFUSE) && !defined(POINT)
        #if FORWARD_SHADOWS
        shadowMultiplier *= shadowWorldPosValid
                          ? GetSunShadow(IN.shadowWorldPos.xyz, sunShadowNormal)
                          : 1.0f;
        #endif
    #endif
    
    // Lighting.
    float3 lighting;
    float finalAtt;
    
    #ifdef NO_LIGHT
        lighting = baseColor.rgb;
    #else
        float4 normal = tex2D(NormalMap, offsetUV.xy);
        normal.xyz = normalize(expand(normal.xyz));

        #ifdef RMAOS
            RmaosRead(offsetUV.xy);	// UNOFFICIAL _rmaos map (Object.hlsl)
        #endif
        float roughness = getRoughness(normal.a);
        SetDiffuseEnergyKeep(normal.xyz, IN.viewDir.xyz, roughness);	// UNOFFICIAL 2026-10-16 EnergySplit (Includes/PBR.hlsl): once per pixel, every lamp reuses it
    
        #if !defined(DIFFUSE) && !defined(POINT)
            if (TESR_ParallaxData.y)
                lighting = getSunLighting(IN.lightDir.xyz, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else
                lighting = getVanillaLightingAtt(IN.lightDir.xyz, 1.f, PSLightColor[0].rgb * shadowMultiplier, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #elif defined(DIFFUSE)
            // Pointlight vanilla att.
            if (TESR_ParallaxData.y)
                lighting = getPointLightLighting(IN.lightDir.xyz, IN.lightDir.w, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else {
                finalAtt = saturate(1 - tex2D(AttenuationMap, IN.lightAtt.xy).x - tex2D(AttenuationMap, IN.lightAtt.zw).x);
                lighting = getVanillaLightingAtt(IN.lightDir.xyz, finalAtt, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
            }
        #else
            if (TESR_ParallaxData.y)
                lighting = getPointLightLighting(IN.lightDir.xyz, IN.lightDir.w, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else
                lighting = getVanillaLighting(IN.lightDir.xyz, IN.lightDir.w, PSLightColor[0].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif
    
        // Self emmitance.
        #ifdef SI
            float3 glow = tex2D(GlowMap, IN.uv.xy).rgb;
            lighting += baseColor.rgb * LinColor(glow.rgb * EmittanceColor.rgb);
        #endif
    
        #if !defined(DIFFUSE) && !defined(ONLY_SPECULAR)
            if (TESR_ParallaxData.y)
                lighting += getAmbientLighting(AmbientColor.rgb, baseColor.rgb, sunShadowNormal, shadowWorldPosValid);
            else
                lighting += baseColor.rgb * AmbientColor.rgb;
        #endif
    
        // Other light sources.
        #if LIGHTS > 1
            finalAtt = saturate(1 - tex2D(AttenuationMap, IN.light2Att.xy).x - tex2D(AttenuationMap, IN.light2Att.zw).x);
        
            if (TESR_ParallaxData.y)
                lighting += getPointLightLightingAtt(IN.light2Dir.xyz, finalAtt, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else
                lighting += getVanillaLightingAtt(IN.light2Dir.xyz, finalAtt, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif
    
        #if LIGHTS > 2
            finalAtt = saturate(1 - tex2D(AttenuationMap, IN.light3Att.xy).x - tex2D(AttenuationMap, IN.light3Att.zw).x);
        
            if (TESR_ParallaxData.y)
                lighting += getPointLightLightingAtt(IN.light3Dir.xyz, finalAtt, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else
                lighting += getVanillaLightingAtt(IN.light3Dir.xyz, finalAtt, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif
    
        #if NUM_PT_LIGHTS > 1
            if (TESR_ParallaxData.y)
                lighting += getPointLightLighting(IN.light2Dir.xyz, IN.light2Dir.w, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else
                lighting += getVanillaLighting(IN.light2Dir.xyz, IN.light2Dir.w, PSLightColor[1].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif
    
        #if NUM_PT_LIGHTS > 2
            if (TESR_ParallaxData.y)
                lighting += getPointLightLighting(IN.light3Dir.xyz, IN.light3Dir.w, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, roughness);
            else
                lighting += getVanillaLighting(IN.light3Dir.xyz, IN.light3Dir.w, PSLightColor[2].rgb, IN.viewDir.xyz, normal.xyz, baseColor.rgb, normal.a, glossPower);
        #endif
    #endif
    
    float3 finalColor = LinEncode(lighting.rgb);	// UNOFFICIAL linear lighting: back to the stored (gamma) space, before the fog
    #if defined(ONLY_LIGHT)
        // UNOFFICIAL 2026-10-25 ExactPassSums (core/PassSums.h): while NVR redirects this run of added passes into its own buffer, the pass
        // gives its LINEAR light (as ObjectTemplate.hlsl).
        [flatten] if (TESR_PassRedirect.x > 0.5f) finalColor = max(lighting.rgb, 0.0f);
    #endif
    #ifdef NVR_LAMP_SUMS
        // UNOFFICIAL 2026-10-25 LampSums / HighlightSums (see the top): c210 y 1 during the world's run of these passes, zw 1 / the frame's size.
        [branch] if (TESR_PassRedirect.y > 0.5f) {
            const float3 runStart = max(tex2Dlod(NVR_LampRunStart, float4((IN.vpos + 0.5f) * TESR_PassRedirect.zw, 0.0f, 0.0f)).rgb, 0.0f);
            finalColor = LinEncode(LinColor(runStart) + max(lighting.rgb, 0.0f)) - runStart;
        }
    #endif
    lighting.rgb = finalColor;
    lighting.rgb = RmaosDebug(lighting.rgb, baseColor.rgb);	// UNOFFICIAL [Shaders.PBR.Rmaos] DebugView

    // Fog.
    #ifndef NO_FOG
        #ifndef OPT
            lighting.rgb = (useFog <= 0.0 ? lighting.rgb : lerp(lighting.rgb, IN.fogColor.rgb, IN.fogColor.a));
        #else
            lighting.rgb = lerp(lighting.rgb, IN.fogColor.rgb, IN.fogColor.a);
        #endif
    #endif
    
    OUT.color.rgb = lighting.rgb;
    
    #if defined(DIFFUSE) || defined(NO_LIGHT)
        OUT.color.a = 1;
    #elif defined(ONLY_SPECULAR)
        if (!TESR_ParallaxData.y)
            OUT.color.rgb = saturate(OUT.color.rgb);
        OUT.color.a = weight(lighting.rgb);
    #elif defined(ONLY_LIGHT)
        OUT.color.a = baseColor.a;
    #else
        OUT.color.a = ReflMaterialAlpha(alpha * AmbientColor.a);	// UNOFFICIAL MaterialAware reflections (Object.hlsl)
    #endif

    return OUT;
};

#endif // Pixel shader.