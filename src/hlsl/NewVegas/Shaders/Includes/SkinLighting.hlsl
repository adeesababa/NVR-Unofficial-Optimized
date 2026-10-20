// UNOFFICIAL 2026-10-15: skin lighting for SkinTemplate.hlsl ([Shaders.Skin.NewSkin] Enabled). Ported from Conor's (mrfury97)
// skin shader in his NVR testbed (github.com/mrfury97/Conor-sClaudeHell-NVR-SAL-Fork, branch testbed, commits a9ca336a,
// 5ddc6291, 2aae8ebd, a43537e9). The lighting model is his; what changed for this build:
//   - colours go through this build's linear lighting (LinColor / LinEncode, Includes/LinearLight.hlsl, [Shaders.PBR.Linear])
//     instead of his decodeColor / encodeColor, and the light and ambient strengths through PBRLight / PBRAmbient
//     (Includes/PBRScale.hlsl), as the other skin, hair and grass shaders do
//   - the sky light and reflection come from this build's SkyAmbient.hlsl (both SkylightingModes); his helpers that this
//     build's PBR.hlsl does not have are below, renamed Skin* so they cannot clash
//   - his merged lamp passes are not in this build: the merged-lamp variant (SkinLampLight) is left out
//   - registers c192-c195 (his c136/c146/c147/c152 are this build's SharpSky, sun cross-fade and contact hardening)
//   - 2026-10-19: optional shading details after Community Shaders' Advanced Skin (skyrim-community-shaders,
//     features\Skin\Shaders\Skin\Skin.hlsli, GPL-3.0), all off by default ([Shaders.Skin.NewSkin] Fuzz, EnergyBalance,
//     CreaseDamping, TransmitProfile; TESR_SkinLightCS c196): see setupSkinCS, SkinFuzz and SkinTransmittanceProfile
//
// His description:
//   - Spherical-Gaussian skin diffusion (as in The Order: 1886, Neubelt & Pettineo): each colour
//     channel's N.L is blurred by a spherical Gaussian whose width grows with the surface's
//     curvature, evaluated with Stephen Hill's fitted SG irradiance (MJP, "SG Series" part 3).
//     The widths are fitted to Penner's pre-integrated skin (2011) for d'Eon's measured
//     diffusion profile; no lookup texture. Curvature comes from screen-space derivatives of
//     the interpolated normal.
//   - Per-channel normal softening for the small-scale detail the curvature cannot see: red uses the
//     blurriest normal, blue the sharpest.
//   - Pre-integrated shadows: light bleeds into the penumbra, red farthest.
//   - Transmission (Barre-Brisebois & Bouchard 2011): red light through ears, nostrils and
//     fingers against the light, using curvature as the thickness.
//   - Dual-lobe GGX specular (Jimenez / Unreal dual specular) at skin's F0 of 0.028.
//   - Spherical-harmonic sky light, softened per channel like direct light, and a dual-lobe sky
//     reflection energy-balanced against it.
//
// Direct lighting works in whatever space the caller's vectors share (SkinTemplate uses tangent
// space); the sky terms take WORLD-space vectors.
#ifndef SKINLIGHTING_INCLUDED
#define SKINLIGHTING_INCLUDED

#if defined(__INTELLISENSE__)
    #include "PBR.hlsl"
    #include "PBRScale.hlsl"
#else
    #include "includes/PBR.hlsl"
    #include "includes/PBRScale.hlsl"
#endif

float4 TESR_SkinLightData : register(c192);   // x SpecularStrength, y Roughness, z PerPixelWidth, w Translucency
float4 TESR_SkinLightExtra : register(c193);  // x SkyReflectionScale (0 indoors), y wetness, z ShadowScatter, w the vanilla-matched highlight factor (1-12, SkinShaders::UpdateConstants)

// Set per draw by the SkinShader hook (NewVegas/Hooks/Shaders.cpp), not a TESR_ constant: x is 1
// when the skin scattering effect's target is bound for this draw, i.e. the screen-space blur
// will diffuse this skin; y is 1 when the hook has bound the FaceGen maps to a light-only pass
// (SkinTemplate.hlsl, ONLY_LIGHT). The per-pixel curvature diffusion then steps aside: doing both doubled
// the scattering and lit eyelid and lip creases, where the curvature estimate peaks, orange.
float4 SkinScreenSpaceScatter : register(c194);

