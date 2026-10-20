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

// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl): where a template defines LINEAR_LIGHTING the sky's colour is decoded
// too (LinColor); otherwise LinColor returns it unchanged.
#if defined(__INTELLISENSE__)
    #include "LinearLight.hlsl"
#else
    #include "includes/LinearLight.hlsl"
#endif
#define LinSky(c) LinColor(c)

float4 TESR_PBRData : register(c32);
float4 TESR_PassRedirect : register(c210);	// UNOFFICIAL ExactPassSums (core/PassSums.h): x 1 = this added pass draws into NVR's buffer, linear
float4 TESR_PBRExtraData : register(c33);
// UNOFFICIAL 2026-10-17 LinearPreDecode (ObjectTemplate.hlsl LIN_PREDECODED): the strengths LinScale would decode per pixel, decoded
// once per frame on the CPU (PBR.cpp); otherwise the same LinScale calls as before.
#ifdef LIN_PREDECODED
float4 TESR_LinearScales : register(c168); // x LinScale(TESR_PBRData.z), y LinScale(TESR_PBRData.w), z LinScale(TESR_PBRExtraData.y)
#define LIN_SCALE_LIGHT TESR_LinearScales.x
#define LIN_SCALE_AMBIENT TESR_LinearScales.y
#define LIN_SCALE_SKY TESR_LinearScales.z
#else
#define LIN_SCALE_LIGHT LinScale(TESR_PBRData.z)
#define LIN_SCALE_AMBIENT LinScale(TESR_PBRData.w)
#define LIN_SCALE_SKY LinScale(SKY_AMBIENT_STRENGTH)
#endif

// UNOFFICIAL material rules ([Shaders.PBR.MaterialRules], core/MaterialRules.h): the class of the texture being drawn, from
// the editable table NewVegasReloaded.dll.materials.txt, written by NVR before each object (Render.cpp MaterialRulesDraw).
// Read only where MATERIAL_RULES is defined (ObjectTemplate). While TESR_MaterialRule.x is 0 (rules off, or no rule for
// the texture) every function below returns exactly what it did without the rules.
#ifdef MATERIAL_RULES
float4 TESR_MaterialRule  : register(c197); // x: 1 a rule applies, y: metal strength (0 = none), z: 2 = all metal (else the guess), w: reflections multiplier
float4 TESR_MaterialRough : register(c198); // x/y: roughness where the highlight mask is full / bare (x < 0: the global roughness), z: 1 DebugView
float4 TESR_MaterialTint  : register(c199); // rgb: the class's DebugView colour
#endif

// UNOFFICIAL 2026-10-14 _rmaos maps ([Shaders.PBR.Rmaos], Render.cpp RmaosDraw): where a texture has a <name>_rmaos.dds (the
// Community Shaders "True PBR" layout: r roughness, g metallic, b ambient occlusion with white = none, a specular level), NVR binds
// it on s2 (no object shader samples s2, and the game binds nothing there for objects) and sets NVR_Rmaos.x for the draw. The
// map's roughness and metal then replace the guesses from the normal map's alpha and the colour, and its occlusion darkens the
// ambient light. RmaosRead samples it once at the top of main (derivatives); everything below reads RmaosValue. Defined only
// in ObjectTemplate's pixel shaders, and only when on at startup (RMAOS_ON); while NVR_Rmaos.x is 0 every function returns
// exactly what it did before.
#ifdef RMAOS
float4 NVR_Rmaos : register(c220); // x: 1 = this draw has an _rmaos map on s2, y: occlusion strength, z: DebugView (1 roughness, 2 metal, 3 occlusion, 4 which objects have one)
sampler2D NVR_RmaosMap : register(s2);
static float4 RmaosValue = float4(1.0f, 0.0f, 1.0f, 1.0f);
// UNOFFICIAL 2026-10-18 FadeIn (pbrpop, [Shaders.PBR.Rmaos] FadeIn): x below 1 = the map arrived a moment ago: its values blend in
// from what the object had without it (x = the map's share), so a map that arrives while the object is in view does not pop. x = 1:
// exactly the map, as before; 0: no map.
#define RMAOS_ACTIVE (NVR_Rmaos.x > 0.0f)
#ifdef RMAOS_NO_FADE
    #define RMAOS_FADING false	// (the includer's shader is near its slot limit: the map comes in at once there)
