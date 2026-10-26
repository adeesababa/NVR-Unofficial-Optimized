#ifndef SKINLIGHTING_INCLUDED
#define SKINLIGHTING_INCLUDED

#if defined(__INTELLISENSE__)
    #include "PBR.hlsl"
    #include "PBRScale.hlsl"
#else
    #include "includes/PBR.hlsl"
    #include "includes/PBRScale.hlsl"
#endif

float4 TESR_SkinLightData : register(c192);
float4 TESR_SkinLightExtra : register(c193);

float4 SkinScreenSpaceScatter : register(c194);

float4 TESR_SkinDebugData : register(c195);

float4 TESR_SkinLightCS : register(c196);

#define SKIN_F0 0.028f
#define SKIN_LOBE0_ROUGHNESS 0.75f
#define SKIN_LOBE1_ROUGHNESS 1.3f
#define SKIN_LOBE_MIX 0.15f
#define SKIN_TRANSMIT_TINT float3(1.0f, 0.28f, 0.12f)
#define SKIN_TRANSMIT_DISTORTION 0.3f
#define SKIN_TRANSMIT_POWER 4.0f
#define SKIN_PENUMBRA_TINT float3(0.3f, 0.06f, 0.04f)
#define SKIN_FUZZ_ROUGHNESS 0.35f
#define SKIN_FUZZ_F0 0.045f
#define SKIN_TRANSMIT_PROFILE_DEPTH 0.2f
#define SKIN_TRANSMIT_PROFILE_GAIN 1.5f

#define SKIN_MM_PER_UNIT 14.2857f
#define SKIN_SCATTER_BASE 2.0f

static float skinRoughness = 0.5f;
static float skinSpecScale = 0.0f;
static float skinVanillaMatch = 1.0f;
static float3 skinCompensation = 1.0f;
static float skinCurvature = 0.0f;
static float skinThinness = 0.0f;
static float3 skinSkyReflected = 0.0f;
static float skinCrease = 1.0f;

float3 SkinTransmittanceProfile(float d) {
    float dd = -d * d;
    return float3(0.233f, 0.455f, 0.649f) * exp(dd / 0.0064f)
         + float3(0.1f, 0.336f, 0.344f) * exp(dd / 0.0484f)
         + float3(0.118f, 0.198f, 0.0f) * exp(dd / 0.187f)
         + float3(0.113f, 0.007f, 0.007f) * exp(dd / 0.567f)
         + float3(0.358f, 0.004f, 0.0f) * exp(dd / 1.99f)
         + float3(0.078f, 0.0f, 0.0f) * exp(dd / 7.41f);
}

void setupSkin(float3 normal, float3 vertexNormal, float3 worldPos, float mask, float3 adAlbedo) {
    float wet = saturate(TESR_SkinLightExtra.y);
    float roughness = lerp(TESR_SkinLightData.y, TESR_SkinLightData.y * 0.55f, wet);
    skinRoughness = clamp(SpecularAA(normal, roughness), 0.08f, 1.0f);

    skinSpecScale = mask * 2.0f * TESR_SkinLightData.x * (1.0f + wet);

    skinVanillaMatch = TESR_SkinLightExtra.w;
    skinCompensation = 1.0f / max(LinColor(adAlbedo), 0.125f);

    float dN = length(fwidth(vertexNormal));
    float dP = length(fwidth(worldPos)) * SKIN_MM_PER_UNIT;
    float curvature = dN / max(dP, 1e-4f);
    skinThinness = saturate(curvature * SKIN_SCATTER_BASE);
    skinCurvature = SkinScreenSpaceScatter.x > 0.0f ? 0.0f : saturate(curvature * SKIN_SCATTER_BASE * TESR_SkinLightData.z);
}

float3 skinLinearTint(float3 c) { return LinColor(sqrt(c)); }

float2 SkinEnvBRDFAB(float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

void setupSkinCS(float3 N, float3 V, float3 worldN) {
    float3 dNdx = ddx(worldN);
    float3 dNdy = ddy(worldN);
    float curvature = length(float2(dot(dNdx, dNdx), dot(dNdy, dNdy)));
    skinCrease = TESR_SkinLightCS.z > 0.0f ? saturate(1.0f - curvature) : 1.0f;

    [branch] if (TESR_SkinLightCS.y > 0.0f) {
        float NdotV = saturate(abs(dot(N, V)) + 1e-5f);
        float averageRoughness = lerp(clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f), clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f), SKIN_LOBE_MIX);
        float2 ab = SkinEnvBRDFAB(averageRoughness, NdotV);
        skinSpecScale *= 1.0f + SKIN_F0 * skinCrease * (1.0f / (ab.x + ab.y) - 1.0f);
    }
}

