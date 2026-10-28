
#if defined(__INTELLISENSE__)
    #define PS
    #define POINTS 2
#endif

#ifndef POINTS
    #define POINTS 0
#endif

#if defined(DIFFUSE)
    #define ONLY_LIGHT
#endif

#if defined(ONLY_LIGHT)
    #define OPT
#else
    #define FOG_COLOR
#endif

#include "includes/Helpers.hlsl"
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#define SHADOW_SKIN_SOFTNESS 1
#undef SUN_CROSSFADE
#define SUN_CROSSFADE 0
#ifdef PS
    #define SHADOW_CHARACTER_MAP_IN 1
    float4 TESR_SkinDebug : register(c195);
    #define SKIN_DEBUG_DECLARED
    #define SHADOW_REACH_ALONG_RAY (TESR_SkinDebug.z < 0.5f)
    #if defined(SMITH_CORRELATED_ON) && SMITH_CORRELATED_ON
        #define PBR_SMITH_CORRELATED 1
    #endif
#endif
#include "includes/Shadow.hlsl"

#define SKIN_SELF_SHADOW_REACH 16.0f

#if defined(VS)

#include "includes/SkinHelpers.hlsl"

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
#ifdef FOG_COLOR
    float4 color : COLOR0;
#endif
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float4 binormal : TEXCOORD2;
    float4 normal : TEXCOORD3;
    float4 worldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7;
#elif defined(PROJ_SHADOW)
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef FOG_COLOR
    float4 color : COLOR0;
    float4 fog : COLOR1;
#endif
};

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
    #define MVP ModelViewProj
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
    #define MVP SkinModelViewProj
#endif

#ifdef FOG_COLOR
    float4 FogParam : register(c14);
    float3 FogColor : register(c15);
#endif

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif

float4 EyePosition : register(c16);
float4 LightData[10] : register(c25);

#ifdef DIFFUSE
    #define FIRST_POINT 0
#else
    #define FIRST_POINT 1
#endif

float3 LampVector(int i, float3 position, float3x3 tbn) {
    const float4 lamp = LightData[FIRST_POINT + i];
    const float3 v = (lamp.xyz - position) / lamp.w;
    const float3 t = mul(tbn, v);
    return t * sqrt(dot(v, v) / max(dot(t, t), 1e-12f));
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 position = float4(IN.position.xyz, 1.0f);
    #ifndef SKIN
        const float3x3 tbn = float3x3(normalize(IN.tangent), normalize(IN.binormal), normalize(IN.normal));
    #else
        const float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        const float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
    #endif

    OUT.position = mul(MVP, position);

    const float3 worldPos = GetShadowWorldPos(OUT.position);
    const float3 viewTS = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.tangent = float4(GetShadowSmoothNormal(MVP, position.xyz, tbn[0], worldPos).xyz, 0.0f);
    OUT.binormal = float4(GetShadowSmoothNormal(MVP, position.xyz, tbn[1], worldPos).xyz, viewTS.x);
    OUT.normal = float4(GetShadowSmoothNormal(MVP, position.xyz, tbn[2], worldPos).xyz, viewTS.y);
    OUT.worldPos = float4(worldPos, viewTS.z);

    #ifndef DIFFUSE
        const float3 sunTS = normalize(mul(tbn, LightData[0].xyz));
    #else
        const float3 sunTS = 0.0f;
    #endif
    OUT.uv = float4(IN.uv.xy, sunTS.xy);
    OUT.tangent.w = sunTS.z;

    float3 lamp4 = 0.0f;
    #if POINTS > 3
        lamp4 = LampVector(3, position.xyz, tbn);
    #endif
    #if POINTS > 0
        OUT.light1 = float4(LampVector(0, position.xyz, tbn), lamp4.x);
    #endif
    #if POINTS > 1
        OUT.light2 = float4(LampVector(1, position.xyz, tbn), lamp4.y);
    #endif
    #if POINTS > 2
        OUT.light3 = float4(LampVector(2, position.xyz, tbn), lamp4.z);
    #elif defined(PROJ_SHADOW)
        const float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    #ifdef FOG_COLOR
        const float fogStrength = saturate((FogParam.x - length(OUT.position.xyz)) / FogParam.y);
        OUT.fog = float4(FogColor, pow(1.0f - fogStrength, FogParam.z));
        OUT.color = IN.color;
    #endif

    return OUT;
}

#endif

#if defined(PS)