#else
    #define RMAOS_FADING (NVR_Rmaos.x < 1.0f)
#endif

// UNOFFICIAL 2026-10-18 per-weather RoughnessScale / MetalnessScale for the maps ([Shaders.PBR.Main/Rain/Night/NightRain/Interiors],
// PBRShaders::UpdateConstants; idea from Physically-Based-Reloaded (Balebandro), GPL-3.0, commit c1415cb8). TESR_PBRMath (PBR.hlsl)
// z = MetalnessScale - 1, w = RoughnessScale - 1, so a register never written (0) and the neutral 1.0 defaults leave the map's
// values exactly as read (x + x * 0 = x); the map's own values are in 0-1, so the saturate changes nothing there either.
// RoughnessScale is 1-2 (rougher only, never above 1), MetalnessScale 0-1. AOScale needs no shader change (c220.y, RmaosDraw).
void RmaosRead(float2 uv) {
    float2 dx = ddx(uv), dy = ddy(uv);
    [branch] if (RMAOS_ACTIVE) {
        RmaosValue = tex2Dgrad(NVR_RmaosMap, uv, dx, dy);
        RmaosValue.rg = saturate(RmaosValue.rg + RmaosValue.rg * TESR_PBRMath.wz);
    }
}

// The ambient light's share the map's occlusion leaves (1 without a map).
float RmaosAmbient() {
    return RMAOS_ACTIVE ? lerp(1.0f, RmaosValue.b, RMAOS_FADING ? saturate(NVR_Rmaos.y) * NVR_Rmaos.x : saturate(NVR_Rmaos.y)) : 1.0f;
}

// DebugView: the map's roughness, metal or occlusion in grey; 4: objects with a map in green, others dim grey.
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
        if (RMAOS_ACTIVE) roughness = RMAOS_FADING ? lerp(roughness, max(RmaosValue.r, 0.02f), NVR_Rmaos.x) : max(RmaosValue.r, 0.02f);	// FadeIn
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
    float att = LinFalloff(vanillaAtt(lightDir, radius));	// UNOFFICIAL 2026-10-20 (LinearLight.hlsl)
    
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
    
    float att = LinFalloff(vanillaAtt(lightDir, radius));	// UNOFFICIAL 2026-10-20: the falloff decoded like the colours (LinearLight.hlsl)
    
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
    lightColor = lightColor * LIN_SCALE_LIGHT;
    albedo = lerp(luma(albedo), albedo, TESR_PBRExtraData.x);
    att = LinFalloff(att);	// UNOFFICIAL 2026-10-20: the falloff decoded like the colours (LinearLight.hlsl)

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

// This draw's metal strength: [Shaders.PBR.Metal] for the pass, or the material rule's (0 for a class that is never metal).
// Uniform over the draw, so the metal branches on it stay cheap.
float getMetalStrength() {
    #ifdef RMAOS
        if (RMAOS_ACTIVE) return 1.0f;	// the map says where the metal is
    #endif
    #ifdef MATERIAL_RULES
        if (TESR_MaterialRule.x > 0.0f) return TESR_MaterialRule.y;
    #endif
    return TESR_PBRMetal.x;
}

float getDerivedMetallicGuess(float specMask, float greyness) {
    #ifdef MATERIAL_RULES
        // A rule's "full" metal (chrome, brass) is metal whatever its colour; otherwise the guess at the rule's strength.
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
        [branch] if (RMAOS_ACTIVE) return RMAOS_FADING ? lerp(getDerivedMetallicGuess(specMask, greyness), RmaosValue.g, NVR_Rmaos.x) : RmaosValue.g;
    #endif
    return getDerivedMetallicGuess(specMask, greyness);
}

// The metal fades out with distance, from 60% of TESR_PBRExtraData.w to all of it (w 0: never). PBRShaders keeps w short of
// where the game starts dropping objects' highlights: beyond it the game draws them without, and the metal went at once.
float getMetalFade(float3 cameraRelativePos) {
    return TESR_PBRExtraData.w > 0.0f ? 1.0f - smoothstep(0.6f * TESR_PBRExtraData.w, TESR_PBRExtraData.w, length(cameraRelativePos)) : 1.0f;
}

