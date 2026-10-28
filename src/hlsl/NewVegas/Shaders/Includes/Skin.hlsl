#ifndef SKIN_INCLUDED
#define SKIN_INCLUDED

#if defined(__INTELLISENSE__)
    #include "PBR.hlsl"
    #include "PBRScale.hlsl"
#else
    #include "includes/PBR.hlsl"
    #include "includes/PBRScale.hlsl"
#endif

float4 TESR_SkinMaterial : register(c192);
float4 TESR_SkinLook     : register(c193);
float4 NVR_SkinDraw      : register(c194);
#ifndef SKIN_DEBUG_DECLARED
float4 TESR_SkinDebug    : register(c195);
#endif
#define SKIN_LEGACY (TESR_SkinDebug.z > 0.5f)

#define SKIN_F0 0.028f
#define SKIN_LOBE0 0.75f
#define SKIN_LOBE1 1.3f
#define SKIN_LOBE_MIX 0.15f
#define SKIN_SHEEN_ALPHA 0.1225f
#define SKIN_SHEEN_F0 0.045f
#define SKIN_MM_PER_UNIT 14.2857f
#define SKIN_SCATTER_BASE 2.0f
#define SKIN_TRANSMIT_DISTORTION 0.3f
#define SKIN_TRANSMIT_POWER 4.0f
#define SKIN_SOFT_NORMAL_BIAS 2.0f

static float skinRoughness = 0.5f;
static float skinSpecScale = 0.0f;
static float skinCurvature = 0.0f;
static float skinThinness = 0.0f;
static float skinSkyWeight = 0.0f;

float3 SkinLinearColor(float3 c) { return LinColor(sqrt(max(c, 0.0f))); }

void SkinSetup(float3 normalTS, float3 worldNormal, float3 worldPos, float mask, float blurTakes) {
    const float wet = saturate(TESR_SkinLook.y);
    const float roughness = lerp(TESR_SkinMaterial.y, TESR_SkinMaterial.y * 0.55f, wet);
    skinRoughness = clamp(SpecularAA(normalTS, roughness), 0.08f, 1.0f);
    skinSpecScale = mask * 2.0f * TESR_SkinMaterial.x * (1.0f + wet);

    const float3 dNx = ddx(worldNormal);
    const float3 dNy = ddy(worldNormal);
    const float3 dPx = ddx(worldPos);
    const float3 dPy = ddy(worldPos);
    float curvature = max(0.5f * (dot(dNx, dPx) / max(dot(dPx, dPx), 1e-8f) + dot(dNy, dPy) / max(dot(dPy, dPy), 1e-8f)), 0.0f) / SKIN_MM_PER_UNIT;
    [flatten] if (SKIN_LEGACY)
        curvature = length(abs(dNx) + abs(dNy)) / max(length(abs(dPx) + abs(dPy)) * SKIN_MM_PER_UNIT, 1e-4f);
    skinThinness = saturate(curvature * SKIN_SCATTER_BASE);
    skinCurvature = blurTakes > 0.0f ? 0.0f : saturate(curvature * SKIN_SCATTER_BASE * TESR_SkinMaterial.z);
}

float3 SkinSGIrradiance(float3 NdotL, float3 lambda) {
    const float c0 = 0.36f;
    const float c1 = 1.0f / (4.0f * c0);
    const float3 eml = exp(-lambda);
    const float3 em2l = eml * eml;
    const float3 rl = 1.0f / lambda;
    const float3 scale = 1.0f + 2.0f * em2l - rl;
    const float3 bias = (eml - em2l) * rl - em2l;
    const float3 x = sqrt(max(1.0f - scale, 1e-8f));
    const float3 x0 = c0 * NdotL;
    const float3 x1 = c1 * x;
    const float3 n = x0 + x1;
    const float3 y = abs(x0) <= x1 ? n * n / x : saturate(NdotL);
    return saturate(scale * y + bias);
}