#include "includes/Skin.hlsl"
#define POINT_SHADOW_SCALAR
#include "includes/PointShadowForward.hlsl"
#if defined(HUD_RAIN_ON) && HUD_RAIN_ON
    #define HUD_RAIN 1
#endif
#include "includes/HudRain.hlsl"

#ifdef DIFFUSE
    #define FIRST_POINT 0
    #define SKIN_LAMP0 0
    #define SKIN_LAMP1 1
    #define SKIN_LAMP2 2
    #define SKIN_LAMP3 3
#else
    #define FIRST_POINT 1
    #define SKIN_LAMP0 1
    #define SKIN_LAMP1 2
    #define SKIN_LAMP2 3
    #define SKIN_LAMP3 4
#endif
#define SKIN_LAMP_SET(k, d) SKIN_LAMP_SET_(k, d)
#define SKIN_LAMP_SET_(k, d) ptShadow##k = POINT_SHADOW_LOOKUP(k, saturate(1.0f - dot(d, d)))
#define SKIN_LAMP_SHADOW(k) POINT_SHADOW(k, 1.0f)

struct PS_INPUT {
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1_centroid;
    float4 binormal : TEXCOORD2_centroid;
    float4 normal : TEXCOORD3_centroid;
    float4 worldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5_centroid;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6_centroid;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7_centroid;
#elif defined(PROJ_SHADOW)
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef FOG_COLOR
    float4 color : COLOR0;
    float4 fog : COLOR1;
#endif
};

#if defined(DIFFUSE)
    sampler2D NormalMap : register(s0);
#elif defined(ONLY_LIGHT)
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
    sampler2D FaceGenMap0 : register(s2);
    sampler2D FaceGenMap1 : register(s3);
    #ifdef PROJ_SHADOW
        sampler2D ShadowMap : register(s5);
        sampler2D ShadowMaskMap : register(s6);
    #endif
#else
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
    sampler2D FaceGenMap0 : register(s2);
    sampler2D FaceGenMap1 : register(s3);
    #ifdef PROJ_SHADOW
        sampler2D ShadowMap : register(s6);
        sampler2D ShadowMaskMap : register(s7);
    #endif
#endif

float4 AmbientColor : register(c1);
float4 EmittanceColor : register(c2);
float4 PSLightColor[10] : register(c3);
#ifndef OPT
    float4 Toggles : register(c27);
#endif

float3 SkinLamp(int i, float3 d, float3 shadow, float3 albedo, float3 N, float3 Nsoft, float3 V, out float3 specular) {
    const float att = LampAttOther(saturate(1.0f - dot(d, d)));
    const float3 light = PBRLight(PSLightColor[FIRST_POINT + i].rgb) * att;
    float3 diffuse = 0.0f;
    specular = 0.0f;
    #ifdef MANY
        [branch] if ((FIRST_POINT + i) < EmittanceColor.a && att > 0.0f)
    #endif
        diffuse = SkinLight(albedo, N, Nsoft, normalize(d).z, V, d, light, 1.0f, 0.0f, 1.0f, specular);
    specular *= shadow;
    return diffuse * shadow;
}

float3 SkinDebugView(float view, float3 direct, float3 ambient, float3 highlights, float3 albedo, float3 adAlbedo) {
    #if defined(DIFFUSE)
        if (view < 1.5f) return float3(0.5f, 0.0f, 0.0f);
        if (view > 2.5f && view < 3.5f) return LinEncode(direct);
        if (view > 4.5f && view < 5.5f) return LinEncode(highlights);
        return 0.0f;
    #else
        const float3 onWhite = 1.0f / max(albedo, 0.02f);
        float3 c;
        if (view < 1.5f) {
            #if defined(ONLY_LIGHT)
                c = float3(1.0f, 1.0f, 0.0f);
            #elif defined(MANY)
                c = float3(0.0f, 1.0f, 1.0f);
            #else
                c = float3(0.0f, 1.0f, 0.0f);
            #endif
        }
        else if (view < 2.5f) {
            c = TESR_SkinDebug.y > 0.0f ? float3(0.2f, 0.45f, 1.0f) : float3(1.0f, 0.55f, 0.1f);
            c *= NVR_SkinDraw.x > 0.0f ? 1.0f : 0.3f;
        }
        else if (view < 3.5f) c = LinEncode(direct * onWhite);
        else if (view < 4.5f) c = LinEncode(ambient * onWhite);
        else if (view < 5.5f) c = LinEncode(highlights);
        else if (view < 6.5f) c = float3(skinRoughness, saturate(skinSpecScale * 0.5f), saturate(TESR_SkinLook.y));
        else c = float3(skinCurvature, skinThinness, 0.0f);
        #if defined(ONLY_LIGHT)
            c /= max(adAlbedo, 0.05f);
        #endif
        return c;
    #endif
}

