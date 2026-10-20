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
#include "includes/Shadow.hlsl"

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
    float4 shadowWorldPos : TEXCOORD4;
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

float3 ToWorld(float3 v) {
    float4 viewDir = mul(mul(MVP, float4(v, 0.0f)), TESR_InvProjectionTransform);
    viewDir.w = 0.0f;
    return mul(viewDir, TESR_InvViewTransform).xyz;
}

#ifdef DIFFUSE
    #define FIRST_POINT 0
#else
    #define FIRST_POINT 1
#endif

float3 PointLightVector(int i, float3 position, float3x3 tbn) {
    float4 light = LightData[FIRST_POINT + i];
    return mul(tbn, (light.xyz - position) / light.w);
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 position = float4(IN.position.xyz, 1.0f);
    #ifndef SKIN
        float3x3 tbn = float3x3(normalize(IN.tangent), normalize(IN.binormal), normalize(IN.normal));
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
    #endif

    OUT.position = mul(MVP, position);

    float3 viewTS = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.tangent = float4(normalize(ToWorld(tbn[0])), 0.0f);
    OUT.binormal = float4(normalize(ToWorld(tbn[1])), viewTS.x);
    OUT.normal = float4(normalize(ToWorld(tbn[2])), viewTS.y);
    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.position), viewTS.z);

    #ifndef DIFFUSE
        float3 sunTS = normalize(mul(tbn, LightData[0].xyz));
    #else
        float3 sunTS = 0.0f;
    #endif
    OUT.uv = float4(IN.uv.xy, sunTS.xy);
    OUT.tangent.w = sunTS.z;

    float3 light4 = 0.0f;
    #if POINTS > 3
        light4 = PointLightVector(3, position.xyz, tbn);
    #endif
    #if POINTS > 0
        OUT.light1 = float4(PointLightVector(0, position.xyz, tbn), light4.x);
    #endif
    #if POINTS > 1
        OUT.light2 = float4(PointLightVector(1, position.xyz, tbn), light4.y);
    #endif
    #if POINTS > 2
        OUT.light3 = float4(PointLightVector(2, position.xyz, tbn), light4.z);
    #elif defined(PROJ_SHADOW)
        float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    #ifdef FOG_COLOR
        float fogStrength = saturate((FogParam.x - length(OUT.position.xyz)) / FogParam.y);
        OUT.fog = float4(FogColor, pow(1.0f - fogStrength, FogParam.z));
        OUT.color = IN.color;
    #endif

    return OUT;
}

#endif

#if defined(PS)

#include "includes/SkinLighting.hlsl"
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
    float4 tangent : TEXCOORD1;
    float4 binormal : TEXCOORD2;
    float4 normal : TEXCOORD3;
    float4 shadowWorldPos : TEXCOORD4;
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

#if FORWARD_SHADOWS
float SkinSunLitBeyond(float3 position) {
#if SHADOW_FIXED_MODE == 0
    const float bias = 0.00001f;
#elif SHADOW_FIXED_MODE == 3
    const float bias = 0.0002f;
#else
    const float bias = 0.01f;
#endif
    const bool near = length(position - TESR_ShadowNearCenter.xyz) < TESR_ShadowNearCenter.w;
    float4x4 lightTransform = near ? TESR_ShadowCameraToLightTransformNear : TESR_ShadowCameraToLightTransformMiddle;
    return saturate(SampleShadowAtlas(mul(float4(position, 1.0f), lightTransform), near ? float2(0.0f, 0.0f) : float2(0.5f, 0.0f), bias, near ? 0.1f : 0.2f));
}
#endif

float3 PointLight(int i, float3 d, float3 shadow, float3 albedo, float3 N, float3 Nsoft, float3 geometricNormal, float3 V, out float3 specular) {
    float3 lightColor = PSLightColor[FIRST_POINT + i].rgb;
    shadow *= saturate(1.0f - dot(d, d));
    #ifdef MANY
        float3 diffuse = 0.0f;
        specular = 0.0f;
        [branch] if ((FIRST_POINT + i) < EmittanceColor.a && any(shadow > 0.0f))
            diffuse = SkinLight(albedo, N, Nsoft, geometricNormal, V, d, lightColor, 1.0f, 1.0f, false, specular);
        specular *= shadow;
        return diffuse * shadow;
    #else
        float3 diffuse = SkinLight(albedo, N, Nsoft, geometricNormal, V, d, lightColor, 1.0f, 1.0f, false, specular);
        specular *= shadow;
        return diffuse * shadow;
    #endif
}

