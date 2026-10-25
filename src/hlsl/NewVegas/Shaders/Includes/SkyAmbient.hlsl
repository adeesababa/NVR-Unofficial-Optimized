// Sky-colour ambient for the lighting shaders (objects, parallax, terrain, skin, hair, grass,
// decals).
//
// Two models, selected at COMPILE time by [Shaders.PBR.Main] SkylightingMode. ps_3_0 flattens
// runtime branches, so a switch here would make every lit pixel pay for both paths; the macro
// comes through ShaderRecord::LoadShader the same way FORWARD_SHADOWS does, and changing the
// setting alters the preprocessed source so the shader cache recompiles.
//
//   0 -- spherical harmonic irradiance (default)
//   1 -- single directional sample, the older model
//
// Both return a GAMMA-ENCODED value. The object and terrain shaders carry no gamma conversion
// anywhere, so their ambient is gamma-encoded, and SKY.pso delinearises GetSkyColor before
// writing it. Keeping both modes in that space means switching between them compares the two
// MODELS rather than confounding the comparison with a colour-space difference.
//
// The encode is sqrt(), i.e. gamma 2.0, not the sRGB curve. One instruction per channel against
// a pow, and the few percent it differs from sRGB is far smaller than the error it replaces:
// integrating encoded values instead of encoding the result cost walls about a third of their
// brightness. Both modes use the same encode so the comparison stays honest.
#ifndef SKYAMBIENT_INCLUDED
#define SKYAMBIENT_INCLUDED

#ifndef SKYLIGHTING_MODE
    #define SKYLIGHTING_MODE 0
#endif

// UNOFFICIAL SharpSky: [Shaders.PBR.Metal] SharpSky in x, 0 = the plain sky. Both modes read it. Only mode 0 has the harmonics
// to sharpen (SkyAmbientSharp), so mode 1 keeps the plain sky. c136 is the one register between the PBR block (c135) and the
// sky (c137) that no shader uses.
float4 TESR_SharpSky : register(c136);

#if SKYLIGHTING_MODE == 1

// ---------------------------------------------------------------------------
// Mode 1: one sample of the sky, in a direction leaning from straight up toward the surface
// normal, scaled by a hand-written cosine form factor.
//
// Known shortcomings, kept deliberately so the two can be compared:
//   - lerp(up, N, d) is `up` for ANY d when N is up, so directionality does nothing at all on
//     floors and terrain -- they are pinned to the zenith colour.
//   - a point sample cannot represent a hemisphere: the cosine-weighted density peaks 45
//     degrees off the normal, so the horizon band is under-weighted.
//   - the horizon falloff has to be softened below SKY.pso's own 8, or sunInfluence -- which
//     only reaches the output through terms scaled by athmosphere -- vanishes at the lifted
//     sample direction.
// ---------------------------------------------------------------------------
float4 TESR_SkyColor     : register(c137);
float4 TESR_SkyLowColor  : register(c138);
float4 TESR_HorizonColor : register(c139);
float4 TESR_SunPosition  : register(c140);
float4 TESR_SkyData      : register(c141); // x: atmosphere thickness, y: sun influence, z: sun strength
float4 TESR_SunAmount    : register(c142); // x: dayTime
float4 TESR_SunDiskColor : register(c143);
float4 TESR_SunsetColor  : register(c144);

// Helpers first: Sky.hlsl uses linearize(), and several shaders include PBRScale (and so this
// file) before their own Helpers include.
#if defined(__INTELLISENSE__)
    #include "Helpers.hlsl"
    #include "Sky.hlsl"
#else
    #include "includes/Helpers.hlsl"
    #include "includes/Sky.hlsl"
#endif

#ifndef SKY_AMBIENT_ATMOSPHERE_POW
    #define SKY_AMBIENT_ATMOSPHERE_POW 2
#endif

// Mirrors SKY.pso's derivation with the sample direction substituted for its eye ray.
float3 GetSkyRadiance(float3 dir) {
    float3 up = float3(0.0f, 0.0f, 1.0f);

    float verticality = pows(compress(dot(dir, up)), 3);
    float sunHeight = shade(TESR_SunPosition.xyz, up);
    float athmosphere = pows(1 - verticality, SKY_AMBIENT_ATMOSPHERE_POW) * TESR_SkyData.x;
    float sunInfluence = pows(compress(dot(dir, TESR_SunPosition.xyz)), 1 / TESR_SkyData.y);

    float3 sunColor = GetSunColor(sunHeight, TESR_SkyData.x, TESR_SunAmount.x,
                                  TESR_SunDiskColor.rgb, TESR_SunsetColor.rgb);

    return GetSkyColor(verticality, athmosphere, sunHeight, sunInfluence, TESR_SkyData.z,
                       TESR_SkyColor.rgb, TESR_SkyLowColor.rgb, TESR_HorizonColor.rgb, sunColor);
}