// Metal's roughness: satin where the mask is bare, polished where it is painted (the mask is no gloss map, see above).
float getMetalRoughness(float specMask) {
    #ifdef RMAOS
        if (RMAOS_ACTIVE && !RMAOS_FADING) return max(RmaosValue.r, 0.02f);
    #endif
    float roughness = lerp(TESR_PBRMetalLook.x, TESR_PBRMetalLook.y, getMetalMask(specMask));
    #ifdef MATERIAL_RULES
        // A rule's roughness range: its bare end where the mask is bare, its polished end where the mask is painted.
        [flatten] if (TESR_MaterialRule.x > 0.0f && TESR_MaterialRough.x >= 0.0f)
            roughness = lerp(TESR_MaterialRough.y, TESR_MaterialRough.x, getMetalMask(specMask));
    #endif
    #ifdef RMAOS
        if (RMAOS_ACTIVE) roughness = RMAOS_FADING ? lerp(roughness, max(RmaosValue.r, 0.02f), NVR_Rmaos.x) : max(RmaosValue.r, 0.02f);	// FadeIn
    #endif
    return roughness;
}

// The metal's reflections of its surroundings, times the rule's (1 without one).
float getMaterialReflections() {
    #ifdef MATERIAL_RULES
        if (TESR_MaterialRule.x > 0.0f) return TESR_MaterialRule.w;
    #endif
    return 1.0f;
}

// [Shaders.PBR.MaterialRules] DebugView: each class in its colour over a dim grey image, textures without a rule grey.
// Passes that only add lamp light add nothing while it is on.
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

// UNOFFICIAL SharpSky: the reflected view in world space, for SkyAmbientSharpened. Its bearing is the reflection of the geometric
// normal (worldPos is camera-relative, so the view ray is its direction); its height is upReflect, so the normal map's bumps still
// show. The tangent frame carries no world bearing, so a bump does not turn the sky sideways.
float3 getMetalSkyDirection(float3 worldPos, float3 worldNormal, float upReflect) {
    float3 r = reflect(normalize(worldPos), worldNormal);
    float horizontal = length(r.xy);
    float2 bearing = horizontal > 1e-4f ? r.xy / horizontal : float2(1.0f, 0.0f);
    return float3(bearing * sqrt(saturate(1.0f - upReflect * upReflect)), upReflect);
}

// What metal reflects in place of the diffuse light it lacks: the weather ambient and the sky along the reflected view
// (upReflect: that view's world up component; skyValid 0 when it is unknown), weighted by the split-sum response for
// the metal's colour (F0) and roughness. getAmbientLighting's terms, with the reflection for the normal.
// UNOFFICIAL SharpSky: with it on and the sky known, the sky is taken along the reflected direction (getMetalSkyDirection, from
// worldPos and the geometric worldNormal), blended from the plain sky. Off, this is the plain sky exactly.
float3 getMetalAmbient(float3 ambient, float3 f0, float roughness, float NdotV, float upReflect, float skyValid,
                       float3 worldPos, float3 worldNormal) {
    float3 sky = SkyAmbientElevation(upReflect);
    [branch] if (TESR_SharpSky.x > 0.0f && skyValid > 0.0f)
        sky = SkyAmbientSharpened(getMetalSkyDirection(worldPos, worldNormal, upReflect), sky);
    float3 env = ambient * LIN_SCALE_AMBIENT + LinSky(sky) * LIN_SCALE_SKY * skyValid;
    // UNOFFICIAL CubeReflections ([Shaders.PBR.Metal], ObjectTemplate defines NVR_LIVE_CUBE): what the camera saw round it
    // along the reflected view (includes/LiveCube.hlsl) in place of the ambient and sky, as far as the cube has seen that
    // direction. Needs the direction, so only where the sky is known (skyValid).
    #ifdef NVR_LIVE_CUBE
    [branch] if (TESR_EnvCube.x > 0.0f && skyValid > 0.0f) {
        float4 live = LiveCube(getMetalSkyDirection(worldPos, worldNormal, upReflect), roughness);
        env = lerp(env, LinSky(live.rgb), TESR_EnvCube.x * live.a);
    }
    #endif
    return EnvBRDFApprox(f0, roughness, NdotV) * env * TESR_PBRMetalLook.z * getMaterialReflections() * RmaosAmbient();
}
