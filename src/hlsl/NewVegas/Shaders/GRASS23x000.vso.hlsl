// Grass VS -- vanilla GRASS23x000.vso (BSSM_GRASS_DIRONLY_LF), plus shadowWorldPos.
// No per-instance rotation basis: the card sits on world axes. Sun term from the instance
// orientation packed in frac(inst).xyz.
//
// REGISTER HAZARD
// As 002: InstanceData spans c20..c247, shadow matrices relocated to c248/c252.
#define SHADOW_INVPROJ_REG c248
#define SHADOW_INVVIEW_REG c252
#include "includes/Shadow.hlsl"

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
// UNOFFICIAL: backlit translucency settings (PBR.cpp, [Shaders.Foliage.Main]). c18 is free: nothing else here uses it and
// InstanceData starts at c20.
float4 TESR_Translucency : register(c18);

#ifndef GRASS_INSTANCE_COUNT
    #define GRASS_INSTANCE_COUNT 228
#endif
float4 InstanceData[GRASS_INSTANCE_COUNT] : register(c20);

struct VS_INPUT {
    float4 position : POSITION;
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
    float2 amount         : TEXCOORD6;   // UNOFFICIAL: x ambient amount, y sun amount (plain numbers, see below)
    float4 fog            : COLOR0;      // .w = fog amount
};

// UNOFFICIAL: backlit translucency (docs/research/foliage-translucency.md). Light that reaches the eye through a blade
// from the far side: the surface faces away from the sun (back: N against Lback), the eye looks into the sun through it
// (align), and the sun is above the horizon (gate). p is the camera-relative world position, Lsun the world sun direction.
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
    // UNOFFICIAL: the vanilla fade metric length(x, y, w - z, w) assumes a standard depth buffer, where w - z is only the
    // near plane; with the reversed depth buffer this game runs, w - z is about w again, so distances came out 1.41x too
    // large and the INI fade (fGrassStartFadeDistance / fGrassFadeRange) happened at 70% of its distance over 70% of its
    // range. Measure what vanilla effectively measured.
#ifdef REVERSED_DEPTH
    float instDist = length(float3(instClip.xy, instClip.w));
#else
    float instDist = length(float4(instClip.xy, instClip.w - instClip.z, instClip.w));
#endif
    OUT.sun.w = 1.0f - saturate((instDist - AlphaParam.z) / AlphaParam.w);

    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float sway = sin(phase) * WindData.z * (IN.color.w * IN.color.w);

    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    float3 pos = IN.position.xyz * scale + float3(sway * WindData.xy, 0.0f);

    float4 worldPos = float4(pos + inst.xyz, 1.0f);
    OUT.position = mul(ModelViewProj, worldPos);

    float4 f = frac(inst);
    float lightScale = f.w * 0.75f + 0.25f;
    float3 orient = (f.xyz - 0.5f) * 2.0f;
    float NdotL = saturate(dot(DiffuseDir, orient));

    // UNOFFICIAL: backlit translucency joins the sun term, so the PS shadow applies to it too. Skipped at Translucency 0.
    float3 shadowWorldPos = GetShadowWorldPos(OUT.position);
    [branch] if (TESR_Translucency.x > 0.0f) {
        NdotL += FoliageTranslucency(orient, DiffuseDir, shadowWorldPos, DiffuseDir);
    }

    // UNOFFICIAL linear lighting (2026-10-14): colours and plain numbers apart. The PS decodes the colours (sun, vertex colour,
    // ambient) to linear; the blade's light scale and the sun's angle are already plain amounts, and decoding them too (they
    // used to be multiplied in here) squared them darker: the grass came out about half as bright as it should.
    OUT.ambient = AmbientColor;
    OUT.sun.xyz = IN.color.rgb * DiffuseColor;
    OUT.amount = float2(lightScale, lightScale * NdotL * AddlParams.x);

    float fogDist = length(float3(OUT.position.xy, OUT.position.w - OUT.position.z));
    float fogStrength = 1.0f - saturate((FogParam.x - fogDist) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.w = pow(fogStrength, FogParam.z);

    OUT.uv = IN.uv;
    OUT.shadowWorldPos = float4(shadowWorldPos, SHADOW_VS_SENTINEL);

    return OUT;
};
