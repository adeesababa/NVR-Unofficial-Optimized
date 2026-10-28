// Forward sun-shadow lookup for GAME shaders (objects, parallax, terrain).
//
// All four cascades live in one 2x2 atlas (TESR_ShadowAtlas).
//
// Mirrors Effects/SunShadows.fx.hlsl. Cascade selection, bias and bleed-reduction constants
// must be changed in both.
//
// TESR_ShadowCameraToLightTransform* consume CAMERA-RELATIVE world space (no
// TESR_CameraPosition added). Build it with GetShadowWorldPos() in the VS.
//
//   VS:  OUT.shadowWorldPos = GetShadowWorldPos(OUT.sPosition);
//   PS:  sunLight *= GetSunShadow(IN.shadowWorldPos, worldNormal);

// ---------------------------------------------------------------------------
// Bound by name via ShaderRecord::CreateCT; registers only decide where the values live.
//
// EVERY constant must stay explicitly pinned. Unpinned, the compiler packs them into gaps
// between the host's declared constants -- registers the engine still writes for the vanilla
// shader -- and overwrites them mid-frame.
//
// c100-c133 is clear in both profiles: heaviest host is TerrainTemplate's PS at c93 and the
// skinned VS Bones[54] ending at c97; ps_3_0 allows 224 constants, vs_3_0 256.
//
// The VS matrices are relocatable: GRASS23x00*.vso indexes InstanceData from c20 well past
// c100.
//
// ---------------------------------------------------------------------------
#ifndef SHADOW_INVPROJ_REG
    #define SHADOW_INVPROJ_REG c100
#endif
#ifndef SHADOW_INVVIEW_REG
    #define SHADOW_INVVIEW_REG c104
#endif

row_major float4x4 TESR_InvProjectionTransform : register(SHADOW_INVPROJ_REG);
row_major float4x4 TESR_InvViewTransform       : register(SHADOW_INVVIEW_REG);

row_major float4x4 TESR_ShadowCameraToLightTransformNear   : register(c108);
row_major float4x4 TESR_ShadowCameraToLightTransformMiddle : register(c112);
row_major float4x4 TESR_ShadowCameraToLightTransformFar    : register(c116);
row_major float4x4 TESR_ShadowCameraToLightTransformLod    : register(c120);

float4 TESR_ShadowNearCenter   : register(c124); // xyz: centre (camera-relative world), w: radius
float4 TESR_ShadowMiddleCenter : register(c125);
float4 TESR_ShadowFarCenter    : register(c126);
float4 TESR_ShadowLodCenter    : register(c127);

float4 TESR_ShadowData        : register(c128); // y: darkness (z is INTERIOR cube texel size only)
float4 TESR_ShadowFormatData  : register(c129); // x: mode (0 VSM, 1 EVSM2, 2 EVSM4), y: format bits
float4 TESR_ShadowFade        : register(c130); // x: sunrise/sunset fade, y: shadow maps active
float4 TESR_SmoothedSunDir    : register(c131);
float4 TESR_ShadowBlur        : register(c132);
float4 TESR_ShadowForwardData : register(c133);

float4 ShadowCascadeBands(float4 radii) {
    return max(0.1f * radii, min(TESR_ShadowForwardData.w, radii));
}

// Object templates top out at s7. Override BEFORE including for wider sampler arrays --
// TerrainTemplate's NormalMap[7] spans s7-s13.
#ifndef SHADOW_ATLAS_SAMPLER_REG
    #define SHADOW_ATLAS_SAMPLER_REG s9
#endif

// MUST stay on ONE line, closing brace included. ShaderTextureValue::GetSamplerStateString
// finds "register ( sN )" and reads only to the end of that line. Split, it silently falls
// back to TextureRecord's POINT/WRAP defaults. Game shaders only; Effects parse their own.
sampler2D TESR_ShadowAtlas : register(SHADOW_ATLAS_SAMPLER_REG) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

#ifndef SUN_CROSSFADE
    #define SUN_CROSSFADE 0
#endif
#if SUN_CROSSFADE
row_major float4x4 TESR_ShadowOldCameraToLightTransformNear   : register(c146);
row_major float4x4 TESR_ShadowOldCameraToLightTransformMiddle : register(c150);
row_major float4x4 TESR_ShadowOldCameraToLightTransformFar    : register(c154);
row_major float4x4 TESR_ShadowOldCameraToLightTransformLod    : register(c158);
float4 TESR_ShadowOldNearCenter   : register(c162);
float4 TESR_ShadowOldMiddleCenter : register(c163);
float4 TESR_ShadowOldFarCenter    : register(c164);
float4 TESR_ShadowOldLodCenter    : register(c165);
float4 TESR_ShadowCrossFade       : register(c166);
#ifndef SHADOW_OLD_ATLAS_SAMPLER_REG
    #define SHADOW_OLD_ATLAS_SAMPLER_REG s10
#endif
sampler2D TESR_ShadowAtlasOld : register(SHADOW_OLD_ATLAS_SAMPLER_REG) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
#endif

// Atlas encoding, compile-time: ps_3_0 would flatten a runtime branch across all three
// variants. Must match [_Shaders.ShadowsExteriors.ShadowMaps] Mode.
//   0 = VSM, 1 = EVSM2, 2 = EVSM4 (toml default)
#ifndef SHADOW_FIXED_MODE
    #define SHADOW_FIXED_MODE 3
#endif

static const float SHADOW_FORMAT = TESR_ShadowFormatData.y;

// Normal-offset bias in SHADOW MAP TEXELS, not world units -- cascade texels span two orders
// of magnitude (~0.15 to ~3 world units at Distance 6000 / Lambda 0.95 / Resolution 2048).
// Must also clear the prefilter kernel: ShadowMapBlur.pso reaches +/-3.23 texels on Near
// (Blur9) and +/-1.33 elsewhere (Blur5).
#ifndef SHADOW_NORMAL_BIAS_TEXELS
    #define SHADOW_NORMAL_BIAS_TEXELS 0.0f
#endif

// Widens the VSM/EVSM minimum variance at grazing incidence, where one texel spans a long run
// of receiver depth. 0 gives a constant bias.
#ifndef SHADOW_SLOPE_BIAS
    #define SHADOW_SLOPE_BIAS 1.0f
#endif

// Taps per cascade, on top of the Gaussian prefilter. 1 matches deferred; 4 costs 12 extra
// fetches per fragment. Above 1, average the MOMENTS and run Chebyshev once -- the bound is
// not linear in the moments.
#ifndef SHADOW_FILTER_TAPS
    #define SHADOW_FILTER_TAPS 1
