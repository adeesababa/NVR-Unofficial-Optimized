#include "includes/Helpers.hlsl"

#if defined(VS)

struct VS_INPUT {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 vertexColor : COLOR0;
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

struct VS_OUTPUT {
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
    float4 frameX : TEXCOORD1;
    float4 frameY : TEXCOORD2;
    float4 frameZ : TEXCOORD3;
    float2 fog : TEXCOORD4;
    float3 vertexColor : COLOR0;
};

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
    float4 ObjToCubeSpace[3] : register(c8);
#else
    #include "includes/SkinHelpers.hlsl"
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 ObjToCubeSpace[3] : register(c9);
    float4 Bones[54] : register(c44);
#endif
float4 FogParam : register(c14);

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;
    float4 position = IN.position;
    #ifndef SKIN
        float3 tangent = IN.tangent, binormal = IN.binormal, normal = IN.normal;
        OUT.sPosition = mul(ModelViewProj, position);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        float3 tangent = tbn[0], binormal = tbn[1], normal = tbn[2];
        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
        OUT.sPosition = mul(SkinModelViewProj, position);
    #endif
    tangent *= 0.1f;
    binormal *= 0.1f;
    OUT.frameX.xyz = float3(dot(tangent, ObjToCubeSpace[0].xyz), dot(binormal, ObjToCubeSpace[0].xyz), dot(normal, ObjToCubeSpace[0].xyz));
    OUT.frameY.xyz = float3(dot(tangent, ObjToCubeSpace[1].xyz), dot(binormal, ObjToCubeSpace[1].xyz), dot(normal, ObjToCubeSpace[1].xyz));
    OUT.frameZ.xyz = float3(dot(tangent, ObjToCubeSpace[2].xyz), dot(binormal, ObjToCubeSpace[2].xyz), dot(normal, ObjToCubeSpace[2].xyz));
    float3 toVertex = float3(dot(ObjToCubeSpace[0], position), dot(ObjToCubeSpace[1], position), dot(ObjToCubeSpace[2], position));
    float3 view = normalize(-toVertex);
    OUT.frameX.w = view.x;
    OUT.frameY.w = view.y;
    OUT.frameZ.w = view.z;
    float3 fogPos = OUT.sPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = OUT.sPosition.w - fogPos.z;
    #endif
    OUT.fog.x = 1 - pow(1 - saturate((FogParam.x - length(fogPos)) / FogParam.y), FogParam.z);
    OUT.fog.y = length(toVertex);
    OUT.uv = IN.uv;
    OUT.vertexColor = IN.vertexColor.rgb;
    return OUT;
}

#elif defined(PS)

#include "includes/SkyAmbient.hlsl"

struct PS_INPUT {
    float2 uv : TEXCOORD0;
    float4 frameX : TEXCOORD1_centroid;
    float4 frameY : TEXCOORD2_centroid;
    float4 frameZ : TEXCOORD3_centroid;
    float2 fog : TEXCOORD4;
    float3 vertexColor : COLOR0;
};

sampler2D NormalMap : register(s0);
samplerCUBE EnvironmentCubeMap : register(s1);
sampler2D CustomEnvMask : register(s3);
float4 AmbientColor : register(c1);
float4 EnvToggles : register(c27);

float4 TESR_PBRData : register(c32);
float4 TESR_PBRExtraData : register(c33);
float4 TESR_PBRMetal : register(c34);
float4 TESR_PBREnv : register(c35);

float4 main(PS_INPUT IN) : COLOR0 {
    float4 normal = tex2D(NormalMap, IN.uv);
    float3 view = float3(IN.frameX.w, IN.frameY.w, IN.frameZ.w);
    #ifdef NEGATE_VIEW
        view = -view;
    #endif
    view = normalize(view);
    float3 n = normalize((normal.xyz - 0.5f) * 2.0f);
    float3 N = normalize(float3(dot(n, IN.frameX.xyz), dot(n, IN.frameY.xyz), dot(n, IN.frameZ.xyz)));
    float3 reflected = 2.0f * dot(N, view) * N - view * dot(N, N);
    float3 color = texCUBE(EnvironmentCubeMap, reflected).rgb;
    color *= lerp(normal.a, tex2D(CustomEnvMask, IN.uv).x, EnvToggles.w) * EnvToggles.z * AmbientColor.a;
    color = EnvToggles.x > 0.0f ? color * IN.vertexColor : color;

    float3 surroundings = AmbientColor.rgb * TESR_PBRData.w + SkyAmbientRadiance(normalize(reflected), TESR_PBRExtraData.z) * TESR_PBRExtraData.y;
    color *= lerp((1.0f).xxx, surroundings * TESR_PBREnv.y, TESR_PBREnv.x);
    float metalFade = TESR_PBRExtraData.w > 0.0f ? 1.0f - smoothstep(0.6f * TESR_PBRExtraData.w, TESR_PBRExtraData.w, IN.fog.y) : 1.0f;
    color *= lerp(1.0f, TESR_PBREnv.z, saturate(TESR_PBRMetal.x) * metalFade);
    return float4(color * IN.fog.x, 1.0f);
}

#endif