struct PS_OUTPUT {
    float4 color : COLOR0;
    float4 scatter : COLOR1;
    float4 albedo : COLOR2;
};

PS_OUTPUT main(PS_INPUT IN) {
    const float3 worldPos = IN.worldPos.xyz;

    HudRain(IN.uv.xy, worldPos, true);
    const float2 uv = HudRainUV(IN.uv.xy);

    #if INTERIOR_SHADOWS && POINTS > 0
        const float3 ptWorldPos = worldPos;
        const float3 ptNormal = ShadowFacingNormal(IN.normal.xyz, worldPos);
        const float ptValid = 1.0f;
        float ptShadow0 = 1.0f, ptShadow1 = 1.0f, ptShadow2 = 1.0f, ptShadow3 = 1.0f, ptShadow4 = 1.0f;
        [branch] if (NVR_PointShadowParams.x > 0.0f) {
            #if POINTS > 0
                SKIN_LAMP_SET(SKIN_LAMP0, IN.light1.xyz);
            #endif
            #if POINTS > 1
                SKIN_LAMP_SET(SKIN_LAMP1, IN.light2.xyz);
            #endif
            #if POINTS > 2
                SKIN_LAMP_SET(SKIN_LAMP2, IN.light3.xyz);
            #endif
            #if POINTS > 3
                SKIN_LAMP_SET(SKIN_LAMP3, float3(IN.light1.w, IN.light2.w, IN.light3.w));
            #endif
        }
    #endif

    #if defined(DIFFUSE)
        float4 baseColor = 1.0f;
        const float3 adAlbedo = 1.0f;
    #elif defined(ONLY_LIGHT)
        float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        float3 adAlbedo = baseColor.rgb;
        const float2 uvDx = ddx(IN.uv.xy);
        const float2 uvDy = ddy(IN.uv.xy);
        [branch] if (NVR_SkinDraw.y > 0.0f)
            adAlbedo = 2.0f * ((expand(tex2Dgrad(FaceGenMap0, uv, uvDx, uvDy).rgb) + baseColor.rgb) * (2.0f * tex2Dgrad(FaceGenMap1, uv, uvDx, uvDy).rgb));
        baseColor.rgb = 1.0f;
    #else
        const float4 baseTexture = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        clip(AmbientColor.a >= 1 ? 0 : (baseTexture.a - Toggles.w));

        const float3 faceGenVar = HUD_RAIN_TEX(FaceGenMap0, IN.uv.xy).rgb;
        const float3 faceGenBlend = HUD_RAIN_TEX(FaceGenMap1, IN.uv.xy).rgb;
        float4 baseColor = float4(2.0f * ((expand(faceGenVar) + baseTexture.rgb) * (2.0f * faceGenBlend)), baseTexture.a);
        baseColor.rgb = Toggles.x <= 0.0f ? baseColor.rgb : baseColor.rgb * IN.color.rgb;
        const float3 adAlbedo = 1.0f;
    #endif

    const float4 normalTexel = HUD_RAIN_TEX(NormalMap, IN.uv.xy);
    const float3 N = HudRainNormal(normalize(expand(normalTexel.xyz)));
    const float3 Nsoft = normalize(expand(tex2Dbias(NormalMap, float4(uv, 0.0f, SKIN_SOFT_NORMAL_BIAS)).xyz));
    const float3 V = normalize(float3(IN.binormal.w, IN.normal.w, IN.worldPos.w));

    SkinSetup(N, IN.normal.xyz, worldPos, normalTexel.a, NVR_SkinDraw.x);
    skinRoughness = HudRainRoughness(skinRoughness);
    #if HUD_RAIN
        skinSpecScale = lerp(skinSpecScale, max(skinSpecScale, 1.0f), hudRainGloss);
    #endif
    const float3 albedo = LinColor(baseColor.rgb);

    #if !defined(DIFFUSE)
        const float3 sunTS = float3(IN.uv.zw, IN.tangent.w);
        float sunShadow = 1.0f;
        float sunShadowEdge = 0.0f;
        float selfLit = 1.0f;
        #if FORWARD_SHADOWS
            #if OBJECT_SMOOTH_NORMALS
                const float3 flatNormal = ShadowFacingNormal(IN.normal.xyz, worldPos);
            #else
                const float3 flatNormal = GetShadowGeometricNormal(worldPos);
            #endif
            const float3 shadowNormal = ShadowLookupNormal(flatNormal, float4(IN.normal.xyz, SHADOW_VS_SENTINEL));
            sunShadow = GetSunShadow(worldPos, shadowNormal);
            sunShadowEdge = fwidth(sunShadow);
            [branch] if (sunShadow < 1.0f && sunTS.z < 0.05f && TESR_SkinLook.z > 0.0f)
                selfLit = min(SunShadowCoarse(worldPos + TESR_SmoothedSunDir.xyz * (0.5f * SKIN_SELF_SHADOW_REACH)),
                    SunShadowCoarse(worldPos + TESR_SmoothedSunDir.xyz * SKIN_SELF_SHADOW_REACH));
        #endif
        float3 projShadow = 1.0f;
        #ifdef PROJ_SHADOW
            projShadow = lerp(1.0f, tex2D(ShadowMap, IN.shadowUVs.xy).rgb, tex2D(ShadowMaskMap, IN.shadowUVs.zw).x);
        #endif
    #endif

    float3 diffuseLight = 0.0f;
    float3 specularLight = 0.0f;
    float3 ambientLight = 0.0f;
    float3 specular;

    #if !defined(DIFFUSE)
        const float3 sunLight = PBRLight(PSLightColor[0].rgb) * projShadow;
        diffuseLight += SkinLight(albedo, N, Nsoft, sunTS.z, V, sunTS, sunLight, sunShadow, sunShadowEdge, selfLit, specular);
        specularLight += specular;
        diffuseLight += sunLight * (sunShadow * HudRainLens(sunTS).y) * albedo;
    #endif

    #if POINTS > 0
        diffuseLight += SkinLamp(0, IN.light1.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP0), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 1
        diffuseLight += SkinLamp(1, IN.light2.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP1), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 2
        diffuseLight += SkinLamp(2, IN.light3.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP2), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 3
        diffuseLight += SkinLamp(3, float3(IN.light1.w, IN.light2.w, IN.light3.w), SKIN_LAMP_SHADOW(SKIN_LAMP3), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif

    #if !defined(DIFFUSE)
        const float3x3 worldFrame = float3x3(normalize(IN.tangent.xyz), normalize(IN.binormal.xyz), normalize(IN.normal.xyz));
        const float3 worldN = normalize(mul(N, worldFrame));
        specularLight += SkinSkyReflection(-normalize(worldPos), worldFrame[2], worldN);
        ambientLight = SkinAmbient(AmbientColor.rgb, albedo, worldN, normalize(mul(Nsoft, worldFrame)));
        diffuseLight += ambientLight;
    #endif

    const float3 compensation = 1.0f / max(LinColor(adAlbedo), 0.125f);
    float4 color;
    color.rgb = LinEncode(diffuseLight + specularLight * compensation);

    float3 specularShare = 0.0f;
    [branch] if (NVR_SkinDraw.x > 0.0f) {
        float3 finalDiffuse = diffuseLight;
        #if defined(ONLY_LIGHT) && !defined(DIFFUSE)
            finalDiffuse *= LinColor(adAlbedo);
        #endif
        specularShare = LinEncode(finalDiffuse + specularLight) - LinEncode(finalDiffuse);
    }

    #ifdef FOG_COLOR
        const bool fogOn = Toggles.y > 0.0f;
        color.rgb = fogOn ? lerp(color.rgb, IN.fog.rgb, IN.fog.a) : color.rgb;
        specularShare *= fogOn ? 1.0f - IN.fog.a : 1.0f;
    #endif

    [branch] if (TESR_SkinDebug.x > 0.0f)
        color.rgb = SkinDebugView(TESR_SkinDebug.x, diffuseLight - ambientLight, ambientLight, specularLight, albedo, adAlbedo);

    #if defined(DIFFUSE)
        color.a = 1.0f;
    #elif defined(ONLY_LIGHT)
        color.a = baseColor.a;
    #else
        color.a = baseColor.a * AmbientColor.a;
    #endif

    PS_OUTPUT OUT;
    OUT.color = color;
    OUT.scatter = float4(specularShare, length(worldPos));
    #if defined(DIFFUSE)
        OUT.albedo = 1.0f;
    #elif defined(ONLY_LIGHT)
        OUT.albedo = float4(saturate(adAlbedo), luma(color.rgb * adAlbedo));
    #else
        OUT.albedo = float4(saturate(baseColor.rgb), luma(color.rgb));
    #endif
    return OUT;
}

#endif
