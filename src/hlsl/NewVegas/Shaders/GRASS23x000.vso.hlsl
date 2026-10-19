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
// UNOFFICIAL 2026-10-17: which pixel shader reads this one, written per draw by GrassShaders::SetForDraw (SetShadersHook); c19 is
// free (the game writes none of c16-c19 for grass). x 1 = NVR's grass PS (GRASS23x000TMS.pso, used with transparency
// multisampling), 0 = the game's own GRASS23x000.pso (no transparency multisampling, the INI default). Never written reads 0.
// UNOFFICIAL 2026-10-18 sandport: y and z carry the sandstorm's wind (SandstormEffect::GrassStorm; 0 = no storm).
float4 GrassPairing : register(c19);
// UNOFFICIAL 2026-10-19 grass collision ([Shaders.Grass.Collision], effects/GrassCollision.cpp): the push round the camera, a
// vertex texture (vertex sampler 0, bound by GrassShaders::SetForDraw). TESR_Translucency.w is 1 / its width (0 = off).
sampler2D NVR_GrassPush : register(s0);

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
    // UNOFFICIAL 2026-10-17 GrassFirst ([Shaders.Grass.Main] DrawFirst, Render.cpp namespace GrassFirst): the grass in two parts,
    // c19.w (GrassPairing.w, written by GrassShaders::SetForDraw): 1 = only the tufts before the fade start (fully opaque), drawn
    // BEFORE the land so the land's pixel shader skips the pixels they cover; 2 = only the fading tufts, drawn at the game's place
    // after the land (they blend over it, as before). Same test on the same numbers in both: every tuft is in exactly one part.
    // 0 (never written) = every tuft, the game's order. A dropped tuft is folded into one point outside the view (no pixels).
    [branch] if (GrassPairing.w > 0.5f && ((instDist > AlphaParam.z) == (GrassPairing.w < 1.5f))) {
        VS_OUTPUT gone = (VS_OUTPUT)0;
        gone.position = float4(2.0f, 2.0f, 0.5f, 1.0f);
        return gone;
    }

    float phase = (inst.x + inst.y) * 0.0078125f + WindData.w;
    phase = frac(phase * 0.159154937f + 0.5f) * 6.28318548f - 3.14159274f;
    float swing = sin(phase);
    // UNOFFICIAL 2026-10-18 sandstorm wind (sandport; ported from Physically-Based-Reloaded, Balebandro / Alejandro Martinez,
    // GPL-3.0): in a sandstorm the grass sways harder, leans downwind in gusts that travel across the field with the wind, and
    // flutters. c19.y (GrassPairing.y, written by GrassShaders::SetForDraw from SandstormEffect): the extra sway S, 0 with no
    // storm (the branch is not taken: the game's sway exactly). c19.z: the gust clock u (0..8 pi), gusts at u / 4, flutter at u.
    // No literal is added: this shader's free registers are full of them, and one more spills past c255 (shader refused).
    [branch] if (GrassPairing.y > 0.0f) {
        float gust = 0.75f + 0.25f * sin(GrassPairing.z * 0.25f - dot(inst.xy, WindData.xy) * 0.0078125f);
        float flutter = sin(GrassPairing.z + (inst.x - inst.y) * 0.0078125f);
        swing = swing * (1.0f + GrassPairing.y) + 1.5f * GrassPairing.y * (0.75f * gust + 0.25f * flutter);
    }
    float sway = swing * WindData.z * (IN.color.w * IN.color.w);

    float3 scale = (0.01f * inst.w) * ScaleMask.xyz + 1.0f;
    float3 pos = IN.position.xyz * scale + float3(sway * WindData.xy, 0.0f);

    float4 worldPos = float4(pos + inst.xyz, 1.0f);
    OUT.position = mul(ModelViewProj, worldPos);

    // UNOFFICIAL 2026-10-19 grass collision ([Shaders.Grass.Collision]; after Community Shaders' Grass Collision, GPL-3.0): the player
    // and the people and creatures near them push the grass aside, and it springs back as they pass (GrassCollision.cpp). The push
    // at a blade's tip is read where this vertex stands, from the camera-relative position the shadows rebuild; the vertex moves
    // as the sway does (its alpha squared: none at the root), and drops as it leans so the blade bends rather than stretches.
    // Only literals this shader already holds are used: its free constant registers are all taken by them.
    [branch] if (TESR_Translucency.w > 0.0f) {
        float2 push = tex2Dlod(NVR_GrassPush, float4(GetShadowWorldPos(OUT.position).xy * TESR_Translucency.w + 0.5f, 0.0f, 0.0f)).xy * (IN.color.w * IN.color.w);
        float height = max(pos.z, 1e-6f);
        worldPos.xy += push;
        worldPos.z -= min(dot(push, push) * 0.5f / height, height);
        OUT.position = mul(ModelViewProj, worldPos);
    }

    float4 f = frac(inst);
    float lightScale = f.w * 0.75f + 0.25f;
    float3 orient = (f.xyz - 0.5f) * 2.0f;
    float NdotL = saturate(dot(DiffuseDir, orient));

    // UNOFFICIAL: backlit translucency joins the sun term, so the PS shadow applies to it too. Skipped at Translucency 0.
    // UNOFFICIAL 2026-10-17: the game's grass PS reads no shadow position: skip its two matrix transforms unless NVR's PS or the
    // translucency needs it.
    float3 shadowWorldPos = 0.0f;
    [branch] if (GrassPairing.x > 0.5f || TESR_Translucency.x > 0.0f) shadowWorldPos = GetShadowWorldPos(OUT.position);
    [branch] if (TESR_Translucency.x > 0.0f) {
        NdotL += FoliageTranslucency(orient, DiffuseDir, shadowWorldPos, DiffuseDir);
    }

    // UNOFFICIAL linear lighting (2026-10-14): colours and plain numbers apart. The PS decodes the colours (sun, vertex colour,
    // ambient) to linear; the blade's light scale and the sun's angle are already plain amounts, and decoding them too (they
    // used to be multiplied in here) squared them darker: the grass came out about half as bright as it should.
    if (GrassPairing.x > 0.5f) {
        OUT.ambient = AmbientColor;
        OUT.sun.xyz = IN.color.rgb * DiffuseColor;
    }
    else {
        // FIX 2026-10-17: the game's own grass PS just adds TEXCOORD4 and TEXCOORD5 and multiplies by the texture, so it needs the
        // amounts inside them, as the game's VS (and P79) wrote them. Since P80 (2026-10-14) it got the bare colours: every blade
        // in full sun on every side (too bright, flat), for everyone without transparency multisampling.
        OUT.ambient = lightScale * AmbientColor;
        OUT.sun.xyz = ((lightScale * IN.color.rgb) * NdotL) * DiffuseColor * AddlParams.x;
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