#endif

// Tap spacing in atlas texels.
#ifndef SHADOW_FILTER_SPREAD
    #define SHADOW_FILTER_SPREAD 1.0f
#endif

// VS presence sentinel. NVR replaces many more pixel than vertex shaders (objects ~134 vs 51,
// terrain 29 vs 2) and the game pairs them independently, so an NVR PS often runs against a
// vanilla VS whose interpolator holds undefined data. NVR vertex shaders stamp this; the PS
// checks it before shadowing.
#define SHADOW_VS_SENTINEL 1.0f
#define SHADOW_VS_PRESENT(w) (abs((w) - SHADOW_VS_SENTINEL) < 0.001f)

// D3DXMACRO from ShaderRecord::LoadShader, off [Shaders.ShadowsExteriors.Main]
// ForwardShadows. Compile-time gate; TESR_ShadowForwardData.x is the runtime one.
#ifndef FORWARD_SHADOWS
    #define FORWARD_SHADOWS 0
#endif

// ---------------------------------------------------------------------------
#ifndef CHARACTER_SHADOW_MAP
    #define CHARACTER_SHADOW_MAP 0
#endif
#ifndef OBJECT_SMOOTH_NORMALS
    #define OBJECT_SMOOTH_NORMALS 1
#endif
#ifndef SHADOW_CHARACTER_MAP_IN
    #define SHADOW_CHARACTER_MAP_IN 0
#endif
#if CHARACTER_SHADOW_MAP && SHADOW_CHARACTER_MAP_IN && FORWARD_SHADOWS && !(defined(INTERIOR_SHADOWS) && INTERIOR_SHADOWS)
    #define SHADOW_CHARACTER_MAP 1
#else
    #define SHADOW_CHARACTER_MAP 0
#endif
#if SHADOW_CHARACTER_MAP
row_major float4x4 TESR_CharShadowCameraToLight : register(c206);
float4 TESR_CharShadowCenter : register(c213);
float4 TESR_CharShadowData   : register(c214);
#ifdef POINT
    #undef POINT
    #define NVR_CHARMAP_POINT_WAS_DEFINED
#endif
sampler2D TESR_CharShadowMap : register(s11) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
#ifdef NVR_CHARMAP_POINT_WAS_DEFINED
    #define POINT
    #undef NVR_CHARMAP_POINT_WAS_DEFINED
#endif

float CharacterMapShadow(float3 worldPos, float3 worldNormal, float NdotL, float cascadeShadow) {
    float texel = max(TESR_CharShadowData.x, 1.0f / 16384.0f);
    float halfExtent = max(TESR_CharShadowCenter.w, 1.0f);
    float offsetScale = saturate(1.0f - NdotL);
    float3 position = worldPos + worldNormal * (offsetScale * 1.5f * 2.0f * halfExtent * texel);
    float4 lightSpace = mul(float4(position, 1.0f), TESR_CharShadowCameraToLight);
    float edge = max(abs(lightSpace.x), abs(lightSpace.y));
    float band = max(TESR_CharShadowData.y, 0.01f);
    float weight = 1.0f - smoothstep(1.0f - band, 1.0f, edge);
    [branch] if (weight <= 0.0f) return cascadeShadow;
    float2 uv = float2(lightSpace.x * 0.5f + 0.5f, lightSpace.y * -0.5f + 0.5f);
    float slope = min(sqrt(saturate(1.0f - NdotL * NdotL)) / max(NdotL, 0.05f), 5.0f);
    float depth = min(lightSpace.z, 0.9999f) - TESR_CharShadowData.z - 1.5f * texel * slope;
    float2 lo = texel * 0.5f;
    float2 hi = 1.0f - texel * 0.5f;
    float2 p = uv / texel - 0.5f;
    float2 c = floor(p + 0.5f);
    float2 f = p - c;
    float3 wx = float3(0.5f * (0.5f - f.x) * (0.5f - f.x), 0.75f - f.x * f.x, 0.5f * (0.5f + f.x) * (0.5f + f.x));
    float3 wy = float3(0.5f * (0.5f - f.y) * (0.5f - f.y), 0.75f - f.y * f.y, 0.5f * (0.5f + f.y) * (0.5f + f.y));
    float lit = 0.0f;
    [unroll] for (int y = 0; y < 3; y++) {
        [unroll] for (int x = 0; x < 3; x++) {
            float2 tapUV = clamp((c + float2(x - 1, y - 1) + 0.5f) * texel, lo, hi);
            lit += (depth <= tex2Dlod(TESR_CharShadowMap, float4(tapUV, 0.0f, 0.0f)).x ? 1.0f : 0.0f) * wx[x] * wy[y];
        }
    }
    [branch] if (TESR_CharShadowData.w > 1.5f) {
        if (TESR_CharShadowData.w > 3.5f) return saturate(lightSpace.z);
        if (TESR_CharShadowData.w > 2.5f) return tex2Dlod(TESR_CharShadowMap, float4(clamp(uv, lo, hi), 0.0f, 0.0f)).x;
        return lit;
    }
    return lerp(cascadeShadow, min(cascadeShadow, lit), weight);
}
#endif

// ---------------------------------------------------------------------------
// VSM / EVSM evaluation. Mirrors Effects/Includes/Shadows.hlsl, duplicated because the two
// trees compile independently.
// ---------------------------------------------------------------------------
float ShadowLinstep(float a, float b, float v) {
    return saturate((v - a) / (b - a));
}

float ShadowReduceLightBleeding(float pMax, float amount) {
    return ShadowLinstep(amount, 1.0f, pMax);
}

float ShadowChebyshevUpperBound(float2 moments, float mean, float minVariance, float bleedReduction) {
    float variance = moments.y - (moments.x * moments.x);
    variance = max(variance, minVariance);

    float d = mean - moments.x;
    float pMax = variance / (variance + (d * d));

    pMax = ShadowReduceLightBleeding(pMax, bleedReduction);

    return (mean <= moments.x ? 1.0f : pMax);
}

float2 ShadowEVSMExponents() {
    const float maxExponent = (SHADOW_FORMAT == 0.0f) ? 5.54f : 42.0f;
    return min(float2(40.0f, 5.0f), maxExponent);
}

float2 ShadowWarpDepth(float depth, float2 exponents) {
    depth = 2.0f * depth - 1.0f;
    return float2(exp(exponents.x * depth), -exp(-exponents.y * depth));
}