// directionality: [Shaders.PBR.*] / [Shaders.Terrain.*] SkylightingDirectionality.
float3 SkyAmbientRadiance(float3 worldNormal, float directionality) {
    float3 up = float3(0.0f, 0.0f, 1.0f);

    // lerp(up, N, d) is the ZERO vector when N points straight down and d is exactly 0.5, and
    // ceilings and undersides have precisely that normal. normalize() of it is NaN.
    float3 v = lerp(up, worldNormal, directionality);
    float len = length(v);
    float3 dir = (len > 1e-4f) ? (v / len) : up;

    // Cosine-weighted form factor for the visible hemisphere: 1 facing up, 0 facing down.
    float wSky = 0.5f * dot(worldNormal, up) + 0.5f;

    // GetSkyColor returns linear; encode to match the space the callers work in.
    return sqrt(max(GetSkyRadiance(dir), 0.0f)) * wSky;
}

// UNOFFICIAL SharpSky: no harmonics to sharpen in this mode, so the plain sky stays.
float3 SkyAmbientSharpened(float3 dir, float3 plain) {
    return plain;
}

#else

// ---------------------------------------------------------------------------
// Mode 0: order-2 spherical harmonic irradiance.
//
// SkyShaders::UpdateConstants projects the sky -- the same GetSkyColor that SKY.pso renders the
// dome with -- onto 9 coefficients once per frame and convolves them with the clamped-cosine
// kernel, so this evaluates
//
//     E(N) = INTEGRAL L(w) max(N.w, 0) dw
//
// rather than approximating it by a sample direction. The cosine kernel is a severe low-pass
// filter, so 9 coefficients carry that integral to within about 1% (Ramamoorthi & Hanrahan
// 2001).
//
// The projection runs on LINEAR radiance, which is what the integral is defined on; the
// reconstruction below is encoded.
//
// The cosine form factor, the wall/floor split and the sun-side azimuthal bias are all inherent
// to the convolution. There is no direction to lean, so `directionality` is ignored here.
// ---------------------------------------------------------------------------
float4 TESR_SkyIrradianceObjects[9] : register(c137);

// worldNormal must be the GEOMETRIC world normal, unit length.
float3 SkyAmbientRadiance(float3 worldNormal, float directionality) {
    float3 n = worldNormal;

    // Reconstruction is LINEAR irradiance; encode it. max() first because an order-2 SH fit can
    // ring slightly negative, and sqrt of a negative is NaN.
    float3 irradiance = TESR_SkyIrradianceObjects[0].rgb
         + TESR_SkyIrradianceObjects[1].rgb * n.y
         + TESR_SkyIrradianceObjects[2].rgb * n.z
         + TESR_SkyIrradianceObjects[3].rgb * n.x
         + TESR_SkyIrradianceObjects[4].rgb * (n.x * n.y)
         + TESR_SkyIrradianceObjects[5].rgb * (n.y * n.z)
         + TESR_SkyIrradianceObjects[6].rgb * (3.0f * n.z * n.z - 1.0f)
         + TESR_SkyIrradianceObjects[7].rgb * (n.x * n.z)
         + TESR_SkyIrradianceObjects[8].rgb * (n.x * n.x - n.y * n.y);

    return sqrt(max(irradiance, 0.0f));
}

// UNOFFICIAL SharpSky: the same nine coefficients evaluated as the sky's radiance along dir, not its smooth irradiance. Sky.cpp
// stores band l at (A_l / pi) of its projection (A0/pi = 1, A1/pi = 2/3, A2/pi = 1/4: the clamped cosine's bands), so undoing
// the convolution weights bands 1, 1.5 and 4 (band 0 stays 1). Order 2 rings: a plain sky reads about a quarter brighter in
// linear light at the zenith and dips below zero under the horizon, so the sum is clamped here.
float3 SkyAmbientSharp(float3 dir) {
    float3 radiance = TESR_SkyIrradianceObjects[0].rgb
         + 1.5f * (TESR_SkyIrradianceObjects[1].rgb * dir.y + TESR_SkyIrradianceObjects[2].rgb * dir.z + TESR_SkyIrradianceObjects[3].rgb * dir.x)
         + 4.0f * (TESR_SkyIrradianceObjects[4].rgb * (dir.x * dir.y) + TESR_SkyIrradianceObjects[5].rgb * (dir.y * dir.z)
                 + TESR_SkyIrradianceObjects[6].rgb * (3.0f * dir.z * dir.z - 1.0f) + TESR_SkyIrradianceObjects[7].rgb * (dir.x * dir.z)
                 + TESR_SkyIrradianceObjects[8].rgb * (dir.x * dir.x - dir.y * dir.y));
    return sqrt(max(radiance, 0.0f));
}

// UNOFFICIAL SharpSky: plain (the caller's encoded sky along dir) blended to the sharp one by TESR_SharpSky.x. Below the horizon
// the sharp value is kept no lower than the plain one, more so the further dir turns down: the sky is zero there, and the plain
// value is what the object reflected before.
float3 SkyAmbientSharpened(float3 dir, float3 plain) {
    float3 sharp = SkyAmbientSharp(dir);
    sharp = lerp(sharp, max(sharp, plain), saturate(-dir.z));
    return lerp(plain, sharp, TESR_SharpSky.x);
}

#endif

#endif
