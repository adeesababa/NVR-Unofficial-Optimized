// Grass PS -- vanilla GRASS23x000TMS.pso plus forward sun shadow.
// Shared by GRASS23x002.vso (LFS) and GRASS23x003.vso (LVS).
//
// The VS hands the lighting over already split, so only the sun is shadowed:
//   TEXCOORD4.xyz ambient    TEXCOORD5.xyz sun    TEXCOORD5.w fade    COLOR0 fog (.w amount)

#include "includes/Shadow.hlsl"
#include "includes/PBRScale.hlsl"

sampler2D DiffuseMap : register(s0);
float4 TESR_GrassScale;
#ifdef GRASS_ALPHA_TEST
float4 AlphaTestRef : register(c3);
#endif
#ifdef GRASS_POINT_LIGHT
float4 PointLightColor : register(c2);
sampler2D AttMap : register(s1);
#endif

struct PS_INPUT {
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float3 ambient        : TEXCOORD4_centroid;
    float4 sun            : TEXCOORD5_centroid;   // .w = distance fade
    float4 fog            : COLOR0;               // .w = fog amount
#ifdef GRASS_POINT_LIGHT
    float4 att            : TEXCOORD2;
#endif
    float2 amount         : TEXCOORD6_centroid;
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

PS_OUTPUT main(PS_INPUT IN, float2 vPos : VPOS) {
    PS_OUTPUT OUT;

    float sunAmount = IN.amount.y;

    // Outside the guard: the skylight needs this normal whether or not forward shadows
    // are compiled in, and ForwardShadows is a live setting that can switch them off.
    float3 shadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos.xyz);
#if FORWARD_SHADOWS
    float3 lookupNormal = fmod(TESR_GrassScale.w, 2.0f) > 0.5f ? float3(0.0f, 0.0f, 1.0f) : shadowNormal;
    // ddx/ddy must stay at top level, outside dynamic flow control.
    sunAmount *= SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
         ? GetSunShadow(IN.shadowWorldPos.xyz, lookupNormal)
         : 1.0f;
#endif

    // Same split getSunLighting/getAmbientLighting apply on the object path.
    float skyScale = TESR_GrassScale.w > 1.5f ? IN.amount.x : 1.0f;
    float3 lighting = PBRLight(IN.sun.xyz) * sunAmount + PBRAmbient(IN.ambient.xyz) * IN.amount.x + SkyAmbient(shadowNormal, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? skyScale : 0.0f);

#ifdef GRASS_POINT_LIGHT
    float4 att = SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? IN.att : IN.shadowWorldPos;
    float lampAtt = saturate(1.0f - tex2D(AttMap, att.xy).x - tex2D(AttMap, att.zw).x);
    lighting += PBRLight(0.4f * PointLightColor.rgb) * lampAtt;
#endif
    float4 albedo = tex2D(DiffuseMap, IN.uv.xy);
    float3 litColor = LinEncode(lighting * LinColor(albedo.rgb));

    OUT.color.rgb = lerp(litColor, IN.fog.rgb, IN.fog.w);
    float dither = frac(52.9829189f * frac(dot(vPos, float2(0.06711056f, 0.00583715f))));
    clip(IN.sun.w - dither);
#ifdef GRASS_ALPHA_TEST
    OUT.color.a = albedo.a > AlphaTestRef.x ? 1.0f : 0.0f;
#else
    OUT.color.a = saturate(albedo.a * 1.75f);
#endif

    return OUT;
};