// ---------------------------------------------------------------------------
// Atlas sampling. The four cascades occupy the four quadrants of one texture:
//   Near (0.0, 0.0)   Middle (0.5, 0.0)
//   Far  (0.0, 0.5)   Lod    (0.5, 0.5)
// ---------------------------------------------------------------------------
// tex2Dlod, not tex2D: gradients inside the dynamic cascade branch are illegal in ps_3_0
// (X3528). The atlas has no mipmaps, so LOD 0 is equivalent.
float4 SampleShadowMoments(float2 uv, float2 quadrantOffset) {
#if SHADOW_FILTER_TAPS <= 1
    return tex2Dlod(TESR_ShadowAtlas, float4(uv, 0.0f, 0.0f));
#else
    // Taps must stay inside their quadrant: crossing a border reads another cascade's depths.
    // Half a texel of inset keeps bilinear off the seam.
    float texel = TESR_ShadowBlur.x;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;

    // Rotated grid: diagonals cover the texel footprint more evenly than axes.
    float2 d = texel * SHADOW_FILTER_SPREAD;
    float4 m;
    m  = tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 1.0f,  0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-0.5f,  1.0f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2(-1.0f, -0.5f), lo, hi), 0.0f, 0.0f));
    m += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + d * float2( 0.5f, -1.0f), lo, hi), 0.0f, 0.0f));
    return m * 0.25f;
#endif
}

#ifndef SHADOW_SKIN_SOFTNESS
    #define SHADOW_SKIN_SOFTNESS 0
#endif
#if SHADOW_SKIN_SOFTNESS
    static float ShadowMinPenumbra = 0.0f;
#endif

#if SHADOW_FIXED_MODE == 3
static float ShadowSlopeBias = 0.0f;

#ifndef SHADOW_SLACK_CAP_ALL
    #define SHADOW_SLACK_CAP_ALL 0
#endif
#ifndef SHADOW_SLACK_CAP_ACTOR
    #define SHADOW_SLACK_CAP_ACTOR 0
#endif
#if SHADOW_SKIN_SOFTNESS || SHADOW_SLACK_CAP_ALL
    #define SHADOW_CHARACTER_REACH TESR_ShadowBlur.w
#elif SHADOW_SLACK_CAP_ACTOR
    float4 NVR_ShadowCharacterDraw : register(c204);
    #define SHADOW_CHARACTER_REACH (TESR_ShadowBlur.w * NVR_ShadowCharacterDraw.x)
#endif
#if SHADOW_SKIN_SOFTNESS || SHADOW_SLACK_CAP_ALL || SHADOW_SLACK_CAP_ACTOR
    #define SHADOW_SLACK_CAP 1
    #ifndef SHADOW_REACH_ALONG_RAY
        #define SHADOW_REACH_ALONG_RAY 1
    #endif
    static float ShadowSlackCap = 1.0e9f;
    #define SHADOW_SLACK(n) min((n) * ShadowSlopeBias, ShadowSlackCap)
    #define SHADOW_SLACK_WIDE(n) SHADOW_SLACK(n)
    #define SHADOW_SLACK_CUBIC_EXTRA (SHADOW_SLACK(2.0f) - SHADOW_SLACK(1.5f))
#else
    #define SHADOW_SLACK_CAP 0
    #define SHADOW_SLACK(n) ((n) * ShadowSlopeBias)
    #define SHADOW_SLACK_WIDE(n) (ShadowSlopeBias * (n))
    #define SHADOW_SLACK_CUBIC_EXTRA (0.5f * ShadowSlopeBias)
#endif

float ShadowPCF3x3(sampler2D atlas, float2 uv, float depth, float2 quadrantOffset) {
    float texel = TESR_ShadowBlur.x;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;
    [branch] if (TESR_ShadowBlur.z > 0.0f) {
        float2 q = uv / texel - 0.5f;
        float2 b = floor(q);
        float2 t = q - b;
        float2 t2 = t * t, t3 = t2 * t;
        float4 cx = float4((1.0f - t.x) * (1.0f - t.x) * (1.0f - t.x), 3.0f * t3.x - 6.0f * t2.x + 4.0f, -3.0f * t3.x + 3.0f * t2.x + 3.0f * t.x + 1.0f, t3.x) / 6.0f;
        float4 cy = float4((1.0f - t.y) * (1.0f - t.y) * (1.0f - t.y), 3.0f * t3.y - 6.0f * t2.y + 4.0f, -3.0f * t3.y + 3.0f * t2.y + 3.0f * t.y + 1.0f, t3.y) / 6.0f;
        float cubicDepth = depth - SHADOW_SLACK_CUBIC_EXTRA;
        float cubic = 0.0f;
        [unroll] for (int cyi = 0; cyi < 4; cyi++) {
            [unroll] for (int cxi = 0; cxi < 4; cxi++) {
                float2 tapUV = clamp((b + float2(cxi - 1, cyi - 1) + 0.5f) * texel, lo, hi);
                cubic += (cubicDepth <= tex2Dlod(atlas, float4(tapUV, 0.0f, 0.0f)).x ? 1.0f : 0.0f) * cx[cxi] * cy[cyi];
            }
        }
        return cubic;
    }
    float2 p = uv / texel - 0.5f;
    float2 c = floor(p + 0.5f);
    float2 f = p - c;
    float3 wx = float3(0.5f * (0.5f - f.x) * (0.5f - f.x), 0.75f - f.x * f.x, 0.5f * (0.5f + f.x) * (0.5f + f.x));
    float3 wy = float3(0.5f * (0.5f - f.y) * (0.5f - f.y), 0.75f - f.y * f.y, 0.5f * (0.5f + f.y) * (0.5f + f.y));
    float lit = 0.0f;
    [unroll] for (int y = 0; y < 3; y++) {
        [unroll] for (int x = 0; x < 3; x++) {
            float2 tapUV = clamp((c + float2(x - 1, y - 1) + 0.5f) * texel, lo, hi);
            lit += (depth <= tex2Dlod(atlas, float4(tapUV, 0.0f, 0.0f)).x ? 1.0f : 0.0f) * wx[x] * wy[y];
        }
    }
    return lit;
}
#endif