float3 SkinTransmitColor() {
    float3 tint = skinLinearTint(SKIN_TRANSMIT_TINT);
    [branch] if (TESR_SkinLightCS.w > 0.0f)
        tint = skinLinearTint(SkinTransmittanceProfile(SKIN_TRANSMIT_PROFILE_DEPTH / max(skinThinness, 0.2f)) * SKIN_TRANSMIT_PROFILE_GAIN);
    return tint;
}

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

float3 SkinShadow(float shadow) {
    float penumbra = shadow * (1.0f - shadow) * 4.0f;
    return saturate(shadow + penumbra * SKIN_PENUMBRA_TINT * TESR_SkinLightExtra.z);
}

float3 SkinSpecularLobe(float3 f0, float roughness, float3 normal, float3 eyeDir, float3 lightDir, float3 lightColor) {
    const float3 halfway = normalize(eyeDir + lightDir);
    const float NdotL = max(shades(normal, lightDir), 0.00001);
    const float NdotV = max(shades(normal, eyeDir), 0.00001);
    const float NdotH = shades(normal, halfway);
    const float LdotH = shades(lightDir, halfway);
    const float3 fresnel = Fresnel(f0, (1.0).xxx, LdotH);
    return BRDF(roughness, fresnel, NdotV, NdotL, NdotH) * NdotL * lightColor * PI;
}

float3 SkinSunSpecularDir(float3 normal, float3 eyeDir, float3 lightDir, float roughness, out float normalization) {
    const float3 r = reflect(-eyeDir, normal);
    const float3 centerToRay = dot(lightDir, r) * r - lightDir;
    const float3 closest = lightDir + centerToRay * saturate(SUN_RADIUS / max(length(centerToRay), 1e-5f));

    const float alpha = roughness * roughness;
    const float alphaPrime = saturate(alpha + SUN_RADIUS * 0.5f);
    normalization = (alpha / alphaPrime) * (alpha / alphaPrime);

    return normalize(closest);
}

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
    float3 f0 = (SKIN_F0 * skinCrease).rrr;
    float3 lobe0 = SkinSpecularLobe(f0, r0, N, V, L0, light) * n0;
    float3 lobe1 = SkinSpecularLobe(f0, r1, N, V, L1, light) * n1;
    return lerp(lobe0, lobe1, SKIN_LOBE_MIX) * skinSpecScale * skinVanillaMatch;
}

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

float3 SkinVanillaFill(float3 N, float3 V, float3 L, float3 albedo) {
    [branch] if (TESR_SkinDebugData.z <= 0.0f) return 0.0f;
    float NdotL = dot(N, L);
    float a = saturate((NdotL + 0.3f) * 0.769230783f);
    float b = saturate(NdotL);
    float wrap = saturate((3.0f - 2.0f * a) * a * a - (3.0f - 2.0f * b) * b * b);
    float rim = (1.0f - saturate(dot(N, V))) * (1.0f - saturate(dot(N, V))) * saturate(dot(V, -L)) * 0.5f;
    return albedo * ((wrap + rim) * TESR_SkinDebugData.z);
}

#define SKIN_SELF_SHADOW_REACH 16.0f