// [Shaders.Skin.Debug]: x the skin shader's own DebugView (1-7, 0 off or a scattering view),
// y 1 while the Interiors settings are in use. See SkinDebugView in SkinTemplate.hlsl.
// Port: z FillLight ([Shaders.Skin.NewSkin] / [Shaders.Skin.Interiors], SkinVanillaFill below).
float4 TESR_SkinDebugData : register(c195);

// 2026-10-19 [Shaders.Skin.NewSkin], indoors and out, after Community Shaders' Advanced Skin (all 0 = off, the shader as before):
// x Fuzz (peach-fuzz sheen strength, CS's FuzzStrength), y EnergyBalance, z CreaseDamping, w TransmitProfile (1 on).
// c196: free in every skin pixel shader (c197-c199 are the object shaders' material rules, c196-c199 terrain blending's).
float4 TESR_SkinLightCS : register(c196);

#define SKIN_F0 0.028f
#define SKIN_LOBE0_ROUGHNESS 0.75f      // sharp lobe, x the material roughness
#define SKIN_LOBE1_ROUGHNESS 1.3f       // broad lobe
#define SKIN_LOBE_MIX 0.15f             // weight of the broad lobe
#define SKIN_TRANSMIT_TINT float3(1.0f, 0.28f, 0.12f)   // LINEAR colour of light through flesh
#define SKIN_TRANSMIT_DISTORTION 0.3f
#define SKIN_TRANSMIT_POWER 4.0f
#define SKIN_PENUMBRA_TINT float3(0.3f, 0.06f, 0.04f)   // light bled into a shadow edge
// Community Shaders' defaults for the details above (src\Features\Skin.h): the fuzz lobe's roughness and F0.
#define SKIN_FUZZ_ROUGHNESS 0.35f
#define SKIN_FUZZ_F0 0.045f
// TransmitProfile: the depth into Jimenez's profile at the thinnest skin (skinThinness 1, a ~1 mm radius: an ear's rim),
// growing as 1 / thinness (0.67 where Translucency starts to show, thinness 0.3): orange at the rim, deep red further in.
// The gain brings the profile's red at the rim (0.67) to the flat tint's 1.
#define SKIN_TRANSMIT_PROFILE_DEPTH 0.2f
#define SKIN_TRANSMIT_PROFILE_GAIN 1.5f

// Curvature in 1/mm (one game unit is 1/70 m), capped at 1 (a 1 mm radius), where the
// diffusion fit below was made. ScatterWidth 1 widens d'Eon's profile 2x, which reads right at
// game viewing distances (the measured width barely shows on a head).
#define SKIN_MM_PER_UNIT 14.2857f
#define SKIN_SCATTER_BASE 2.0f

static float skinRoughness = 0.5f;
static float skinSpecScale = 0.0f;
static float skinVanillaMatch = 1.0f;   // direct highlights only; see setupSkin
static float3 skinCompensation = 1.0f;  // 1 / base texture in the AD passes, else 1; the caller applies it
static float skinCurvature = 0.0f;      // 1/mm after ScatterWidth: 0 flat, 1 a 1 mm radius
static float skinThinness = 0.0f;       // the same curvature without ScatterWidth, for transmission
static float3 skinSkyReflected = 0.0f;  // the share of the sky the reflection takes (SkinSkyReflection), which the ambient leaves out
// Set by setupSkinCS: exactly 1 with CreaseDamping off, which leaves the shading as it was. The only value the details keep
// live through the lights (more overflows ps_3_0's 32 temporaries in the indoor many-lamp variants): EnergyBalance's factor
// goes into skinSpecScale, and the fuzz lobe and TransmitProfile's colour are worked out inside their own branches.
static float skinCrease = 1.0f;         // CreaseDamping: saturate(1 - CS's curvature), scales the highlights' F0

// Jorge Jimenez's transmittance profile (Separable SSS, SSSSTransmittance, as in Community Shaders' Skin.hlsli): d'Eon's six
// skin Gaussians per colour (variances in mm^2) at depth d. White at 0, orange, then red as the depth grows.
// "Uses Separable SSS. Copyright (C) 2012 by Jorge Jimenez and Diego Gutierrez."
float3 SkinTransmittanceProfile(float d) {
    float dd = -d * d;
    return float3(0.233f, 0.455f, 0.649f) * exp(dd / 0.0064f)
         + float3(0.1f, 0.336f, 0.344f) * exp(dd / 0.0484f)
         + float3(0.118f, 0.198f, 0.0f) * exp(dd / 0.187f)
         + float3(0.113f, 0.007f, 0.007f) * exp(dd / 0.567f)
         + float3(0.358f, 0.004f, 0.0f) * exp(dd / 1.99f)
         + float3(0.078f, 0.0f, 0.0f) * exp(dd / 7.41f);
}