float SampleShadowAtlas(float4 lightSpace, float2 quadrantOffset, float bias, float bleedReduction) {
    lightSpace.xyz /= lightSpace.w;
    lightSpace.x = lightSpace.x * 0.5f + 0.5f;
    lightSpace.y = lightSpace.y * -0.5f + 0.5f;

    // Fold into the correct atlas quadrant.
    lightSpace.xy = lightSpace.xy * 0.5f + quadrantOffset;

#if SHADOW_FIXED_MODE == 3
    return ShadowPCF3x3(TESR_ShadowAtlas, lightSpace.xy, lightSpace.z - bias - SHADOW_SLACK(1.5f), quadrantOffset);
#else
    float4 moments = SampleShadowMoments(lightSpace.xy, quadrantOffset);

#if SHADOW_FIXED_MODE == 0
    // VSM
    return ShadowChebyshevUpperBound(moments.xy, lightSpace.z, bias, bleedReduction);
#elif SHADOW_FIXED_MODE == 1
    // EVSM2
    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(lightSpace.z, exponents);
    float2 depthScale = bias * exponents * warped;
    return ShadowChebyshevUpperBound(moments.xy, warped.x, depthScale.x * depthScale.x, bleedReduction);
#else
    // EVSM4
    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(lightSpace.z, exponents);
    float2 depthScale = bias * exponents * warped;
    float2 minVariance = depthScale * depthScale;

    float posContrib = ShadowChebyshevUpperBound(moments.xz, warped.x, minVariance.x, bleedReduction);
    float negContrib = ShadowChebyshevUpperBound(moments.yw, warped.y, minVariance.y, bleedReduction);
    return min(posContrib, negContrib);
#endif
#endif
}

#if SUN_CROSSFADE
float SampleShadowAtlasOld(float4 lightSpace, float2 quadrantOffset, float bias, float bleedReduction) {
    lightSpace.xyz /= lightSpace.w;
    lightSpace.x = lightSpace.x * 0.5f + 0.5f;
    lightSpace.y = lightSpace.y * -0.5f + 0.5f;
    lightSpace.xy = lightSpace.xy * 0.5f + quadrantOffset;

#if SHADOW_FIXED_MODE == 3
    return ShadowPCF3x3(TESR_ShadowAtlasOld, lightSpace.xy, lightSpace.z - bias - SHADOW_SLACK(1.5f), quadrantOffset);
#else
    float4 moments = tex2Dlod(TESR_ShadowAtlasOld, float4(lightSpace.xy, 0.0f, 0.0f));

#if SHADOW_FIXED_MODE == 0
    return ShadowChebyshevUpperBound(moments.xy, lightSpace.z, bias, bleedReduction);
#elif SHADOW_FIXED_MODE == 1
    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(lightSpace.z, exponents);
    float2 depthScale = bias * exponents * warped;
    return ShadowChebyshevUpperBound(moments.xy, warped.x, depthScale.x * depthScale.x, bleedReduction);
#else
    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(lightSpace.z, exponents);
    float2 depthScale = bias * exponents * warped;
    float2 minVariance = depthScale * depthScale;
    float posContrib = ShadowChebyshevUpperBound(moments.xz, warped.x, minVariance.x, bleedReduction);
    float negContrib = ShadowChebyshevUpperBound(moments.yw, warped.y, minVariance.y, bleedReduction);
    return min(posContrib, negContrib);
#endif
#endif
}

float GetOldSunShadow(float3 worldPos, float3 worldNormal, float offsetScale, float bias, out float covered) {
    float4 radii = float4(TESR_ShadowOldNearCenter.w, TESR_ShadowOldMiddleCenter.w,
                          TESR_ShadowOldFarCenter.w,  TESR_ShadowOldLodCenter.w);
    float4 texelWorld = 4.0f * radii * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
    float4 offsetDistance = offsetScale * SHADOW_NORMAL_BIAS_TEXELS * texelWorld;
#if SHADOW_FIXED_MODE == 3
    offsetDistance = max(offsetDistance, offsetScale * 1.5f * texelWorld);
#endif
    float4 starts = radii - ShadowCascadeBands(radii);
    float4 distances = float4(
        length(worldPos - TESR_ShadowOldNearCenter.xyz),
        length(worldPos - TESR_ShadowOldMiddleCenter.xyz),
        length(worldPos - TESR_ShadowOldFarCenter.xyz),
        length(worldPos - TESR_ShadowOldLodCenter.xyz));

#define OLD_TAP_NEAR(o)   SampleShadowAtlasOld(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowOldCameraToLightTransformNear),   float2(0.0f, 0.0f), bias, 0.1f)
#define OLD_TAP_MIDDLE(o) SampleShadowAtlasOld(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowOldCameraToLightTransformMiddle), float2(0.5f, 0.0f), bias, 0.2f)
#define OLD_TAP_FAR(o)    SampleShadowAtlasOld(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowOldCameraToLightTransformFar),    float2(0.0f, 0.5f), bias, 0.6f)
#define OLD_TAP_LOD(o)    SampleShadowAtlasOld(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowOldCameraToLightTransformLod),    float2(0.5f, 0.5f), bias, 0.8f)

    covered = 1.0f;
    float shadow = 1.0f;
    [branch] if (distances.x < radii.x) {
        float f = smoothstep(starts.x, radii.x, distances.x);
        float o = lerp(offsetDistance.x, offsetDistance.y, f);
        [branch] if (f <= 0.0f)
            shadow = OLD_TAP_NEAR(o);
        else
            shadow = lerp(OLD_TAP_NEAR(o), OLD_TAP_MIDDLE(o), f);
    }
    else if (distances.y < radii.y) {
        float f = smoothstep(starts.y, radii.y, distances.y);
        float o = lerp(offsetDistance.y, offsetDistance.z, f);
        [branch] if (f <= 0.0f)
            shadow = OLD_TAP_MIDDLE(o);
        else
            shadow = lerp(OLD_TAP_MIDDLE(o), OLD_TAP_FAR(o), f);
    }
    else if (distances.z < radii.z) {
        float f = smoothstep(starts.z, radii.z, distances.z);
        float o = lerp(offsetDistance.z, offsetDistance.w, f);
        [branch] if (f <= 0.0f)
            shadow = OLD_TAP_FAR(o);
        else
            shadow = lerp(OLD_TAP_FAR(o), OLD_TAP_LOD(o), f);
    }
    else if (distances.w < radii.w) {
        shadow = lerp(OLD_TAP_LOD(offsetDistance.w), 1.0f, smoothstep(starts.w, radii.w, distances.w));
    }
    else {
        covered = 0.0f;
    }

#undef OLD_TAP_NEAR
#undef OLD_TAP_MIDDLE
#undef OLD_TAP_FAR
#undef OLD_TAP_LOD

    return saturate(shadow);
}
#endif

#ifndef CONTACT_HARDENING
    #define CONTACT_HARDENING 0
#endif
#if CONTACT_HARDENING
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
float4 TESR_ContactHardeningData : register(c167);

