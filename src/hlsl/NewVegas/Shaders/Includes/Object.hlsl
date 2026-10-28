#if defined(__INTELLISENSE__)
    #include "Pointlights.hlsl"
    #include "PBR.hlsl"
#else
    #include "includes/Pointlights.hlsl"
    #include "includes/PBR.hlsl"
#endif

#if defined(__INTELLISENSE__)
    #include "SkyAmbient.hlsl"
#else
    #include "includes/SkyAmbient.hlsl"
#endif

#if defined(__INTELLISENSE__)
    #include "LinearLight.hlsl"
#else
    #include "includes/LinearLight.hlsl"
#endif
#define LinSky(c) LinColor(c)

float4 TESR_PBRData : register(c32);
float4 TESR_PassRedirect : register(c210);
float4 TESR_PBRExtraData : register(c33);
#ifdef LIN_PREDECODED
float4 TESR_LinearScales : register(c168);
#define LIN_SCALE_LIGHT TESR_LinearScales.x
#define LIN_SCALE_AMBIENT TESR_LinearScales.y
#define LIN_SCALE_SKY TESR_LinearScales.z
#else
#define LIN_SCALE_LIGHT LinScale(TESR_PBRData.z)
#define LIN_SCALE_AMBIENT LinScale(TESR_PBRData.w)
#define LIN_SCALE_SKY LinScale(SKY_AMBIENT_STRENGTH)
#endif

#ifdef MATERIAL_RULES
float4 TESR_MaterialRule  : register(c197);
float4 TESR_MaterialRough : register(c198);
float4 TESR_MaterialTint  : register(c199);
#endif

#ifdef RMAOS
float4 NVR_Rmaos : register(c220);
sampler2D NVR_RmaosMap : register(s2);
static float4 RmaosValue = float4(1.0f, 0.0f, 1.0f, 1.0f);
#define RMAOS_ACTIVE (NVR_Rmaos.x > 0.0f)
#ifdef RMAOS_NO_FADE
    #define RMAOS_FADING false
#else
    #define RMAOS_FADING (NVR_Rmaos.x < 1.0f)
#endif

void RmaosRead(float2 uv) {
    float2 dx = ddx(uv), dy = ddy(uv);
    [branch] if (RMAOS_ACTIVE) {
        RmaosValue = tex2Dgrad(NVR_RmaosMap, uv, dx, dy);
        RmaosValue.rg = saturate(RmaosValue.rg + RmaosValue.rg * TESR_PBRMath.wz);
    }
}

float RmaosAmbient() {
    return RMAOS_ACTIVE ? lerp(1.0f, RmaosValue.b, RMAOS_FADING ? saturate(NVR_Rmaos.y) * NVR_Rmaos.x : saturate(NVR_Rmaos.y)) : 1.0f;
}

float3 RmaosDebug(float3 color, float3 albedo) {
    [branch] if (NVR_Rmaos.z > 0.5f) {
        #ifdef ONLY_LIGHT
            return 0.0f;
        #else
            float dim = luma(albedo) * 0.35f;
            if (NVR_Rmaos.z > 3.5f) return RMAOS_ACTIVE ? lerp(dim.xxx, float3(0.1f, 0.9f, 0.2f), 0.6f) : dim.xxx;
            if (!RMAOS_ACTIVE) return dim.xxx;
            float value = NVR_Rmaos.z < 1.5f ? RmaosValue.r : NVR_Rmaos.z < 2.5f ? RmaosValue.g : RmaosValue.b;
            return value.xxx;
        #endif
    }
    return color;
}
#else
float RmaosAmbient() { return 1.0f; }
float3 RmaosDebug(float3 color, float3 albedo) { return color; }
#endif