// Call once per pixel at the TOP LEVEL of the shader: it takes derivatives.
//   normal:       shading normal (tangent space in SkinTemplate; only its derivatives are used)
//   vertexNormal: interpolated geometric normal (world), for the curvature
//   worldPos:     camera-relative world position
//   mask:         the normal map's alpha, the specular mask
//   adAlbedo:     the base texture in the AD passes, whose output the engine multiplies by it
//                 afterwards (the specular is divided by it in advance); 1 elsewhere
void setupSkin(float3 normal, float3 vertexNormal, float3 worldPos, float mask, float3 adAlbedo) {
    float wet = saturate(TESR_SkinLightExtra.y);
    float roughness = lerp(TESR_SkinLightData.y, TESR_SkinLightData.y * 0.55f, wet);   // a water film is smoother than skin
    skinRoughness = clamp(SpecularAA(normal, roughness), 0.08f, 1.0f);

    // SpecularStrength 1 reads a 50% mask as real skin at F0 0.028.
    skinSpecScale = mask * 2.0f * TESR_SkinLightData.x * (1.0f + wet);

    // Vanilla-matched highlights ([Shaders.Skin.NewSkin] VanillaMatchedHighlights): settings alone
    // decide it, so it is worked out once a frame (SkinShaders::UpdateConstants, which says how).
    skinVanillaMatch = TESR_SkinLightExtra.w;
    skinCompensation = 1.0f / max(LinColor(adAlbedo), 0.125f);

    // How fast the normal turns per millimetre of surface (Penner).
    float dN = length(fwidth(vertexNormal));
    float dP = length(fwidth(worldPos)) * SKIN_MM_PER_UNIT;
    float curvature = dN / max(dP, 1e-4f);
    skinThinness = saturate(curvature * SKIN_SCATTER_BASE);
    skinCurvature = SkinScreenSpaceScatter.x > 0.0f ? 0.0f : saturate(curvature * SKIN_SCATTER_BASE * TESR_SkinLightData.z);
}

// A LINEAR colour constant in this build's working space: gamma (the game's, about sqrt) or, with linear lighting, decoded
// from that gamma, as SkyAmbient's colours are.
float3 skinLinearTint(float3 c) { return LinColor(sqrt(c)); }

// Lazarov's fit to the split-sum environment BRDF's scale and bias (Karis, Unreal Engine 4 mobile): F0 x A + B.
float2 SkinEnvBRDFAB(float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// 2026-10-19: Community Shaders' Advanced Skin details (TESR_SkinLightCS). Call once per pixel at the TOP LEVEL, after
// setupSkin and any change to skinRoughness (it takes derivatives). N and V share a space (SkinTemplate: tangent);
// worldN is the bumped normal in world space.
void setupSkinCS(float3 N, float3 V, float3 worldN) {
    // CreaseDamping (CS: F0 x saturate(1 - curvature), Skin::CalculateCurvature): how fast the bumped normal turns from one
    // pixel to the next. Highlights fade in creases (eyelids, lips, nostrils) and on fine relief, where they sparkle.
    float3 dNdx = ddx(worldN);
    float3 dNdy = ddy(worldN);
    float curvature = length(float2(dot(dNdx, dNdx), dot(dNdy, dNdy)));
    skinCrease = TESR_SkinLightCS.z > 0.0f ? saturate(1.0f - curvature) : 1.0f;

    // EnergyBalance (CS: specular x (1 + F0 (1 / (A + B) - 1)), Karis / Fdez-Aguera multiple scattering): rough GGX lobes
    // lose the light that bounces more than once between microfacets; this puts it back. Under 1% at skin's F0. Into
    // skinSpecScale, which every highlight (lamps, sun, sky reflection) is scaled by.
    [branch] if (TESR_SkinLightCS.y > 0.0f) {
        float NdotV = saturate(abs(dot(N, V)) + 1e-5f);
        float averageRoughness = lerp(clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f), clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f), SKIN_LOBE_MIX);
        float2 ab = SkinEnvBRDFAB(averageRoughness, NdotV);
        skinSpecScale *= 1.0f + SKIN_F0 * skinCrease * (1.0f / (ab.x + ab.y) - 1.0f);
    }
}