static float ShadowSoftnessCap = 1.0f;

static const float2 ContactDisc[12] = {
    float2(0.2041f, 0.0000f), float2(-0.2607f, 0.2388f), float2(0.0399f, -0.4547f), float2(0.3286f, 0.4286f),
    float2(-0.6030f, -0.1067f), float2(0.5712f, -0.3634f), float2(-0.1911f, 0.7107f), float2(-0.3644f, -0.7016f),
    float2(0.7906f, 0.2887f), float2(-0.8224f, 0.3395f), float2(0.3965f, -0.8472f), float2(0.2930f, 0.9341f) };

#if SHADOW_FIXED_MODE == 3
static const float2 ContactDisc8[8] = {
    float2(0.2500f, 0.0000f), float2(-0.3193f, 0.2925f), float2(0.0489f, -0.5569f), float2(0.4025f, 0.5248f),
    float2(-0.7385f, -0.1306f), float2(0.6997f, -0.4451f), float2(-0.2340f, 0.8705f), float2(-0.4467f, -0.8590f) };

float ShadowPCF2x2Clamped(float2 uv, float depth, float2 lo, float2 hi) {
    float texel = TESR_ShadowBlur.x;
    float2 p = uv / texel - 0.5f;
    float2 base = floor(p);
    float2 f = p - base;
    float2 uv0 = (base + 0.5f) * texel;
    float4 d = float4(tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv0, lo, hi), 0.0f, 0.0f)).x,
                      tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv0 + float2(texel, 0.0f), lo, hi), 0.0f, 0.0f)).x,
                      tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv0 + float2(0.0f, texel), lo, hi), 0.0f, 0.0f)).x,
                      tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv0 + texel, lo, hi), 0.0f, 0.0f)).x);
    float4 lit = depth <= d ? 1.0f : 0.0f;
    return lerp(lerp(lit.x, lit.y, f.x), lerp(lit.z, lit.w, f.x), f.y);
}
#endif

#if SHADOW_FIXED_MODE == 3
float SampleShadowAtlasPCSS(float3 position, float4x4 lightTransform, float2 quadrantOffset, float bias, bool contact);
#endif
float SampleShadowAtlasContact(float3 position, float4x4 lightTransform, float2 quadrantOffset, float bias, float bleedReduction) {
#if SHADOW_FIXED_MODE == 3
    return SampleShadowAtlasPCSS(position, lightTransform, quadrantOffset, bias, true);
}
float SampleShadowAtlasPCSS(float3 position, float4x4 lightTransform, float2 quadrantOffset, float bias, bool contact) {
    float4 lightSpace = mul(float4(position, 1.0f), lightTransform);
    lightSpace.xyz /= lightSpace.w;
    float2 uv = float2(lightSpace.x * 0.5f + 0.5f, lightSpace.y * -0.5f + 0.5f) * 0.5f + quadrantOffset;
    float depth = lightSpace.z - bias;
    float worldPerDepth = 1.0f / length(float3(lightTransform[0][2], lightTransform[1][2], lightTransform[2][2]));
    float uvPerWorld = 0.25f * length(float3(lightTransform[0][0], lightTransform[1][0], lightTransform[2][0]));
    float texel = TESR_ShadowBlur.x;
    float maxRadius = TESR_ContactHardeningData.y * ShadowSoftnessCap * texel;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;

    static const float2 search[5] = { float2(0.0f, 0.0f), float2(0.73f, 0.20f), float2(-0.20f, 0.73f), float2(-0.73f, -0.20f), float2(0.20f, -0.73f) };
    float radius = 0.0f;
    [branch] if (contact) {
        float casterSum = 0.0f, casters = 0.0f;
        float searchDepth = depth - SHADOW_SLACK_WIDE(0.73f * TESR_ContactHardeningData.y * ShadowSoftnessCap + 1.0f);
        [unroll] for (int s = 0; s < 5; s++) {
            float d = tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + search[s] * maxRadius, lo, hi), 0.0f, 0.0f)).x;
            float blocks = d < searchDepth ? 1.0f : 0.0f;
            casterSum += d * blocks;
            casters += blocks;
        }
        float distance = max(depth - casterSum / max(casters, 1.0f), 0.0f) * worldPerDepth;
        radius = casters < 0.5f || casters > 4.5f ? 0.0f : min(0.5f * distance * TESR_ContactHardeningData.x * uvPerWorld, maxRadius);
        #if SHADOW_SKIN_SOFTNESS
        if (casters > 0.5f && casters < 4.5f) radius = max(radius, min(ShadowMinPenumbra * uvPerWorld, 0.73f * maxRadius));
        #endif
    }
    float w3 = TESR_ContactHardeningData.z > 0.0f ? saturate(2.0f - radius / texel) : (radius <= texel ? 1.0f : 0.0f);
    float lit = 0.0f;
    [branch] if (w3 > 0.0f) lit = w3 * ShadowPCF3x3(TESR_ShadowAtlas, uv, depth - SHADOW_SLACK(1.5f), quadrantOffset);
    [branch] if (w3 < 1.0f) {
        float discDepth = depth - SHADOW_SLACK_WIDE(radius / texel + 1.0f);
        float2 turn = float2(1.0f, 0.0f);
        [branch] if (TESR_ContactHardeningData.z > 0.0f)
            sincos(6.2831853f * frac(52.9829189f * frac(dot(floor(uv / texel * 3.0f), float2(0.06711056f, 0.00583715f)))), turn.y, turn.x);
        float disc = ShadowPCF2x2Clamped(uv, discDepth, lo, hi);
        [unroll] for (int i = 0; i < 8; i++) {
            float2 o = ContactDisc8[i];
            disc += ShadowPCF2x2Clamped(uv + float2(o.x * turn.x - o.y * turn.y, o.x * turn.y + o.y * turn.x) * radius, discDepth, lo, hi);
        }
        lit += (1.0f - w3) * disc / 9.0f;
    }
    return lit;
#elif SHADOW_FIXED_MODE != 2
    return SampleShadowAtlas(mul(float4(position, 1.0f), lightTransform), quadrantOffset, bias, bleedReduction);
