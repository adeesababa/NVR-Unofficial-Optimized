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

#define SKIN_F0 0.028f
#define SKIN_LOBE0_ROUGHNESS 0.75f
#define SKIN_LOBE1_ROUGHNESS 1.3f
#define SKIN_LOBE_MIX 0.15f
#define SKIN_TRANSMIT_TINT float3(1.0f, 0.28f, 0.12f)
#define SKIN_TRANSMIT_DISTORTION 0.3f
#define SKIN_TRANSMIT_POWER 4.0f
#define SKIN_PENUMBRA_TINT float3(0.3f, 0.06f, 0.04f)

#define SKIN_MM_PER_UNIT 14.2857f
#define SKIN_SCATTER_BASE 2.0f

static float skinRoughness = 0.5f;
static float skinSpecScale = 0.0f;
static float skinVanillaMatch = 1.0f;
static float3 skinCompensation = 1.0f;
static float skinCurvature = 0.0f;
static float skinThinness = 0.0f;
static float3 skinSkyReflected = 0.0f;

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
    float3 lobe0 = SkinSpecularLobe(SKIN_F0.rrr, r0, N, V, L0, light) * n0;
    float3 lobe1 = SkinSpecularLobe(SKIN_F0.rrr, r1, N, V, L1, light) * n1;
    return lerp(lobe0, lobe1, SKIN_LOBE_MIX) * skinSpecScale * skinVanillaMatch;
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

    float thin = smoothstep(0.3f, 0.9f, skinThinness);
    float farSide = saturate(0.3f - geometricNdotL);
    float3 transmitDir = normalize(L + Nsoft * SKIN_TRANSMIT_DISTORTION);
    float back = pow(saturate(dot(V, -transmitDir)), SKIN_TRANSMIT_POWER);
    float3 transmitted = albedo * skinLinearTint(SKIN_TRANSMIT_TINT) * (back * thin * farSide * TESR_SkinLightData.w * shadow);

    specular = SkinSpecular(N, V, L, light, sun) * shadow;
    return (diffuse + transmitted + SkinVanillaFill(N, V, L, albedo) * shadow) * light;
}

float3 SkinEnvBRDF(float3 f0, float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

#define SKIN_SKY_GROUND 0.15f
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

    float3 skyOnGround = TESR_SkyIrradiance[0].rgb + TESR_SkyIrradiance[2].rgb + 2.0f * TESR_SkyIrradiance[6].rgb;
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

    float3 reflected = envBRDF * horizon * skinSpecScale * TESR_SkinLightExtra.x;
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