// The colour of light through thin skin: the flat tint, or with TransmitProfile Jimenez's profile, deeper (redder) where the
// skin is thicker (thinness from the curvature, setupSkin).
float3 SkinTransmitColor() {
    float3 tint = skinLinearTint(SKIN_TRANSMIT_TINT);
    [branch] if (TESR_SkinLightCS.w > 0.0f)
        tint = skinLinearTint(SkinTransmittanceProfile(SKIN_TRANSMIT_PROFILE_DEPTH / max(skinThinness, 0.2f)) * SKIN_TRANSMIT_PROFILE_GAIN);
    return tint;
}

// Stephen Hill's fit to the irradiance of a normalised spherical Gaussian of sharpness lambda,
// per channel: N.L blurred by the lobe (MJP, "SG Series" part 3). Plain saturate(N.L) as
// lambda grows; light wraps past the terminator and the peak spreads as it shrinks.
float3 SGDiffuse(float3 NdotL, float3 lambda) {
    const float c0 = 0.36f;
    const float c1 = 1.0f / (4.0f * c0);
    float3 eml = exp(-lambda);
    float3 em2l = eml * eml;
    float3 rl = 1.0f / lambda;
    float3 scale = 1.0f + 2.0f * em2l - rl;
    float3 bias = (eml - em2l) * rl - em2l;
    float3 x = sqrt(max(1.0f - scale, 1e-8f));
    float3 x0 = c0 * NdotL;
    float3 x1 = c1 * x;
    float3 n = x0 + x1;
    float3 y = abs(x0) <= x1 ? n * n / x : saturate(NdotL);
    return saturate(scale * y + bias);
}

// Diffusion for one light, along per-channel softened normals. Each channel's lobe sharpness
// comes from the curvature c (1/mm), fitted to Penner's pre-integrated skin for d'Eon's
// profile over N.L -1..1 and radii 1-20 mm: worst error 0.07 red (radii under 2 mm, slightly
// dark), 0.016 green, 0.009 blue.
// With no curvature (the screen-space blur takes this skin, or a flat surface) the lobes are so sharp
// that the fit is plain saturate(N.L), within 0.002 at the terminator: skipped there.
float3 SkinDiffusion(float3 N, float3 Nsoft, float3 L) {
    float3 normalGreen = normalize(lerp(N, Nsoft, 0.6f));
    float3 normalBlue = normalize(lerp(N, Nsoft, 0.3f));
    float3 NdotL = float3(dot(Nsoft, L), dot(normalGreen, L), dot(normalBlue, L));

    float3 diffusion;
    [branch] if (skinCurvature > 1e-3f) {
        float c = skinCurvature;
        float3 lambda = float3(2.4f * pow(c, -1.5f), 28.0f / (c * c), 71.0f / (c * c));
        diffusion = SGDiffuse(NdotL, lambda);
    }
    else
        diffusion = saturate(NdotL);
    return diffusion;
}

// Shadow visibility with light scattered into the penumbra.
float3 SkinShadow(float shadow) {
    float penumbra = shadow * (1.0f - shadow) * 4.0f;
    return saturate(shadow + penumbra * SKIN_PENUMBRA_TINT * TESR_SkinLightExtra.z);
}

// GGX specular for one light direction, times N.L, PI and the light (Conor's PBR.hlsl SpecularLobe).
float3 SkinSpecularLobe(float3 f0, float roughness, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);
    const float3 fresnel = Fresnel(f0, (1.0).xxx, LdotH);
    return BRDF(roughness, fresnel, NdotV, NdotL, NdotH) * NdotL * lightColor * PI;
}

// Karis' representative point (Real Shading in Unreal Engine 4, 2013): the point of the sun's
// disc nearest the VIEW reflection, plus the normalisation that keeps the lobe widened to cover
// the disc from gaining energy (Conor's PBR.hlsl SunSpecularDir).
float3 SkinSunSpecularDir(float3 normal, float3 eyeDir, float3 lightDir, float roughness, out float normalization) {
    const float3 r = reflect(-eyeDir, normal);
    const float3 centerToRay = dot(lightDir, r) * r - lightDir;
    const float3 closest = lightDir + centerToRay * saturate(SUN_RADIUS / max(length(centerToRay), 1e-5f));

    const float alpha = roughness * roughness;
    const float alphaPrime = saturate(alpha + SUN_RADIUS * 0.5f);
    normalization = (alpha / alphaPrime) * (alpha / alphaPrime);

    return normalize(closest);
}