#else
    float4 lightSpace = mul(float4(position, 1.0f), lightTransform);
    lightSpace.xyz /= lightSpace.w;
    float2 uv = float2(lightSpace.x * 0.5f + 0.5f, lightSpace.y * -0.5f + 0.5f) * 0.5f + quadrantOffset;
    float depth = lightSpace.z;

    float worldPerDepth = 1.0f / length(float3(lightTransform[0][2], lightTransform[1][2], lightTransform[2][2]));
    float uvPerWorld = 0.25f * length(float3(lightTransform[0][0], lightTransform[1][0], lightTransform[2][0]));
    float texel = TESR_ShadowBlur.x;
    float maxRadius = TESR_ContactHardeningData.y * ShadowSoftnessCap * texel;
    float2 lo = quadrantOffset + texel * 0.5f;
    float2 hi = quadrantOffset + 0.5f - texel * 0.5f;

    float2 exponents = ShadowEVSMExponents();
    float2 warped = ShadowWarpDepth(depth, exponents);
    float2 depthScale = bias * exponents * warped;
    float2 minVariance = depthScale * depthScale;

    float2 search = tex2Dlod(TESR_ShadowAtlas, float4(uv, 0.0f, 0.0f)).xz;
    search += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + maxRadius * float2( 0.7f,  0.7f), lo, hi), 0.0f, 0.0f)).xz;
    search += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + maxRadius * float2(-0.7f,  0.7f), lo, hi), 0.0f, 0.0f)).xz;
    search += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + maxRadius * float2( 0.7f, -0.7f), lo, hi), 0.0f, 0.0f)).xz;
    search += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + maxRadius * float2(-0.7f, -0.7f), lo, hi), 0.0f, 0.0f)).xz;
    search *= 0.2f;
    float variance = max(search.y - search.x * search.x, minVariance.x);
    float difference = warped.x - search.x;
    float lit = warped.x <= search.x ? 1.0f : variance / (variance + difference * difference);
    [branch] if (lit > 0.99f) return 1.0f;

    float casterWarped = max((search.x - lit * warped.x) / (1.0f - lit), 1.0e-6f);
    float casterDepth = (log(casterWarped) / exponents.x + 1.0f) * 0.5f;
    float distance = max(depth - casterDepth, 0.0f) * worldPerDepth;

    float radius = min(0.5f * distance * TESR_ContactHardeningData.x * uvPerWorld, maxRadius);

    float4 moments = tex2Dlod(TESR_ShadowAtlas, float4(uv, 0.0f, 0.0f));
    [branch] if (radius > texel) {
        [unroll] for (int i = 0; i < 12; i++)
            moments += tex2Dlod(TESR_ShadowAtlas, float4(clamp(uv + ContactDisc[i] * radius, lo, hi), 0.0f, 0.0f));
        moments /= 13.0f;
    }
    float posContrib = ShadowChebyshevUpperBound(moments.xz, warped.x, minVariance.x, bleedReduction);
    float negContrib = ShadowChebyshevUpperBound(moments.yw, warped.y, minVariance.y, bleedReduction);
    return min(posContrib, negContrib);
#endif
}

float SampleShadowCascade(float3 position, float4x4 lightTransform, float2 quadrantOffset, float bias, float bleedReduction) {
    [branch] if (TESR_ContactHardeningData.x > 0.0f)
        return SampleShadowAtlasContact(position, lightTransform, quadrantOffset, bias, bleedReduction);
    return SampleShadowAtlas(mul(float4(position, 1.0f), lightTransform), quadrantOffset, bias, bleedReduction);
}
#endif

#if FORWARD_SHADOWS
float SunShadowCoarse(float3 worldPos) {
#if SHADOW_FIXED_MODE == 0
    const float bias = 0.00001f;
#elif SHADOW_FIXED_MODE == 3
    const float bias = 0.0002f;
#else
    const float bias = 0.01f;
#endif
    const bool near = length(worldPos - TESR_ShadowNearCenter.xyz) < TESR_ShadowNearCenter.w;
    const float4x4 lightTransform = near ? TESR_ShadowCameraToLightTransformNear : TESR_ShadowCameraToLightTransformMiddle;
    return saturate(SampleShadowAtlas(mul(float4(worldPos, 1.0f), lightTransform), near ? float2(0.0f, 0.0f) : float2(0.5f, 0.0f), bias, near ? 0.1f : 0.2f));
}
#endif

// ---------------------------------------------------------------------------
// Camera-relative world position from clip space. Call in the VS and interpolate: it is exact
// under perspective-correct interpolation and costs one interpolator.
// ---------------------------------------------------------------------------
float3 GetShadowWorldPos(float4 clipPos) {
    float4 viewPos = mul(clipPos, TESR_InvProjectionTransform);
    viewPos /= viewPos.w;
    viewPos.xyz *= clipPos.w / (abs(viewPos.z) > 1e-6f ? viewPos.z : 1e-6f);
    return mul(viewPos, TESR_InvViewTransform).xyz;
}

// ---------------------------------------------------------------------------
// Flat face normal from screen-space derivatives of the interpolated world position. Object
// shaders carry their normal in TANGENT space, so no world normal is available to the bias.
//
// MUST be called from the top level of the pixel shader: ddx/ddy are illegal inside dynamic
// flow control.
// ---------------------------------------------------------------------------
float3 GetShadowGeometricNormal(float3 worldPos) {
    float3 n = normalize(cross(ddx(worldPos), ddy(worldPos)));

    // worldPos is camera-relative, so the camera is at the origin and the outward normal is
    // the one facing it. Independent of winding.
    return n * sign(dot(n, -worldPos));
}

float3 ShadowLookupNormal(float3 flatNormal, float4 smoothNormal) {
#if SHADOW_SKIN_SOFTNESS
    ShadowMinPenumbra = TESR_ShadowForwardData.z;
#endif
    float len2 = dot(smoothNormal.xyz, smoothNormal.xyz);
    return ((TESR_ShadowForwardData.y > 0.0f || OBJECT_SMOOTH_NORMALS) && SHADOW_VS_PRESENT(smoothNormal.w) && len2 > 0.01f) ? smoothNormal.xyz * rsqrt(len2) : flatNormal;
}

float4 GetShadowSmoothNormal(float4x4 modelViewProj, float3 position, float3 normal, float3 shadowWorldPos) {
    float3 along = GetShadowWorldPos(mul(modelViewProj, float4(position + 2.0f * normalize(normal), 1.0f)));
    return float4(normalize(along - shadowWorldPos), SHADOW_VS_SENTINEL);
}


float2 ShadowEncodeNormal(float3 n) {
    n /= max(abs(n.x) + abs(n.y) + abs(n.z), 1.0e-6f);
    float2 e = n.xy;
    if (n.z < 0.0f) e = (1.0f - abs(n.yx)) * float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
    return e;
}
float3 ShadowDecodeNormal(float2 e) {
    float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
    return normalize(n);
}

