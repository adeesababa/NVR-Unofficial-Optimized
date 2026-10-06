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

float4 TESR_PBRData : register(c32);
float4 TESR_PBRExtraData : register(c33);

float getRoughness(float gloss) {
    return saturate(max(0.043, 1 - gloss) * TESR_PBRData.y);
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
    float att = vanillaAtt(lightDir, radius);
    
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
    lightColor = lightColor * TESR_PBRData.z;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);
    
    float att = vanillaAtt(lightDir, radius);
    
    #if defined(ONLY_SPECULAR)
        return att * PBRSpecular(0, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #elif defined(SPECULAR)
        return att * PBR(0, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #else
        return att * PBRDiffuse(0, roughness, albedo, normal, viewDir, lightDir, lightColor);
    #endif
}

// UNOFFICIAL: metallic, the share of the pixel that is metal (getDerivedMetallic below; 0 everywhere else).
float3 getPointLightLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float roughness, float metallic) {
    lightColor = lightColor * TESR_PBRData.z;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);

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
    lightColor = lightColor * TESR_PBRData.z;
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



// [_Main.Develop.Main], via Debug.cpp UpdateSettings. c135: c132 is TESR_ShadowBlur.
// Populated even with Shaders.Debug disabled -- Debug has no per-frame UpdateConstants.
float4 TESR_DebugVar : register(c135);

// --- Hemisphere skylight ------------------------------------------------------------------
// Additive upper-sky term on top of the weather ambient, weighted by w = (1 + N.up) / 2.
// w must stay linear in the dot product: that is the exact cosine-weighted form factor.
// [Shaders.PBR.*] SkylightingScale. No separate toggle: 0 disables the term.
#define SKY_AMBIENT_STRENGTH  (TESR_PBRExtraData.y)      // scale on skyUpper at w = 1

float3 getAmbientLighting(float3 ambient, float3 albedo) {
    return ambient * TESR_PBRData.w * albedo;
}

float3 getAmbientLighting(float3 ambient, float3 albedo, float3 worldNormal, float worldNormalValid) {
    float3 flatAmbient = ambient * TESR_PBRData.w;

    // AmbientScale (TESR_PBRData.w) scales the weather ambient above but not this: the sky is a
    // second, independent light source, so SkylightingScale is its only strength knob and it
    // survives AmbientScale = 0.
    float3 skyTerm = SkyAmbientRadiance(worldNormal, TESR_PBRExtraData.z) * SKY_AMBIENT_STRENGTH;

    // worldNormalValid is 0 under a vanilla VS, where the carried world position is undefined.
    return (flatAmbient + skyTerm * worldNormalValid) * albedo;
}

// ---- Metal (UNOFFICIAL) ----------------------------------------------------------------------------------------------
// New Vegas has no metalness map, so every object was lit as a dielectric: a white 4 % highlight over full diffuse. Bare
// metal -- guns above all -- read as plastic: white highlights in the sun, matte in the shade, where nothing reflected
// its surroundings. The normal map's alpha is a highlight-STRENGTH mask, not a gloss map: a gun's body is mostly near 0
// (the 10mm pistol's median is 0.06) with its edges and worn spots painted high, so it cannot find the metal alone.
// Metal is guessed per pixel instead: grey (unsaturated) colour counts as metal, partly, and the mask makes it fully
// metal and polished. [Shaders.PBR.Metal] sets the strength separately for the first-person model and for the world
// (PBRShaders swaps them round the first-person pass; World 0 leaves the world as it was). docs/derived-metallicness.md.
float4 TESR_PBRMetal     : register(c34); // x: strength this pass (0 off), y: metal share of grey parts the mask leaves bare, z/w: colour saturation where metal starts/stops being ruled out
float4 TESR_PBRMetalLook : register(c36); // x: roughness of bare metal, y: of fully masked (polished) metal, z: sky reflection strength, w: 1 debug view

float getMetalMask(float specMask) {
    return smoothstep(0.05f, 0.45f, specMask);
}

float getDerivedMetallic(float specMask, float3 albedo) {
    float peak = max(max(albedo.r, albedo.g), albedo.b);
    float chroma = (peak - min(min(albedo.r, albedo.g), albedo.b)) / max(peak, 0.001f);
    float greyness = 1.0f - smoothstep(TESR_PBRMetal.z, max(TESR_PBRMetal.w, TESR_PBRMetal.z + 0.001f), chroma);
    return saturate(greyness * lerp(TESR_PBRMetal.y, 1.0f, getMetalMask(specMask)) * TESR_PBRMetal.x);
}

// Metal's roughness: satin where the mask is bare, polished where it is painted (the mask is no gloss map, see above).
float getMetalRoughness(float specMask) {
    return lerp(TESR_PBRMetalLook.x, TESR_PBRMetalLook.y, getMetalMask(specMask));
}

// The split-sum environment response (scale and bias on F0) for a roughness and N.V: Karis, "Physically Based Shading on
// Mobile" (2014).
float3 EnvBRDFApprox(float3 f0, float roughness, float NdotV) {
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    float2 ab = float2(-1.04f, 1.04f) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}

// The sky's light along a direction of world up component z, averaged round the horizon (the object shaders know which
// way is up per pixel, not the compass bearing): SkyAmbientRadiance's scale and encoding. Mode 0: the spherical harmonic
// terms that do not depend on the bearing.
float3 SkyAmbientElevation(float z) {
    #if SKYLIGHTING_MODE == 1
        return SkyAmbientRadiance(float3(sqrt(saturate(1.0f - z * z)), 0.0f, z), TESR_PBRExtraData.z);
    #else
        float3 irradiance = TESR_SkyIrradiance[0].rgb + TESR_SkyIrradiance[2].rgb * z + TESR_SkyIrradiance[6].rgb * (3.0f * z * z - 1.0f);
        return sqrt(max(irradiance, 0.0f));
    #endif
}

// What metal reflects in place of the diffuse light it lacks: the weather ambient and the sky along the reflected view
// (upReflect: that view's world up component; skyValid 0 when it is unknown), weighted by the split-sum response for
// the metal's colour (F0) and roughness. getAmbientLighting's terms, with the reflection for the normal.
float3 getMetalAmbient(float3 ambient, float3 f0, float roughness, float NdotV, float upReflect, float skyValid) {
    float3 env = ambient * TESR_PBRData.w + SkyAmbientElevation(upReflect) * SKY_AMBIENT_STRENGTH * skyValid;
    return EnvBRDFApprox(f0, roughness, NdotV) * env * TESR_PBRMetalLook.z;
}