#if defined(RMAOS) && defined(REFL_MATERIAL_ON) && REFL_MATERIAL_ON
float4 NVR_ReflMaterial : register(c212);
float ReflMaterialAlpha(float alpha) {
    #ifdef NVR_SLOT_TIGHT
        const float metal = 0.0f;
    #else
        float metal = floor(saturate(RmaosValue.g * 2.5f - 1.5f) * 3.0f + 0.5f);
    #endif
    float roughness = saturate(RmaosValue.r * NVR_ReflMaterial.y + NVR_ReflMaterial.z);
    float encoded = 2.0f + metal * 0.25f + roughness * 0.24f;
    return lerp(alpha, encoded, NVR_ReflMaterial.x * (alpha >= 0.999f ? 1.0f : 0.0f));
}
#else
#define ReflMaterialAlpha(alpha) (alpha)
#endif

float getRoughness(float gloss) {
    #ifdef RMAOS
        if (RMAOS_ACTIVE && !RMAOS_FADING) return max(RmaosValue.r, 0.02f);
    #endif
    float roughness = saturate(max(0.043, 1 - gloss) * TESR_PBRData.y);
    #ifdef MATERIAL_RULES
        [flatten] if (TESR_MaterialRule.x > 0.0f && TESR_MaterialRough.x >= 0.0f)
            roughness = lerp(TESR_MaterialRough.x, TESR_MaterialRough.y, saturate(1.0f - gloss));
    #endif
    #ifdef RMAOS
        if (RMAOS_ACTIVE) roughness = RMAOS_FADING ? lerp(roughness, max(RmaosValue.r, 0.02f), NVR_Rmaos.x) : max(RmaosValue.r, 0.02f);
    #endif
    return roughness;
}

float getRoughness(float glossmap, float meshgloss){
    // return pow(glossmap, log(meshgloss));    
    // no gloss = 1
    // full gloss = 0

    return saturate(1 - log(meshgloss) / 4 * glossmap);
    // return 1 - saturate(log(meshgloss)/4 + glossmap);
    // return pow(1 - glossmap, meshgloss);
}

// Vanilla
float3 getVanillaLighting(float3 lightDir, float radius, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float gloss, float glossPower) {
    float att = LampAtt(vanillaAtt(lightDir, radius));
    
    lightDir = normalize(lightDir);
    viewDir = normalize(viewDir);
    float3 halfwayDir = normalize(lightDir + viewDir);
    
    float NdotL = shades(normal.xyz, lightDir.xyz);
    
    #if defined(ONLY_SPECULAR)
        float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
        float3 lighting = saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    #elif defined(SPECULAR)
        float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
        float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
        lighting += saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    #else
        float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
    #endif
    
    return lighting;
}

float3 getVanillaLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float gloss, float glossPower) {
    lightDir = normalize(lightDir);
    viewDir = normalize(viewDir);
    float3 halfwayDir = normalize(lightDir + viewDir);
    
    float NdotL = shades(normal.xyz, lightDir.xyz);
    
    #if defined(ONLY_SPECULAR)
        float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
        float3 lighting = saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    #elif defined(SPECULAR)
        float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
        float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
        lighting += saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    #else
        float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
    #endif
    
    return lighting;
}