float3 ShadowPlainNormal(float2 xy, float zColor) {
    return float3(xy, zColor * 2.0f - 1.0f);
}

float3 ShadowFacingNormal(float3 n, float3 worldPos) {
    float len2 = dot(n, n);
    n = len2 > 1.0e-6f ? n * rsqrt(len2) : float3(0.0f, 0.0f, 1.0f);
    return n * (dot(n, worldPos) > 0.0f ? -1.0f : 1.0f);
}

#if SHADOW_FIXED_MODE == 3
float SunCascadeTap(float c, float3 worldPos, float3 worldNormal, float offset, float bias) {
    float4x4 lightTransform = c < 0.5f ? TESR_ShadowCameraToLightTransformNear : c < 1.5f ? TESR_ShadowCameraToLightTransformMiddle :
                              c < 2.5f ? TESR_ShadowCameraToLightTransformFar : TESR_ShadowCameraToLightTransformLod;
    float2 quadrant = c < 0.5f ? float2(0.0f, 0.0f) : c < 1.5f ? float2(0.5f, 0.0f) : c < 2.5f ? float2(0.0f, 0.5f) : float2(0.5f, 0.5f);
    float3 position = worldPos + offset * worldNormal;
#if CONTACT_HARDENING
#if SHADOW_SKIN_SOFTNESS
    return SampleShadowAtlasPCSS(position, lightTransform, quadrant, bias, c < 1.5f && (TESR_ContactHardeningData.x > 0.0f || ShadowMinPenumbra > 0.0f));
#else
    return SampleShadowAtlasPCSS(position, lightTransform, quadrant, bias, c < 1.5f && TESR_ContactHardeningData.x > 0.0f);
#endif
#else
    return SampleShadowAtlas(mul(float4(position, 1.0f), lightTransform), quadrant, bias, 0.0f);
#endif
}
#endif