float3 SkinDiffusion(float3 N, float3 Nsoft, float3 L, out float3 plain) {
    const float3 normalGreen = normalize(lerp(N, Nsoft, 0.6f));
    const float3 normalBlue = normalize(lerp(N, Nsoft, 0.3f));
    const float3 NdotL = float3(dot(Nsoft, L), dot(normalGreen, L), dot(normalBlue, L));
    plain = saturate(NdotL);
    [branch] if (skinCurvature > 0.002f) {
        const float c = skinCurvature;
        const float3 lambda = float3(2.34f * pow(c, -1.354f), 27.1f * pow(c, -2.017f), 63.4f * pow(c, -2.222f));
        return SkinSGIrradiance(NdotL, lambda);
    }
    return saturate(NdotL);
}

float3 SkinShadow(float shadow, float edge) {
    const float3 reach = TESR_SkinLook.z * float3(0.6f, 0.25f, 0.12f);
    const float s = saturate(shadow);
    const float3 legacy = pow(s.xxx, 1.0f / (1.0f + reach));
    const float3 bounded = saturate(s + reach * (s * (1.0f - s) * saturate(1.6f - 2.0f * edge)));
    return SKIN_LEGACY ? legacy : bounded;
}

float3 SkinTransmittance(float d) {
    const float dd = -d * d;
    return float3(0.233f, 0.455f, 0.649f) * exp(dd / 0.0064f)
         + float3(0.1f, 0.336f, 0.344f) * exp(dd / 0.0484f)
         + float3(0.118f, 0.198f, 0.0f) * exp(dd / 0.187f)
         + float3(0.113f, 0.007f, 0.007f) * exp(dd / 0.567f)
         + float3(0.358f, 0.004f, 0.0f) * exp(dd / 1.99f)
         + float3(0.078f, 0.0f, 0.0f) * exp(dd / 7.41f);
}

float3 SkinSpecular(float3 N, float3 V, float3 L, float3 light) {
    const float r0 = clamp(skinRoughness * SKIN_LOBE0, 0.04f, 1.0f);
    const float r1 = clamp(skinRoughness * SKIN_LOBE1, 0.04f, 1.0f);
    const float3 H = normalize(V + L);
    const float NdotL = max(shades(N, L), 0.00001f);
    const float NdotV = max(shades(N, V), 0.00001f);
    const float NdotH = shades(N, H);
    const float LdotH = shades(L, H);
    const float3 fresnel = Fresnel(SKIN_F0.xxx, (1.0f).xxx, LdotH);
    const float3 lobes = lerp(BRDF(r0, fresnel, NdotV, NdotL, NdotH), BRDF(r1, fresnel, NdotV, NdotL, NdotH), SKIN_LOBE_MIX);
    return lobes * (NdotL * PI * skinSpecScale) * light;
}

float3 SkinSheen(float3 N, float3 V, float3 L, float3 light) {
    const float3 H = normalize(V + L);
    const float NdotL = clamp(dot(N, L), 1e-5f, 1.0f);
    const float NdotV = saturate(abs(dot(N, V)) + 1e-5f);
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));
    const float invAlpha = 1.0f / SKIN_SHEEN_ALPHA;
    const float D = (2.0f + invAlpha) * pow(max(1.0f - NdotH * NdotH, 1e-6f), invAlpha * 0.5f) / (2.0f * PI);
    const float visibility = 1.0f / (4.0f * max(NdotL + NdotV - NdotL * NdotV, 1e-5f));
    const float fresnel = Fresnel(SKIN_SHEEN_F0.xxx, (1.0f).xxx, VdotH).x;
    return (D * visibility * fresnel * NdotL * PI * TESR_SkinLook.w) * light;
}

