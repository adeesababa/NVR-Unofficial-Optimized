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

#define SKIN_F0 0.028f
#define SKIN_LOBE0_ROUGHNESS 0.75f      // sharp lobe, x the material roughness
#define SKIN_LOBE1_ROUGHNESS 1.3f       // broad lobe
#define SKIN_LOBE_MIX 0.15f             // weight of the broad lobe
#define SKIN_TRANSMIT_TINT float3(1.0f, 0.28f, 0.12f)   // LINEAR colour of light through flesh
#define SKIN_TRANSMIT_DISTORTION 0.3f
#define SKIN_TRANSMIT_POWER 4.0f
#define SKIN_PENUMBRA_TINT float3(0.3f, 0.06f, 0.04f)   // light bled into a shadow edge

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
    float3 lobe0 = SkinSpecularLobe(SKIN_F0.rrr, r0, N, V, L0, light) * n0;
    float3 lobe1 = SkinSpecularLobe(SKIN_F0.rrr, r1, N, V, L1, light) * n1;
    return lerp(lobe0, lobe1, SKIN_LOBE_MIX) * skinSpecScale * skinVanillaMatch;
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

    // Transmission only through thin features (ears, nostrils, fingers: high curvature), and only
    // where the light reaches the far side of the surface (Jimenez: saturate(0.3 - N.L)).
    float thin = smoothstep(0.3f, 0.9f, skinThinness);
    float farSide = saturate(0.3f - geometricNdotL);
    float3 transmitDir = normalize(L + Nsoft * SKIN_TRANSMIT_DISTORTION);
    float back = pow(saturate(dot(V, -transmitDir)), SKIN_TRANSMIT_POWER);
    float3 transmitted = albedo * skinLinearTint(SKIN_TRANSMIT_TINT) * (back * thin * farSide * TESR_SkinLightData.w * shadow);

    specular = SkinSpecular(N, V, L, light, sun) * shadow;
    return (diffuse + transmitted + SkinVanillaFill(N, V, L, albedo) * shadow) * light;
}

// Karis' analytic fit to the split-sum environment BRDF (Unreal Engine 4 mobile).
float3 SkinEnvBRDF(float3 f0, float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
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

    float3 reflected = envBRDF * horizon * skinSpecScale * TESR_SkinLightExtra.x;
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
