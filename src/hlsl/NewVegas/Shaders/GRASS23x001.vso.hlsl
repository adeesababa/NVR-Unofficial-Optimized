// Grass VS -- vanilla GRASS23x001.vso (BSSM_GRASS_DIRONLY_LV), plus shadowWorldPos.
// GRASS23x000 except the sun term uses the raw vertex normal. lightScale comes from frac of
// inst.w alone.
//
// REGISTER HAZARD
// As 002: InstanceData spans c20..c247, shadow matrices relocated to c248/c252.
#define SHADOW_INVPROJ_REG c248
#define SHADOW_INVVIEW_REG c252
#include "includes/Shadow.hlsl"
#include "includes/GrassWind.hlsl"

float3 DiffuseDir   : register(c0);
float3 DiffuseColor : register(c1);
float3 ScaleMask    : register(c2);
float4 WindData     : register(c4);
float4 AlphaParam   : register(c5);
float4 AmbientColor : register(c6);
float4 AddlParams   : register(c7);
row_major float4x4 ModelViewProj : register(c9);
float4 FogColor     : register(c14);
float4 FogParam     : register(c15);
float4 TESR_Translucency : register(c18);
float4 GrassPairing : register(c19);
sampler2D NVR_GrassPush : register(s0);

#ifndef GRASS_INSTANCE_COUNT
    #define GRASS_INSTANCE_COUNT 228
#endif
float4 InstanceData[GRASS_INSTANCE_COUNT] : register(c20);

struct VS_INPUT {
    float4 position : POSITION;
    float3 normal   : NORMAL;
    float4 color    : COLOR0;
    float2 uv       : TEXCOORD0;
    float4 instance : TEXCOORD1;   // .x = instance index
};

struct VS_OUTPUT {
    float4 position       : POSITION;
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float4 ambient        : TEXCOORD4;
    float4 sun            : TEXCOORD5;   // .w = distance fade
    float2 amount         : TEXCOORD6;
    float4 fog            : COLOR0;      // .w = fog amount
};

float FoliageTranslucency(float3 N, float3 Lback, float3 p, float3 Lsun) {
    float3 V = -p / max(length(p), 1e-6f);
    float back = saturate(-dot(N, Lback));
    float align = pow(saturate(dot(V, -Lsun)), TESR_Translucency.y);
    float gate = saturate(Lsun.z * TESR_Translucency.z);
    return TESR_Translucency.x * back * align * gate;
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    int idx = (int)(IN.instance.x - frac(IN.instance.x));
    float4 inst = InstanceData[idx];

    // Both distances measure (x, y, w - z), not xyz.
    float4 instClip = mul(ModelViewProj, float4(inst.xyz, 1.0f));
#ifdef REVERSED_DEPTH
    float instDist = length(float3(instClip.xy, instClip.w));
#else
    float instDist = length(float4(instClip.xy, instClip.w - instClip.z, instClip.w));
#endif
    OUT.sun.w = 1.0f - saturate((instDist - AlphaParam.z) / AlphaParam.w);
    [branch] if (GrassPairing.w > 0.5f && ((instDist > AlphaParam.z) == (GrassPairing.w < 1.5f))) {
        VS_OUTPUT gone = (VS_OUTPUT)0;
        gone.position = float4(2.0f, 2.0f, 0.5f, 1.0f);
        return gone;
    }

    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float swing = sin(phase);
    [branch] if (GrassPairing.y > 0.0f) {
        float gust = 0.75f + 0.25f * sin(GrassPairing.z * 0.25f - dot(inst.xy, WindData.xy) * 0.0078125f);
        float flutter = sin(GrassPairing.z + (inst.x - inst.y) * 0.0078125f);
        swing = swing * (1.0f + GrassPairing.y) + 1.5f * GrassPairing.y * (0.75f * gust + 0.25f * flutter);
    }
    float sway = swing * WindData.z * (IN.color.w * IN.color.w);

    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    float3 pos = IN.position.xyz * scale + float3(sway * WindData.xy, 0.0f);

    const float4 windLanes = float4(TESR_InvViewTransform._m03, TESR_InvViewTransform._m13, TESR_InvViewTransform._m23, TESR_InvViewTransform._m33);
    [branch] if (dot(windLanes.xy, windLanes.xy) > 0.0f) {
        pos = IN.position.xyz * scale + GrassWindMove(inst, windLanes, GrassPairing.z, swing, WindData,
            IN.color.w * IN.color.w, IN.position.z * scale.z);
    }

    float4 worldPos = float4(pos + inst.xyz, 1.0f);
    OUT.position = mul(ModelViewProj, worldPos);

    [branch] if (TESR_Translucency.w > 0.0f) {
        float2 pushUV = GrassCameraRelative(instClip).xy * TESR_Translucency.w;
        [branch] if (max(abs(pushUV.x), abs(pushUV.y)) < 0.5f) {
            float2 push = tex2Dlod(NVR_GrassPush, float4(pushUV + 0.5f, 0.0f, 0.0f)).xy * GrassPushWeight(pos.z);
            [branch] if (dot(push, push) > 0.0f) {
                float height = max(pos.z, 1e-6f);
                worldPos.xy += push;
                worldPos.z -= min(dot(push, push) * 0.5f / height, height);
                OUT.position = mul(ModelViewProj, worldPos);
            }
        }
    }

    float lightScale = frac(inst.w) * 0.75f + 0.25f;
    float NdotL = saturate(dot(DiffuseDir, IN.normal));

    float3 shadowWorldPos = 0.0f;
    [branch] if (GrassPairing.x > 0.5f || TESR_Translucency.x > 0.0f) shadowWorldPos = GetShadowWorldPos(OUT.position);
    [branch] if (TESR_Translucency.x > 0.0f) {
        NdotL += FoliageTranslucency(IN.normal, DiffuseDir, shadowWorldPos, DiffuseDir);
    }

    if (GrassPairing.x > 0.5f) {
        OUT.ambient = AmbientColor;
        OUT.sun.xyz = IN.color.rgb * DiffuseColor;
    }
    else {
        OUT.ambient = lightScale * AmbientColor;
        OUT.sun.xyz = ((lightScale * IN.color.rgb) * DiffuseColor) * NdotL * AddlParams.x;
    }
    OUT.amount = float2(lightScale, lightScale * NdotL * AddlParams.x);

    float fogDist = length(float3(OUT.position.xy, OUT.position.w - OUT.position.z));
    float fogStrength = 1.0f - saturate((FogParam.x - fogDist) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.w = pow(fogStrength, FogParam.z);

    OUT.uv = IN.uv;
    OUT.shadowWorldPos = float4(shadowWorldPos, SHADOW_VS_SENTINEL);

    return OUT;
};