float3 SkinLight(float3 albedo, float3 N, float3 Nsoft, float3 geometricNormal, float3 V, float3 L, float3 lightColor, float shadow, float selfShadowEscape, bool sun, out float3 specular) {
    float3 light = PBRLight(lightColor);
    L = normalize(L);

    float geometricNdotL = dot(geometricNormal, L);

    float3 scattered = SkinDiffusion(N, Nsoft, L);
    float3 direct = min(scattered, saturate(dot(N, L)));
    float3 wrapped = scattered - direct;

    float selfShadowed = saturate((0.05f - geometricNdotL) / 0.15f);
    selfShadowed = selfShadowed * selfShadowed * (3.0f - 2.0f * selfShadowed) * saturate(TESR_SkinLightExtra.z) * saturate(selfShadowEscape);
    float3 shadowColor = SkinShadow(shadow);
    float3 diffuse = albedo * (direct * shadowColor + wrapped * lerp(shadowColor, 1.0f, selfShadowed));
    [branch] if (TESR_SkinLightCS.y > 0.0f) {
        float Fc = pow(1.0f - saturate(dot(V, normalize(V + L))), 5.0f);
        diffuse *= 1.0f - (Fc + (1.0f - Fc) * (SKIN_F0 * skinCrease));
    }

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

float3 SkinEnvBRDF(float3 f0, float roughness, float NdotV) {
    float2 ab = SkinEnvBRDFAB(roughness, NdotV);
    return f0 * ab.x + ab.y;
}

#define SKIN_SKY_GROUND 0.15f
float3 SkinSkyRadiance(float3 r, float roughness) {
#if SKYLIGHTING_MODE == 1
    return max(GetSkyRadiance(normalize(float3(r.xy, max(r.z, 0.02f)))), 0.0f) * lerp(1.0f, SKIN_SKY_GROUND, saturate(-r.z));
#else
    float band1 = lerp(1.5f, 1.0f, roughness);
    float band2 = lerp(4.0f, 1.0f, roughness);
    float3 radiance = TESR_SkyIrradianceObjects[0].rgb
         + band1 * (TESR_SkyIrradianceObjects[1].rgb * r.y
                  + TESR_SkyIrradianceObjects[2].rgb * r.z
                  + TESR_SkyIrradianceObjects[3].rgb * r.x)
         + band2 * (TESR_SkyIrradianceObjects[4].rgb * (r.x * r.y)
                  + TESR_SkyIrradianceObjects[5].rgb * (r.y * r.z)
                  + TESR_SkyIrradianceObjects[6].rgb * (3.0f * r.z * r.z - 1.0f)
                  + TESR_SkyIrradianceObjects[7].rgb * (r.x * r.z)
                  + TESR_SkyIrradianceObjects[8].rgb * (r.x * r.x - r.y * r.y));

    float3 skyOnGround = TESR_SkyIrradianceObjects[0].rgb + TESR_SkyIrradianceObjects[2].rgb + 2.0f * TESR_SkyIrradianceObjects[6].rgb;
    return max(radiance, 0.0f) + max(skyOnGround, 0.0f) * (SKIN_SKY_GROUND * saturate(-r.z));
#endif
}

float3 SkinSkyReflection(float3 V, float3 geometricNormal, float3 N) {
    float3 r = reflect(-V, N);
    float NdotV = saturate(dot(N, V));

    float horizon = saturate(1.0f + 1.2f * dot(r, geometricNormal));
    horizon *= horizon;

    float r0 = clamp(skinRoughness * SKIN_LOBE0_ROUGHNESS, 0.04f, 1.0f);
    float r1 = clamp(skinRoughness * SKIN_LOBE1_ROUGHNESS, 0.04f, 1.0f);
    float3 radiance = lerp(SkinSkyRadiance(r, r0), SkinSkyRadiance(r, r1), SKIN_LOBE_MIX);
    float3 envBRDF = lerp(SkinEnvBRDF(SKIN_F0.rrr, r0, NdotV), SkinEnvBRDF(SKIN_F0.rrr, r1, NdotV), SKIN_LOBE_MIX);

    float3 reflected = envBRDF * horizon * skinSpecScale * TESR_SkinLightExtra.x * skinCrease;
    skinSkyReflected = saturate(reflected);

    return skinLinearTint(radiance) * reflected;
}

float3 SkinAmbient(float3 ambient, float3 albedo, float3 N, float3 Nsoft) {
    float3 flat = PBRAmbient(ambient);

    float3 sharp = LinColor(SkyAmbientRadiance(N, TESR_PBRExtraData.z));
    float3 soft = LinColor(SkyAmbientRadiance(Nsoft, TESR_PBRExtraData.z));
    float3 sky = float3(soft.r, lerp(sharp.g, soft.g, 0.6f), lerp(sharp.b, soft.b, 0.3f)) * LinScale(TESR_PBRExtraData.y);

    return (flat + sky) * albedo * (1.0f - skinSkyReflected);
}

#endif