// Both specular lobes, for a point light (sun = false) or the sun's disc.
float3 SkinSpecular(float3 N, float3 V, float3 L, float3 light, bool sun) {
    float r0 = clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f);
    float r1 = clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f);

    float n0 = 1.0f;
    float n1 = 1.0f;
    float3 L0 = L;
    float3 L1 = L;
    if (sun) {
        L0 = SkinSunSpecularDir(N, V, L, r0, n0);
        L1 = SkinSunSpecularDir(N, V, L, r1, n1);
    }
    // 2026-10-19: skinCrease is 1 unless CreaseDamping is on (setupSkinCS); EnergyBalance is in skinSpecScale.
    float3 f0 = (SKIN_F0 * skinCrease).rrr;
    float3 lobe0 = SkinSpecularLobe(f0, r0, N, V, L0, light) * n0;
    float3 lobe1 = SkinSpecularLobe(f0, r1, N, V, L1, light) * n1;
    return lerp(lobe0, lobe1, SKIN_LOBE_MIX) * skinSpecScale * skinVanillaMatch;
}

// 2026-10-19 Fuzz, after Community Shaders' Advanced Skin (SkinDirectLightInput's fuzz lobe): peach fuzz, a soft sheen where both
// the light and the view skim the skin (silhouettes; checked in game 2026-10-19: scalp and jaw line). Charlie sheen distribution (Estevez & Kulla 2017), Neubelt's visibility, Schlick Fresnel at CS's
// FuzzF0 (damped in creases like the GGX lobes), CS's FuzzRoughness, and its own energy compensation as CS has it. Times PI as
// SkinSpecularLobe: this shader's lights carry no 1 / PI (diffuse is albedo x N.L x light), CS's do. Not scaled by the
// specular mask (CS scales it only by its own fuzz mask texture, which FNV skin has none of).
float3 SkinFuzz(float3 N, float3 V, float3 L, float3 light) {
    float3 H = normalize(V + L);
    float NdotL = clamp(dot(N, L), 1e-5f, 1.0f);
    float NdotV = saturate(abs(dot(N, V)) + 1e-5f);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    const float invAlpha = 1.0f / (SKIN_FUZZ_ROUGHNESS * SKIN_FUZZ_ROUGHNESS * SKIN_FUZZ_ROUGHNESS * SKIN_FUZZ_ROUGHNESS);
    float D = (2.0f + invAlpha) * pow(max(1.0f - NdotH * NdotH, 1e-6f), invAlpha * 0.5f) / (2.0f * PI);
    float G = 1.0f / (4.0f * max(NdotL + NdotV - NdotL * NdotV, 1e-5f));
    float Fc = pow(1.0f - VdotH, 5.0f);
    float F = Fc + (1.0f - Fc) * (SKIN_FUZZ_F0 * skinCrease);
    float2 ab = SkinEnvBRDFAB(SKIN_FUZZ_ROUGHNESS, NdotV);
    float energy = 1.0f + SKIN_FUZZ_F0 * (1.0f / (ab.x + ab.y) - 1.0f);
    return (D * G * F * NdotL * energy * PI * TESR_SkinLightCS.x) * light;
}

// Port (2026-10-15), FillLight (TESR_SkinDebugData.z, 0 = Conor's model alone): vanilla's skin shaders (SKIN2000-2011, this
// build's decompiled ones) light skin with two terms beyond N.L, which made faces indoors clearly brighter than Conor's
// physically based model: a wrap band past the terminator, smoothstep((N.L + 0.3) / 1.3) - smoothstep(N.L), and a rim,
// (1 - N.V)^2 x (V . -L) x 0.5, light from behind the head catching its edges. FillLight 1 adds both at vanilla's strength.
float3 SkinVanillaFill(float3 N, float3 V, float3 L, float3 albedo) {
    [branch] if (TESR_SkinDebugData.z <= 0.0f) return 0.0f;
    float NdotL = dot(N, L);
    float a = saturate((NdotL + 0.3f) * 0.769230783f);
    float b = saturate(NdotL);
    float wrap = saturate((3.0f - 2.0f * a) * a * a - (3.0f - 2.0f * b) * b * b);
    float rim = (1.0f - saturate(dot(N, V))) * (1.0f - saturate(dot(N, V))) * saturate(dot(V, -L)) * 0.5f;
    return albedo * ((wrap + rim) * TESR_SkinDebugData.z);
}