// ---------------------------------------------------------------------------
// 1.0 in full light, towards 0 in shadow. Multiply the SUN term by it, before ambient.
// ---------------------------------------------------------------------------
float GetSunShadow(float3 worldPos, float3 worldNormal) {
    // Nonzero SUPPRESSES the forward path; SunShadows.fx reads the same constant and takes
    // over deferred. A missing constant reads zero and leaves forward running.
    if (TESR_ShadowForwardData.x) return 1.0f;

    // Sun shadows never apply indoors. Dedicated flag, not ShadowFade.y below -- that one is
    // shared with ShadowsInteriors.fx's own darkness calc and means something else indoors
    // (interior point-shadows enabled), so it can't double as this gate. Without this check,
    // an interior object shader would fall through to stale exterior cascade data left over
    // from the last time the player was outside.
    if (!TESR_ShadowFormatData.z) return 1.0f;

    // Shadow maps switched off entirely (setting), or sun below horizon.
    if (!TESR_ShadowFade.y) return 1.0f;

    // ShadowMap.pso's channel layout differs per Mode. Fail unshadowed on a mismatch.
    if (TESR_ShadowFormatData.x != (float)SHADOW_FIXED_MODE) return 1.0f;

    // Push the sample along the normal, scaled by how grazing the sun is.
    float NdotL = dot(worldNormal, TESR_SmoothedSunDir.xyz);
    float offsetScale = saturate(1.0f - NdotL);

    float4 radii = float4(TESR_ShadowNearCenter.w, TESR_ShadowMiddleCenter.w,
                          TESR_ShadowFarCenter.w,  TESR_ShadowLodCenter.w);

    // Texel world size per cascade. GetCascadeViewProj's ortho box is 2*radius across, and the
    // atlas is two cascades wide, so with TESR_ShadowBlur.x = 1/atlasResolution:
    //     cascadeResolution = 0.5 / TESR_ShadowBlur.x
    //     texelWorldSize    = 4 * radius * TESR_ShadowBlur.x
    // The floor keeps a zero constant from producing an infinite offset.
    float4 texelWorld = 4.0f * radii * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
    float4 offsetDistance = offsetScale * SHADOW_NORMAL_BIAS_TEXELS * texelWorld;
#if SHADOW_FIXED_MODE == 3
    offsetDistance = max(offsetDistance, offsetScale * 1.5f * texelWorld);
    ShadowSlopeBias = 2.0f * TESR_ShadowBlur.x * min(sqrt(saturate(1.0f - NdotL * NdotL)) / max(NdotL, 0.05f), 5.0f);
#if SHADOW_SLACK_CAP
    [flatten] if (SHADOW_CHARACTER_REACH > 0.0f) {
        offsetDistance = min(offsetDistance, SHADOW_CHARACTER_REACH * radii / max(radii.x, 0.001f));
        ShadowSlackCap = SHADOW_CHARACTER_REACH / (2.0f * max(radii.x, 0.001f) * ((SHADOW_REACH_ALONG_RAY) ? max(NdotL, 0.2f) : 1.0f));
    }
#endif
#endif

#if SHADOW_FIXED_MODE == 0
    const float baseBias = 0.00001f;
#elif SHADOW_FIXED_MODE == 3
    const float baseBias = 0.0002f;
#else
    const float baseBias = 0.01f;
#endif

    // Slope-scaled variance floor: bias drives minVariance, the slack Chebyshev gets before
    // calling a texel occluded.
    float bias = baseBias * (1.0f + SHADOW_SLOPE_BIAS * offsetScale);

    float4 starts = radii - ShadowCascadeBands(radii);

    // matching SunShadows.fx: selecting on the square light-space NDC box steps visibly.
    float4 distances = float4(
        length(worldPos - TESR_ShadowNearCenter.xyz),
        length(worldPos - TESR_ShadowMiddleCenter.xyz),
        length(worldPos - TESR_ShadowFarCenter.xyz),
        length(worldPos - TESR_ShadowLodCenter.xyz));

    // matrix transforms on every shadowed pixel, and the forward path pays that per object.
    //
    // Legal here only because SampleShadowAtlas uses tex2Dlod -- a gradient instruction under
    // dynamic flow control is X3528 in ps_3_0. The atlas has no mipmaps, so LOD 0 is exact.
#if CONTACT_HARDENING
#define SHADOW_TAP_NEAR(o)   SampleShadowCascade(worldPos + (o) * worldNormal, TESR_ShadowCameraToLightTransformNear,   float2(0.0f, 0.0f), bias, 0.1f)
#define SHADOW_TAP_MIDDLE(o) SampleShadowCascade(worldPos + (o) * worldNormal, TESR_ShadowCameraToLightTransformMiddle, float2(0.5f, 0.0f), bias, 0.2f)
#else
#define SHADOW_TAP_NEAR(o)   SampleShadowAtlas(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformNear),   float2(0.0f, 0.0f), bias, 0.1f)
#define SHADOW_TAP_MIDDLE(o) SampleShadowAtlas(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformMiddle), float2(0.5f, 0.0f), bias, 0.2f)
#endif
#define SHADOW_TAP_FAR(o)    SampleShadowAtlas(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformFar),    float2(0.0f, 0.5f), bias, 0.6f)
#define SHADOW_TAP_LOD(o)    SampleShadowAtlas(mul(float4(worldPos + (o) * worldNormal, 1.0f), TESR_ShadowCameraToLightTransformLod),    float2(0.5f, 0.5f), bias, 0.8f)

#if SHADOW_FIXED_MODE == 3
    float cascade = -1.0f, fade = 0.0f;
    [flatten] if (distances.x < radii.x) { cascade = 0.0f; fade = smoothstep(starts.x, radii.x, distances.x); }
    else if (distances.y < radii.y) { cascade = 1.0f; fade = smoothstep(starts.y, radii.y, distances.y); }
    else if (distances.z < radii.z) { cascade = 2.0f; fade = smoothstep(starts.z, radii.z, distances.z); }
    else if (distances.w < radii.w) { cascade = 3.0f; fade = smoothstep(starts.w, radii.w, distances.w); }
    float offsetHere = cascade < 0.5f ? lerp(offsetDistance.x, offsetDistance.y, fade) : cascade < 1.5f ? lerp(offsetDistance.y, offsetDistance.z, fade) :
                       cascade < 2.5f ? lerp(offsetDistance.z, offsetDistance.w, fade) : offsetDistance.w;
    float slackScale = cascade < 0.5f ? lerp(radii.x / max(radii.y, 0.001f), 1.0f, fade) : 1.0f;
#if defined(NVR_NO_SM3_LIMITS) && NVR_NO_SM3_LIMITS
    float shadow = 1.0f;
    [branch] if (cascade >= 0.0f && cascade < 3.5f) {
        shadow = SunCascadeTap(cascade, worldPos, worldNormal, offsetHere, bias);
        [branch] if (fade > 0.0f) {
            float next = 1.0f;
            [branch] if (cascade < 2.5f) {
                float slopeBiasHere = ShadowSlopeBias;
                ShadowSlopeBias *= slackScale;
#if CONTACT_HARDENING
                ShadowSoftnessCap = slackScale;
#endif
                next = SunCascadeTap(cascade + 1.0f, worldPos, worldNormal, offsetHere, bias);
                ShadowSlopeBias = slopeBiasHere;
#if CONTACT_HARDENING
                ShadowSoftnessCap = 1.0f;
#endif
            }
            shadow = lerp(shadow, next, fade);
        }
    }
#else
    float3 hashP = frac(worldPos * 7.1031f);
    hashP += dot(hashP, hashP.yzx + 33.33f);
    float dither = frac((hashP.x + hashP.y) * hashP.z);
    cascade += fade > dither ? 1.0f : 0.0f;
    slackScale = cascade > 0.5f ? slackScale : 1.0f;
    float slopeBiasHere = ShadowSlopeBias;
    ShadowSlopeBias *= slackScale;
#if CONTACT_HARDENING
    ShadowSoftnessCap = slackScale;
#endif
    float shadow = 1.0f;
    [branch] if (cascade >= 0.0f && cascade < 3.5f)
        shadow = SunCascadeTap(cascade, worldPos, worldNormal, offsetHere, bias);
    ShadowSlopeBias = slopeBiasHere;
#if CONTACT_HARDENING
    ShadowSoftnessCap = 1.0f;
#endif
#endif
#else
    // Initialised: a point beyond the last cascade falls through every branch.
    float shadow = 1.0f;
    [branch] if (distances.x < radii.x) {
        float f = smoothstep(starts.x, radii.x, distances.x);
        float o = lerp(offsetDistance.x, offsetDistance.y, f);
        [branch] if (f <= 0.0f)
            shadow = SHADOW_TAP_NEAR(o);
        else {
            float nearTap = SHADOW_TAP_NEAR(o);
#if CONTACT_HARDENING
            ShadowSoftnessCap = lerp(radii.x / max(radii.y, 0.001f), 1.0f, f);
#endif
            float middleTap = SHADOW_TAP_MIDDLE(o);
#if CONTACT_HARDENING
            ShadowSoftnessCap = 1.0f;
#endif
            shadow = lerp(nearTap, middleTap, f);
        }
    }
    else if (distances.y < radii.y) {
        float f = smoothstep(starts.y, radii.y, distances.y);
        float o = lerp(offsetDistance.y, offsetDistance.z, f);
        [branch] if (f <= 0.0f)
            shadow = SHADOW_TAP_MIDDLE(o);
        else
            shadow = lerp(SHADOW_TAP_MIDDLE(o), SHADOW_TAP_FAR(o), f);
    }
    else if (distances.z < radii.z) {
        float f = smoothstep(starts.z, radii.z, distances.z);
        float o = lerp(offsetDistance.z, offsetDistance.w, f);
        [branch] if (f <= 0.0f)
            shadow = SHADOW_TAP_FAR(o);
        else
            shadow = lerp(SHADOW_TAP_FAR(o), SHADOW_TAP_LOD(o), f);
    }
    else if (distances.w < radii.w) {
        shadow = lerp(SHADOW_TAP_LOD(offsetDistance.w), 1.0f, smoothstep(starts.w, radii.w, distances.w));
    }
#endif

#undef SHADOW_TAP_NEAR
#undef SHADOW_TAP_MIDDLE
#undef SHADOW_TAP_FAR
#undef SHADOW_TAP_LOD

    shadow = saturate(shadow);

#if SUN_CROSSFADE
    [branch] if (TESR_ShadowCrossFade.x > 0.0f) {
        float covered;
        float oldShadow = GetOldSunShadow(worldPos, worldNormal, offsetScale, bias, covered);
        shadow = lerp(shadow, oldShadow, TESR_ShadowCrossFade.x * covered);
    }
#endif

#if SHADOW_CHARACTER_MAP
    [branch] if (TESR_CharShadowData.w > 0.0f)
        shadow = CharacterMapShadow(worldPos, worldNormal, NdotL, shadow);
#endif

    // Fade out as the sun approaches the horizon, matching SunShadows.fx.
    shadow = lerp(shadow, 1.0f, saturate(TESR_ShadowFade.x));

    return shadow;
}