float3 SkinLight(float3 albedo, float3 N, float3 Nsoft, float geometricNdotL, float3 V, float3 L, float3 light, float shadow, float shadowEdge, float selfLit, out float3 specular) {
    L = normalize(L);

    float3 plain;
    const float3 scattered = SkinDiffusion(N, Nsoft, L, plain);
    const float3 direct = min(scattered, SKIN_LEGACY ? saturate(dot(N, L)).xxx : plain);
    const float3 wrapped = scattered - direct;

    float selfShadowed = saturate((0.05f - geometricNdotL) / 0.15f);
    selfShadowed = selfShadowed * selfShadowed * (3.0f - 2.0f * selfShadowed) * saturate(TESR_SkinLook.z) * saturate(selfLit);
    selfShadowed *= SKIN_LEGACY ? 1.0f : saturate((geometricNdotL + 0.45f) * 3.333f);
    const float3 shadowColor = lerp(saturate(shadow).xxx, SkinShadow(shadow, shadowEdge), SKIN_LEGACY ? 1.0f : saturate(geometricNdotL * 4.0f));
    float3 diffuse = albedo * (direct * shadowColor + wrapped * lerp(shadowColor, 1.0f, selfShadowed));
    diffuse *= 1.0f - Fresnel(SKIN_F0.xxx, (1.0f).xxx, saturate(dot(L, normalize(V + L)))).x;

    const float thin = smoothstep(0.3f, 0.9f, skinThinness);
    const float farSide = saturate(0.3f - geometricNdotL);
    const float3 transmitDir = normalize(L + Nsoft * SKIN_TRANSMIT_DISTORTION);
    const float back = pow(saturate(dot(V, -transmitDir)), SKIN_TRANSMIT_POWER);
    const float3 tint = SkinLinearColor(SkinTransmittance(0.25f / max(skinThinness, 0.25f)) * 1.5f);
    const float3 transmitted = albedo * tint * (back * thin * farSide * TESR_SkinMaterial.w * shadow);

    specular = SkinSpecular(N, V, L, light);
    [branch] if (TESR_SkinLook.w > 0.0f)
        specular += SkinSheen(N, V, L, light);
    specular *= shadow;
    return (diffuse + transmitted) * light;
}


float3 SkinSkyRadiance(float3 r, float roughness) {
#if SKYLIGHTING_MODE == 1
    const float3 sky = sqrt(max(GetSkyRadiance(normalize(float3(r.xy, max(r.z, 0.02f)))), 0.0f));
    return sky * lerp(1.0f, 0.4f, saturate(-r.z));
#else
    const float3 plain = SkyAmbientRadiance(r, TESR_PBRExtraData.z);
    float3 sharp = SkyAmbientSharp(r);
    sharp = lerp(sharp, max(sharp, plain), saturate(-r.z));
    return lerp(sharp, plain, saturate(roughness));
#endif
}

float3 SkinSkyReflection(float3 V, float3 geometricNormal, float3 N) {
    const float3 r = reflect(-V, N);
    const float NdotV = saturate(dot(N, V));
    float horizon = saturate(1.0f + 1.2f * dot(r, geometricNormal));
    horizon *= horizon;

    const float r0 = clamp(skinRoughness * SKIN_LOBE0, 0.04f, 1.0f);
    const float r1 = clamp(skinRoughness * SKIN_LOBE1, 0.04f, 1.0f);
    const float response = lerp(EnvBRDFApprox(SKIN_F0.xxx, r0, NdotV), EnvBRDFApprox(SKIN_F0.xxx, r1, NdotV), SKIN_LOBE_MIX).x;
    const float weight = response * horizon * skinSpecScale * TESR_SkinLook.x;
    skinSkyWeight = saturate(weight);

    const float3 radiance = lerp(SkinSkyRadiance(r, r0), SkinSkyRadiance(r, r1), SKIN_LOBE_MIX);
    return LinColor(radiance) * (LinScale(TESR_PBRExtraData.y) * weight);
}

float3 SkinAmbient(float3 ambient, float3 albedo, float3 N, float3 Nsoft) {
    const float3 flat = PBRAmbient(ambient);
    const float3 sharp = SkyAmbient(N, 1.0f);
    const float3 soft = SkyAmbient(Nsoft, 1.0f);
    const float3 sky = float3(soft.r, lerp(sharp.g, soft.g, 0.6f), lerp(sharp.b, soft.b, 0.3f));
    return (flat + sky) * albedo * (1.0f - skinSkyWeight);
}

#endif