// How far toward the sun the skin template looks a second time into the shadow map, in game units
// (16 = 23 cm): past the far side of a hand or a head, so a point there is lit unless something else
// casts the shadow. See selfShadowEscape below.
#define SKIN_SELF_SHADOW_REACH 16.0f

// One light: returns the diffuse light (with transmission), and the highlight in specular, kept
// apart for the skin scattering effect. lightColor is the engine's GAMMA colour with any gamma-space factor (attenuation,
// the projected actor shadow) multiplied in; shadow is a LINEAR visibility (NVR's sun shadow).
// selfShadowEscape: the sun's visibility SKIN_SELF_SHADOW_REACH toward it (1 for lamps), which says
// whether a shadow is the surface's own (lit out there) or cast by something else (dark there too).
// albedo must already be decoded (LinColor).
float3 SkinLight(float3 albedo, float3 N, float3 Nsoft, float3 geometricNormal, float3 V, float3 L, float3 lightColor, float shadow, float selfShadowEscape, bool sun, out float3 specular) {
    float3 light = PBRLight(lightColor);
    L = normalize(L);

    float geometricNdotL = dot(geometricNormal, L);

    // Diffusion, split into plain N.L and the light scattering carries beyond it.
    float3 scattered = SkinDiffusion(N, Nsoft, L);
    float3 direct = min(scattered, saturate(dot(N, L)));
    float3 wrapped = scattered - direct;

    // The shadow map also marks the surface's own far side as shadowed, which would cut the
    // scattered wrap off at the geometric terminator. On that far side only (geometric N.L 0.05
    // to -0.1), the WRAPPED part escapes the shadow map; plain N.L always keeps it. Skin facing
    // the light inside another surface's shadow (a neck under the jaw) stays shadowed. So does the
    // terminator itself when the whole surface lies in something else's shadow (hands under a
    // roof): the second look toward the sun (selfShadowEscape) is dark there too. Without it the
    // terminator kept a red band in every cast shadow.
    float selfShadowed = saturate((0.05f - geometricNdotL) / 0.15f);
    selfShadowed = selfShadowed * selfShadowed * (3.0f - 2.0f * selfShadowed) * saturate(TESR_SkinLightExtra.z) * saturate(selfShadowEscape);
    float3 shadowColor = SkinShadow(shadow);
    float3 diffuse = albedo * (direct * shadowColor + wrapped * lerp(shadowColor, 1.0f, selfShadowed));
    // 2026-10-19 EnergyBalance (Community Shaders): the light the highlight reflects does not also enter the skin, diffuse x
    // (1 - F). Mostly at grazing angles: rims lit from behind.
    [branch] if (TESR_SkinLightCS.y > 0.0f) {
        float Fc = pow(1.0f - saturate(dot(V, normalize(V + L))), 5.0f);
        diffuse *= 1.0f - (Fc + (1.0f - Fc) * (SKIN_F0 * skinCrease));
    }

    // Transmission only through thin features (ears, nostrils, fingers: high curvature), and only
    // where the light reaches the far side of the surface (Jimenez: saturate(0.3 - N.L)).
    // Its colour (SkinTransmitColor): the flat tint, or Jimenez's profile with TransmitProfile.
    float thin = smoothstep(0.3f, 0.9f, skinThinness);
    float farSide = saturate(0.3f - geometricNdotL);
    float3 transmitDir = normalize(L + Nsoft * SKIN_TRANSMIT_DISTORTION);
    float back = pow(saturate(dot(V, -transmitDir)), SKIN_TRANSMIT_POWER);
    float3 transmitted = albedo * SkinTransmitColor() * (back * thin * farSide * TESR_SkinLightData.w * shadow);

    specular = SkinSpecular(N, V, L, light, sun);
    [branch] if (TESR_SkinLightCS.x > 0.0f)
        specular += SkinFuzz(N, V, L, light);
    specular *= shadow;
    return (diffuse + transmitted + SkinVanillaFill(N, V, L, albedo) * shadow) * light;
}