// PBR
float3 getPointLightLighting(float3 lightDir, float radius, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness) {
    lightColor = lightColor * LIN_SCALE_LIGHT;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);
    
    float att = LampAtt(vanillaAtt(lightDir, radius));
    
    #if defined(ONLY_SPECULAR)
        return att * PBRSpecular(0, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #elif defined(SPECULAR)
        return att * PBR(0, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #else
        return att * PBRDiffuse(0, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #endif
}

float3 getPointLightLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float metallic) {
    lightColor = lightColor * LIN_SCALE_LIGHT;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);
    att = LampAtt(att);

    #if defined(ONLY_SPECULAR)
        return att * PBRSpecular(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #elif defined(SPECULAR)
        return att * PBR(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #else
    return att * PBRDiffuse(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #endif
}

float3 getPointLightLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness) {
    return getPointLightLightingAtt(lightDir, att, lightColor, viewDir, normal, albedo, roughness, 0.0f);
}

float3 getSunLighting(float3 lightDir, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float metallic) {
    lightColor = lightColor * LIN_SCALE_LIGHT;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);

    #if defined(ONLY_SPECULAR)
        return PBRSunSpecular(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #elif defined(SPECULAR)
        return PBRSun(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #else
        return PBRDiffuse(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #endif
}

float3 getSunLighting(float3 lightDir, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness) {
    return getSunLighting(lightDir, lightColor, viewDir, normal, albedo, roughness, 0.0f);
}

#ifdef NVR_FOLD
float3 getPointLightLightingAttFull(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float metallic) {
    lightColor = lightColor * LIN_SCALE_LIGHT;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);
    att = LampAtt(att);
    return att * PBR(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
}

float3 getSunLightingFull(float3 lightDir, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float metallic) {
    lightColor = lightColor * LIN_SCALE_LIGHT;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);
    return PBRSun(metallic, roughness, albedo, normal, viewDir, lightDir, lightColor);
}
#endif



// [_Main.Develop.Main], via Debug.cpp UpdateSettings. c135: c132 is TESR_ShadowBlur.
// Populated even with Shaders.Debug disabled -- Debug has no per-frame UpdateConstants.
float4 TESR_DebugVar : register(c135);

// --- Hemisphere skylight ------------------------------------------------------------------
// Additive upper-sky term on top of the weather ambient, weighted by w = (1 + N.up) / 2.
// w must stay linear in the dot product: that is the exact cosine-weighted form factor.
// [Shaders.PBR.*] SkylightingScale. No separate toggle: 0 disables the term.
#define SKY_AMBIENT_STRENGTH  (TESR_PBRExtraData.y)      // scale on skyUpper at w = 1

float3 getAmbientLighting(float3 ambient, float3 albedo) {
    return ambient * LIN_SCALE_AMBIENT * albedo * RmaosAmbient();
}

float3 getAmbientLighting(float3 ambient, float3 albedo, float3 worldNormal, float worldNormalValid) {
    float3 flatAmbient = ambient * LIN_SCALE_AMBIENT;

    // AmbientScale (TESR_PBRData.w) scales the weather ambient above but not this: the sky is a
    // second, independent light source, so SkylightingScale is its only strength knob and it
    // survives AmbientScale = 0.
    float3 skyTerm = LinSky(SkyAmbientRadiance(worldNormal, TESR_PBRExtraData.z)) * LIN_SCALE_SKY;

    // worldNormalValid is 0 under a vanilla VS, where the carried world position is undefined.
    return (flatAmbient + skyTerm * worldNormalValid) * albedo * RmaosAmbient();
}

float4 TESR_PBRMetal     : register(c34);
float4 TESR_PBRMetalLook : register(c36);

float getMetalMask(float specMask) {
    return smoothstep(0.05f, 0.45f, specMask);
}

float getMetalStrength() {
    #ifdef RMAOS
        if (RMAOS_ACTIVE) return 1.0f;
    #endif
    #ifdef MATERIAL_RULES
        if (TESR_MaterialRule.x > 0.0f) return TESR_MaterialRule.y;
    #endif
    return TESR_PBRMetal.x;
}

float getDerivedMetallicGuess(float specMask, float greyness) {
    #ifdef MATERIAL_RULES
        [branch] if (TESR_MaterialRule.x > 0.0f)
            return TESR_MaterialRule.z > 1.5f ? TESR_MaterialRule.y : saturate(greyness * lerp(TESR_PBRMetal.y, 1.0f, getMetalMask(specMask)) * TESR_MaterialRule.y);
    #endif
    return saturate(greyness * lerp(TESR_PBRMetal.y, 1.0f, getMetalMask(specMask)) * TESR_PBRMetal.x);
}

float getDerivedMetallic(float specMask, float3 albedo) {
    float peak = max(max(albedo.r, albedo.g), albedo.b);
    float chroma = (peak - min(min(albedo.r, albedo.g), albedo.b)) / max(peak, 0.001f);
    float greyness = 1.0f - smoothstep(TESR_PBRMetal.z, max(TESR_PBRMetal.w, TESR_PBRMetal.z + 0.001f), chroma);
    #ifdef RMAOS
        [branch] if (RMAOS_ACTIVE && NVR_Rmaos.w > 0.5f) return RMAOS_FADING ? lerp(getDerivedMetallicGuess(specMask, greyness), RmaosValue.g, NVR_Rmaos.x) : RmaosValue.g;
    #endif
    return getDerivedMetallicGuess(specMask, greyness);
}

float getMetalFade(float3 cameraRelativePos) {
    return TESR_PBRExtraData.w > 0.0f ? 1.0f - smoothstep(0.6f * TESR_PBRExtraData.w, TESR_PBRExtraData.w, length(cameraRelativePos)) : 1.0f;
}

float getMetalRoughness(float specMask) {
    #ifdef RMAOS
        if (RMAOS_ACTIVE && !RMAOS_FADING) return max(RmaosValue.r, 0.02f);
    #endif
    float roughness = lerp(TESR_PBRMetalLook.x, TESR_PBRMetalLook.y, getMetalMask(specMask));
    #ifdef MATERIAL_RULES
        [flatten] if (TESR_MaterialRule.x > 0.0f && TESR_MaterialRough.x >= 0.0f)
            roughness = lerp(TESR_MaterialRough.y, TESR_MaterialRough.x, getMetalMask(specMask));
    #endif
    #ifdef RMAOS
        if (RMAOS_ACTIVE) roughness = RMAOS_FADING ? lerp(roughness, max(RmaosValue.r, 0.02f), NVR_Rmaos.x) : max(RmaosValue.r, 0.02f);
    #endif
    return roughness;
}

float getMaterialReflections() {
    #ifdef MATERIAL_RULES
        if (TESR_MaterialRule.x > 0.0f) return TESR_MaterialRule.w;
    #endif
    return 1.0f;
}

float3 MaterialRulesDebug(float3 color, float3 albedo) {
    #ifdef MATERIAL_RULES
        [branch] if (TESR_MaterialRough.z > 0.5f) {
            #ifdef ONLY_LIGHT
                return 0.0f;
            #else
                float dim = luma(albedo) * 0.35f;
                return TESR_MaterialRule.x > 0.0f ? lerp(dim.xxx, TESR_MaterialTint.rgb, 0.7f) : dim.xxx;
            #endif
        }
    #endif
    return color;
}


float3 SkyAmbientElevation(float z) {
    #if SKYLIGHTING_MODE == 1
        return SkyAmbientRadiance(float3(sqrt(saturate(1.0f - z * z)), 0.0f, z), TESR_PBRExtraData.z);
    #else
        float3 irradiance = TESR_SkyIrradianceObjects[0].rgb + TESR_SkyIrradianceObjects[2].rgb * z + TESR_SkyIrradianceObjects[6].rgb * (3.0f * z * z - 1.0f);
        return sqrt(max(irradiance, 0.0f));
    #endif
}

float3 getMetalSkyDirection(float3 worldPos, float3 worldNormal, float upReflect) {
    float3 r = reflect(normalize(worldPos), worldNormal);
    float horizontal = length(r.xy);
    float2 bearing = horizontal > 1e-4f ? r.xy / horizontal : float2(1.0f, 0.0f);
    return float3(bearing * sqrt(saturate(1.0f - upReflect * upReflect)), upReflect);
}

float3 getMetalAmbient(float3 ambient, float3 f0, float roughness, float NdotV, float upReflect, float skyValid,
                       float3 worldPos, float3 worldNormal) {
    float3 sky = SkyAmbientElevation(upReflect);
    [branch] if (TESR_SharpSky.x > 0.0f && skyValid > 0.0f)
        sky = SkyAmbientSharpened(getMetalSkyDirection(worldPos, worldNormal, upReflect), sky);
    float3 env = ambient * LIN_SCALE_AMBIENT + LinSky(sky) * LIN_SCALE_SKY * skyValid;
    #ifdef NVR_LIVE_CUBE
    [branch] if (TESR_EnvCube.x > 0.0f && skyValid > 0.0f) {
        float4 live = LiveCube(getMetalSkyDirection(worldPos, worldNormal, upReflect), roughness);
        env = lerp(env, LinSky(live.rgb), TESR_EnvCube.x * live.a);
    }
    #endif
    return EnvBRDFApprox(f0, roughness, NdotV) * env * TESR_PBRMetalLook.z * getMaterialReflections() * RmaosAmbient();
}