float3 SkinDebugView(float view, float3 direct, float3 ambient, float3 highlights, float3 albedo, float3 adAlbedo) {
    #if defined(DIFFUSE)
        if (view < 1.5f) return float3(0.5f, 0.0f, 0.0f);
        if (view > 2.5f && view < 3.5f) return LinEncode(direct);
        if (view > 4.5f && view < 5.5f) return LinEncode(highlights);
        return 0.0f;
    #else
        float3 onWhite = 1.0f / max(albedo, 0.02f);
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
            c = TESR_SkinDebugData.y > 0.0f ? float3(0.2f, 0.45f, 1.0f) : float3(1.0f, 0.55f, 0.1f);
            c *= SkinScreenSpaceScatter.x > 0.0f ? 1.0f : 0.3f;
        }
        else if (view < 3.5f) c = LinEncode(direct * onWhite);
        else if (view < 4.5f) c = LinEncode(ambient * onWhite);
        else if (view < 5.5f) c = LinEncode(highlights);
        else if (view < 6.5f) c = float3(skinRoughness, saturate(skinSpecScale * 0.5f), saturate((skinVanillaMatch - 1.0f) / 11.0f));
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
    float3 worldPos = IN.shadowWorldPos.xyz;

    HudRain(IN.uv.xy, worldPos, true);
    float2 uv = HudRainUV(IN.uv.xy);

    #if INTERIOR_SHADOWS && POINTS > 0
        float3 ptWorldPos = worldPos;
        float3 ptNormal = normalize(IN.normal.xyz);
        float ptValid = 1.0f;
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
        float3 adAlbedo = 1.0f;
    #elif defined(ONLY_LIGHT)
        float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        float3 adAlbedo = baseColor.rgb;
        float2 uvDx = ddx(IN.uv.xy);
        float2 uvDy = ddy(IN.uv.xy);
        [branch] if (SkinScreenSpaceScatter.y > 0.0f)
            adAlbedo = 2.0f * ((expand(tex2Dgrad(FaceGenMap0, uv, uvDx, uvDy).rgb) + baseColor.rgb) * (2.0f * tex2Dgrad(FaceGenMap1, uv, uvDx, uvDy).rgb));
        baseColor.rgb = 1.0f;
    #else
        float4 baseTexture = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        clip(AmbientColor.a >= 1 ? 0 : (baseTexture.a - Toggles.w));

        float3 faceGenVar = HUD_RAIN_TEX(FaceGenMap0, IN.uv.xy).rgb;
        float3 faceGenBlend = HUD_RAIN_TEX(FaceGenMap1, IN.uv.xy).rgb;
        float4 baseColor = float4(2.0f * ((expand(faceGenVar) + baseTexture.rgb) * (2.0f * faceGenBlend)), baseTexture.a);
        baseColor.rgb = Toggles.x <= 0.0f ? baseColor.rgb : baseColor.rgb * IN.color.rgb;
        float3 adAlbedo = 1.0f;
    #endif

    float4 normalTexel = HUD_RAIN_TEX(NormalMap, IN.uv.xy);
    float3 N = HudRainNormal(normalize(expand(normalTexel.xyz)));
    float3 Nsoft = normalize(expand(tex2Dbias(NormalMap, float4(uv, 0.0f, 2.0f)).xyz));
    const float3 geometricNormal = float3(0.0f, 0.0f, 1.0f);
    float3 V = normalize(float3(IN.binormal.w, IN.normal.w, IN.shadowWorldPos.w));

    setupSkin(N, IN.normal.xyz, worldPos, normalTexel.a, adAlbedo);
    skinRoughness = HudRainRoughness(skinRoughness);
    #if HUD_RAIN
        skinSpecScale = lerp(skinSpecScale, max(skinSpecScale, 1.0f), hudRainGloss);
    #endif
    setupSkinCS(N, V, normalize(N.x * IN.tangent.xyz + N.y * IN.binormal.xyz + N.z * IN.normal.xyz));
    float3 albedo = LinColor(baseColor.rgb);

    #if !defined(DIFFUSE)
        #if FORWARD_SHADOWS
            float3 shadowNormal = GetShadowGeometricNormal(worldPos);
            float sunShadow = GetSunShadow(worldPos, shadowNormal);
            float sunShadowBeyond = 1.0f;
            [branch] if (sunShadow < 1.0f && normalize(float3(IN.uv.zw, IN.tangent.w)).z < 0.05f && TESR_SkinLightExtra.z > 0.0f)
                sunShadowBeyond = SkinSunLitBeyond(worldPos + TESR_SmoothedSunDir.xyz * SKIN_SELF_SHADOW_REACH);
        #else
            float sunShadow = 1.0f;
            float sunShadowBeyond = 1.0f;
        #endif

        float3 projShadow = 1.0f;
        #ifdef PROJ_SHADOW
            float3 shadowTexel = tex2D(ShadowMap, IN.shadowUVs.xy).rgb;
            projShadow = lerp(1.0f, shadowTexel, tex2D(ShadowMaskMap, IN.shadowUVs.zw).x);
        #endif
    #endif

    float3 diffuseLight = 0.0f;
    float3 specularLight = 0.0f;
    float3 ambientLight = 0.0f;
    float3 specular;

    #if !defined(DIFFUSE)
        float3 sunDir = float3(IN.uv.zw, IN.tangent.w);
        diffuseLight += SkinLight(albedo, N, Nsoft, geometricNormal, V, sunDir, PSLightColor[0].rgb * projShadow, sunShadow, sunShadowBeyond, true, specular);
        specularLight += specular;
    #endif

    #if POINTS > 0
        diffuseLight += PointLight(0, IN.light1.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP0), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 1
        diffuseLight += PointLight(1, IN.light2.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP1), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 2
        diffuseLight += PointLight(2, IN.light3.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP2), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 3
        diffuseLight += PointLight(3, float3(IN.light1.w, IN.light2.w, IN.light3.w), SKIN_LAMP_SHADOW(SKIN_LAMP3), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif

    #if !defined(DIFFUSE)
        float3x3 worldFrame = float3x3(normalize(IN.tangent.xyz), normalize(IN.binormal.xyz), normalize(IN.normal.xyz));
        float3 worldN = normalize(mul(N, worldFrame));

        specularLight += SkinSkyReflection(-normalize(worldPos), worldFrame[2], worldN);
        ambientLight = SkinAmbient(AmbientColor.rgb, albedo, worldN, normalize(mul(Nsoft, worldFrame)));
        diffuseLight += ambientLight;
    #endif

    float4 color;
    color.rgb = LinEncode(diffuseLight + specularLight * skinCompensation);

    float3 specularShare = 0.0f;
    [branch] if (SkinScreenSpaceScatter.x > 0.0f) {
        float3 finalDiffuse = diffuseLight;
        #if defined(ONLY_LIGHT) && !defined(DIFFUSE)
            finalDiffuse *= LinColor(adAlbedo);
        #endif
        specularShare = LinEncode(finalDiffuse + specularLight) - LinEncode(finalDiffuse);
    }

    #ifdef FOG_COLOR
        bool fogOn = Toggles.y > 0.0f;
        color.rgb = fogOn ? lerp(color.rgb, IN.fog.rgb, IN.fog.a) : color.rgb;
        specularShare *= fogOn ? 1.0f - IN.fog.a : 1.0f;
    #endif

    [branch] if (TESR_SkinDebugData.x > 0.0f)
        color.rgb = SkinDebugView(TESR_SkinDebugData.x, diffuseLight - ambientLight, ambientLight, specularLight, albedo, adAlbedo);

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
