
#include "includes/Shadow.hlsl"
#include "includes/PBRScale.hlsl"

#ifndef NEWGRASS_SHADOWS
    #define NEWGRASS_SHADOWS 1
#endif

float4 NG_Ramp0 : register(c20);
float4 NG_Ramp1 : register(c21);
float4 NG_Ramp2 : register(c22);
float4 NG_Ramp3 : register(c23);
float4 NG_PS    : register(c24);
float4 NG_PS2   : register(c25);

struct PS_INPUT {
    float4 clip     : TEXCOORD0_centroid;
    float4 params   : TEXCOORD1_centroid;
    float4 normal   : TEXCOORD2_centroid;
    float4 sunDir   : TEXCOORD3;
    float4 ambient  : TEXCOORD4_centroid;
    float4 sun      : TEXCOORD5_centroid;
    float4 up       : TEXCOORD6_centroid;
    float4 fog      : COLOR0;
};

struct PS_OUTPUT {
    float4 color : COLOR0;
};

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

    float t = saturate(IN.params.x);
    float s = t * 3.0f;
    float3 albedo = s < 1.0f ? lerp(NG_Ramp0.rgb, NG_Ramp1.rgb, s)
                  : s < 2.0f ? lerp(NG_Ramp1.rgb, NG_Ramp2.rgb, s - 1.0f)
                             : lerp(NG_Ramp2.rgb, NG_Ramp3.rgb, s - 2.0f);
    albedo *= 1.0f + (IN.params.z - 0.5f) * 2.0f * NG_PS.w;
    albedo += (NG_PS.z * t * t * t) * float3(0.10f, 0.08f, 0.0f);

    float3 camRel = GetShadowWorldPos(IN.clip);
    float3 N = normalize(IN.normal.xyz);
    N = dot(N, -camRel) < 0.0f ? -N : N;

    float3 Nlit = normalize(lerp(N, normalize(IN.up.xyz), NG_PS2.x));
    float ndl = saturate((dot(Nlit, IN.sunDir.xyz) + NG_PS.y) / (1.0f + NG_PS.y));
    float sunAmount = ndl * IN.params.w * IN.normal.w;
#if FORWARD_SHADOWS && NEWGRASS_SHADOWS
    sunAmount *= GetSunShadow(camRel + float3(0.0f, 0.0f, NG_PS.x), N);
#endif

    float3 lighting = PBRLight(IN.sun.rgb) * sunAmount + PBRAmbient(IN.ambient.rgb) * IN.params.w + SkyAmbient(N, 1.0f) * NG_PS2.y;
    float3 lit = LinEncode(lighting * LinColor(albedo));
    float3 litGame = (IN.ambient.rgb * IN.params.w + IN.sun.rgb * sunAmount) * albedo;
    lit = NG_PS2.z > 0.5f ? litGame : lit;

    OUT.color.rgb = lerp(lit, IN.fog.rgb, IN.fog.w);
    OUT.color.a = 1.0f;
    return OUT;
};