// Karis' analytic fit to the split-sum environment BRDF (Unreal Engine 4 mobile).
float3 SkinEnvBRDF(float3 f0, float roughness, float NdotV) {
    float2 ab = SkinEnvBRDFAB(roughness, NdotV);
    return f0 * ab.x + ab.y;
}

// The sky along a world direction, LINEAR, blurred toward the cosine-convolved sky by roughness (Conor's SkyAmbient.hlsl
// SkyReflectionRadiance). SkylightingMode 1 has no harmonics: it reads the sky once along the direction.
#define SKIN_SKY_GROUND 0.15f   // albedo of the ground seen in downward reflections
float3 SkinSkyRadiance(float3 r, float roughness) {
#if SKYLIGHTING_MODE == 1
    return max(GetSkyRadiance(normalize(float3(r.xy, max(r.z, 0.02f)))), 0.0f) * lerp(1.0f, SKIN_SKY_GROUND, saturate(-r.z));
#else
    float band1 = lerp(1.5f, 1.0f, roughness);
    float band2 = lerp(4.0f, 1.0f, roughness);
    float3 radiance = TESR_SkyIrradiance[0].rgb
         + band1 * (TESR_SkyIrradiance[1].rgb * r.y
                  + TESR_SkyIrradiance[2].rgb * r.z
                  + TESR_SkyIrradiance[3].rgb * r.x)
         + band2 * (TESR_SkyIrradiance[4].rgb * (r.x * r.y)
                  + TESR_SkyIrradiance[5].rgb * (r.y * r.z)
                  + TESR_SkyIrradiance[6].rgb * (3.0f * r.z * r.z - 1.0f)
                  + TESR_SkyIrradiance[7].rgb * (r.x * r.z)
                  + TESR_SkyIrradiance[8].rgb * (r.x * r.x - r.y * r.y));

    // The projection holds no radiance below the horizon, so a reflection pointing down would
    // see black. Give it the ground: what an upward facing surface receives, times an albedo.
    float3 skyOnGround = TESR_SkyIrradiance[0].rgb + TESR_SkyIrradiance[2].rgb + 2.0f * TESR_SkyIrradiance[6].rgb;
    return max(radiance, 0.0f) + max(skyOnGround, 0.0f) * (SKIN_SKY_GROUND * saturate(-r.z));
#endif
}

// Sky reflection off both lobes. Sets skinSkyReflected, so call it BEFORE SkinAmbient.
float3 SkinSkyReflection(float3 V, float3 geometricNormal, float3 N) {
    float3 r = reflect(-V, N);
    float NdotV = saturate(dot(N, V));

    float horizon = saturate(1.0f + 1.2f * dot(r, geometricNormal));
    horizon *= horizon;

    float r0 = clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f);
    float r1 = clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f);
    float3 radiance = lerp(SkinSkyRadiance(r, r0), SkinSkyRadiance(r, r1), SKIN_LOBE_MIX);
    float3 envBRDF = lerp(SkinEnvBRDF(SKIN_F0.rrr, r0, NdotV), SkinEnvBRDF(SKIN_F0.rrr, r1, NdotV), SKIN_LOBE_MIX);

    // 2026-10-19: x skinCrease (CS: the sky lobe x saturate(1 - curvature)), 1 unless CreaseDamping is on.
    float3 reflected = envBRDF * horizon * skinSpecScale * TESR_SkinLightExtra.x * skinCrease;
    // The share of the sky the ambient must not scatter too. Without the AD compensation: the
    // ambient is divided by the same texture afterwards.
    skinSkyReflected = saturate(reflected);

    // Radiance is linear: into this build's working space as SkyAmbient's colours are (gamma, decoded when linear).
    return skinLinearTint(radiance) * reflected;
}

// Weather ambient plus the sky, sampled with the sharp and softened normals and blended per
// channel the way direct light is.
float3 SkinAmbient(float3 ambient, float3 albedo, float3 N, float3 Nsoft) {
    float3 flat = PBRAmbient(ambient);

    float3 sharp = LinColor(SkyAmbientRadiance(N, TESR_PBRExtraData.z));
    float3 soft = LinColor(SkyAmbientRadiance(Nsoft, TESR_PBRExtraData.z));
    float3 sky = float3(soft.r, lerp(sharp.g, soft.g, 0.6f), lerp(sharp.b, soft.b, 0.3f)) * LinScale(TESR_PBRExtraData.y);

    return (flat + sky) * albedo * (1.0f - skinSkyReflected);
}

#endif
