// Grass PS -- vanilla GRASS23x000TMS.pso plus forward sun shadow.
// Shared by GRASS23x002.vso (LFS) and GRASS23x003.vso (LVS).
//
// The VS hands the lighting over already split, so only the sun is shadowed:
//   TEXCOORD4.xyz ambient    TEXCOORD5.xyz sun    TEXCOORD5.w fade    COLOR0 fog (.w amount)

#include "includes/Shadow.hlsl"
#include "includes/PBRScale.hlsl"

sampler2D DiffuseMap : register(s0);
// UNOFFICIAL 2026-10-27 [Shaders.Grass.Main] ShadowFix: w 1 = the sun shadow lookup takes the world up normal (see main).
float4 TESR_GrassScale;

struct PS_INPUT {
    float2 uv             : TEXCOORD0;
    float4 shadowWorldPos : TEXCOORD1;
    float3 ambient        : TEXCOORD4_centroid;
    float4 sun            : TEXCOORD5_centroid;   // .w = distance fade
    float4 fog            : COLOR0;               // .w = fog amount
    float2 amount         : TEXCOORD6_centroid;   // UNOFFICIAL: x ambient amount, y sun amount (plain numbers: not decoded)
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
    // UNOFFICIAL 2026-10-27 ShadowFix (tester: zebra stripes on each tuft, shaded as if the sun came from the other side): the
    // derivative normal is turned to face the CAMERA, and a grass card is two-sided, so seen with the sun behind it the normal
    // points away from the sun and the normal offset pushes the lookup into the tuft's own shadow (mirrored), and the per-triangle
    // derivatives of thin, swaying blades flip from blade to blade (stripes). Grass has no surface of its own to bias from: take the
    // ground's up normal, the same for the whole tuft, as the ground it stands on. Off: as before.
    float3 lookupNormal = TESR_GrassScale.w > 0.5f ? float3(0.0f, 0.0f, 1.0f) : shadowNormal;
    // ddx/ddy must stay at top level, outside dynamic flow control.
    sunAmount *= SHADOW_VS_PRESENT(IN.shadowWorldPos.w)
         ? GetSunShadow(IN.shadowWorldPos.xyz, lookupNormal)
         : 1.0f;
#endif

    // Same split getSunLighting/getAmbientLighting apply on the object path.
    // UNOFFICIAL linear lighting (2026-10-14): only the colours are decoded; the amounts multiply after (see the VS).
    float3 lighting = PBRLight(IN.sun.xyz) * sunAmount + PBRAmbient(IN.ambient.xyz) * IN.amount.x + SkyAmbient(shadowNormal, SHADOW_VS_PRESENT(IN.shadowWorldPos.w) ? 1.0f : 0.0f);

    float4 albedo = tex2D(DiffuseMap, IN.uv.xy);
    float3 litColor = LinEncode(lighting * LinColor(albedo.rgb));	// UNOFFICIAL linear lighting: decoded albedo, encoded result (PBRScale.hlsl decodes the light)

    OUT.color.rgb = lerp(litColor, IN.fog.rgb, IN.fog.w);
    // UNOFFICIAL grass fade: the game fades grass by scaling its alpha with distance (TEXCOORD5.w from the VS,
    // fGrassStartFadeDistance / fGrassFadeRange), but grass is alpha-TESTED, so a tuft keeps its full cut-out until
    // the scaled alpha crosses the test threshold and then vanishes in one step: the pop at the fade distance.
    // Dissolve with a screen-space dither instead: the share of pixels that survive follows the fade, so a tuft
    // thins out across the whole fade range. The cut-out alpha itself is unchanged.
    float dither = frac(52.9829189f * frac(dot(vPos, float2(0.06711056f, 0.00583715f))));
    clip(IN.sun.w - dither);
    OUT.color.a = saturate(albedo.a * 1.75f);

    return OUT;
};
