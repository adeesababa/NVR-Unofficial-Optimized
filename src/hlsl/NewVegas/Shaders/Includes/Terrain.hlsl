#if defined(__INTELLISENSE__)
    #include "Helpers.hlsl"
    #include "Pointlights.hlsl"
    #include "PBR.hlsl"
#else
    #include "includes/Pointlights.hlsl"
    #include "includes/PBR.hlsl"
#endif

// UNOFFICIAL 2026-10-17 (deep roadmap). x: IsoHeightTaps -> the parallax HEIGHT taps (march and self-shadow, Parallax.hlsl) use
// one isotropic footprint the size of the pixel's shorter axis: a single trilinear lookup at the mip anisotropic filtering
// would pick, without its extra taps along the long axis (the game's iMaxAnisotropy applies to the land maps); colour and
// normal taps keep the real gradients. In game at noon: ground 4.0 -> 2.4 ms, change 1.3x the frame-to-frame flicker. After
// Wronski, "Assassin's Creed 4: Road to Next-Gen Graphics", GDC 2014 ("turn off aniso filtering on heightmaps").
// y: SkipEmptyLayers -> blendDiffuseMaps/blendNormalMaps skip layers with weight exactly 0 (below).
// c94: free in every land variant (the game's land constants end at c93, Shadow.hlsl starts at c100).
float4 TESR_TerrainParallaxIso : register(c94);
// UNOFFICIAL 2026-10-19 [Shaders.Terrain.LODBlending] (after Community Shaders' LOD Blending, GPL-3.0; Terrain.cpp): x the far land's
// brightness, y its gamma, z 1 = the near land without its vertex colours, w 1 = the far land adjusted (0: untouched). c97: clear of
// the land's own constants, NVR's c89-c96 and Shadow.hlsl's c100-c133.
float4 TESR_LODBlendData : register(c97);

// UNOFFICIAL 2026-10-19 [Shaders.Terrain.Variation] (Includes/TerrainVariation.hlsl, after Conor's port of Community Shaders' Terrain
// Variation): compiled in only when Enabled at startup (TERRAIN_VARIATION from ShaderRecord::LoadShader); the first
// TERRAIN_VARIATION_LAYERS layers (by index) take the two hashed taps. Otherwise the land shaders are exactly what they were.
#ifndef TERRAIN_VARIATION
    #define TERRAIN_VARIATION 0
#endif
#ifndef TERRAIN_VARIATION_LAYERS
    #define TERRAIN_VARIATION_LAYERS 3
#endif
#if TERRAIN_VARIATION
    #if defined(__INTELLISENSE__)
        #include "TerrainVariation.hlsl"
    #else
        #include "includes/TerrainVariation.hlsl"
    #endif
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

float4 TESR_TerrainData : register(c89);
float4 TESR_TerrainExtraData : register(c90);

// Hemisphere skylight, mirroring Includes/Object.hlsl. Terrain never includes that file -- it
// carries its own getVanillaLightingAtt -- so the constants are declared here. c134/c135 are
// clear on this side: the terrain chain tops out at c92 and Shadow.hlsl pins c100-c133.
// [Shaders.Terrain.*] SkylightingScale in .x. Its own constant because TESR_TerrainExtraData is
// full: x usePBR, y saturation, z NoiseScale, w NoiseTile.
float4 TESR_TerrainSkyData : register(c135);

// Additive upper-sky term on top of the weather ambient, weighted by w = (1 + N.up) / 2.
// w must stay linear in the dot product: that is the exact cosine-weighted form factor.
// No separate toggle: a strength of 0 disables it.
#ifndef SKY_AMBIENT_STRENGTH
    #define SKY_AMBIENT_STRENGTH  (TESR_TerrainSkyData.x)    // scale on skyUpper at w = 1
#endif

// UNOFFICIAL 2026-10-17 SkipEmptyLayers (TESR_TerrainParallaxIso.y): a layer whose blend weight is exactly 0 adds exactly 0,
// so its colour and normal lookups are skipped (fixes-b #10: tested != 0, not > 0, so a slightly NEGATIVE weight at an MSAA edge pixel
// still adds its texel, exactly as with the option off) (with the game's anisotropic filtering each one is many taps). Inside a branch
// the lookups take explicit gradients: odx/ody = ddx/ddy of the same uv, taken at the top level (what tex2D used implicitly).
// Harness, real land maps, aniso 15: -12..-14% at 5-7 layers with patchy weights, +0..2% when every layer is present;
// largest pixel difference 3e-7 (rounding).
float3 blendDiffuseMaps(float3 vertexColor, float2 uv, float2 odx, float2 ody, int texCount, sampler2D tex[7], float blends[7]) {
    float3 color = float3(0, 0, 0);
    [unroll] for (int i = 0; i < texCount; i++) {
        [branch] if (blends[i] != 0 || TESR_TerrainParallaxIso.y < 0.5f) color += tex2Dgrad(tex[i], uv, odx, ody).xyz * blends[i];
    }

    return color * vertexColor;
}

#if TERRAIN_VARIATION
// UNOFFICIAL 2026-10-19 Terrain Variation: blendDiffuseMaps with the first TERRAIN_VARIATION_LAYERS layers taking two hashed taps
// where the pixel asks for them (stochastic.tap1Weight below 1; the template sets it to 1 nearer than StartDistance). Each such
// layer's tap weight goes out in tapWeights for its normal map. The i < TERRAIN_VARIATION_LAYERS test is on the unrolled index,
// so it folds away. (After Conor's blendDiffuseMaps, 66653eea.)
float3 blendDiffuseMapsVaried(float3 vertexColor, float2 uv, float2 odx, float2 ody, int texCount, sampler2D tex[7], float blends[7], StochasticOffsets stochastic, inout float tapWeights[7]) {
    float3 color = float3(0, 0, 0);
    [unroll] for (int i = 0; i < texCount; i++) {
        [branch] if (blends[i] != 0 || TESR_TerrainParallaxIso.y < 0.5f) {
            if (i < TERRAIN_VARIATION_LAYERS) {
                [branch] if (stochastic.tap1Weight < 0.999f) color += StochasticSampleAlbedo(tex[i], uv, odx, ody, stochastic, tapWeights[i]).xyz * blends[i];
                else color += tex2Dgrad(tex[i], uv, odx, ody).xyz * blends[i];
            }
            else color += tex2Dgrad(tex[i], uv, odx, ody).xyz * blends[i];
        }
    }
    return color * vertexColor;
}

float3 blendNormalMapsVaried(float2 uv, float2 odx, float2 ody, int texCount, sampler2D tex[7], float blends[7], float spec[7], StochasticOffsets stochastic, float tapWeights[7], out float gloss, out float specExponent) {
    gloss = 0.0f;
    specExponent = 0.0f;
    float3 blendedNormal = float3(0, 0, 0);
    float4 normal;
    [unroll] for (int i = 0; i < texCount; i++) {
        [branch] if (blends[i] != 0 || TESR_TerrainParallaxIso.y < 0.5f) {
        if (i < TERRAIN_VARIATION_LAYERS) {
            [branch] if (stochastic.tap1Weight < 0.999f) normal = StochasticSampleWith(tex[i], uv, odx, ody, stochastic, tapWeights[i]);
            else normal = tex2Dgrad(tex[i], uv, odx, ody);
        }
        else normal = tex2Dgrad(tex[i], uv, odx, ody);
        blendedNormal += normal.xyz * blends[i];
        gloss += normal.w * blends[i] * (spec[i] > 0 ? 1.0f : 0.0f);
        specExponent += spec[i] * blends[i];
        }
    }
    gloss = saturate(gloss);
    return normalize(expand(blendedNormal));
}
#endif

float3 blendNormalMaps(float2 uv, float2 odx, float2 ody, int texCount, sampler2D tex[7], float blends[7], float spec[7], out float gloss, out float specExponent) {
    gloss = 0.0f;
    specExponent = 0.0f;

    float3 blendedNormal = float3(0, 0, 0);

    float4 normal;
    [unroll] for (int i = 0; i < texCount; i++) {
        [branch] if (blends[i] != 0 || TESR_TerrainParallaxIso.y < 0.5f) {   // UNOFFICIAL SkipEmptyLayers (see blendDiffuseMaps)
        normal = tex2Dgrad(tex[i], uv, odx, ody);
        blendedNormal += normal.xyz * blends[i];
        gloss += normal.w * blends[i] * (spec[i] > 0 ? 1.0f : 0.0f);
        specExponent += spec[i] * blends[i];
        }
    }

    gloss = saturate(gloss);
    return normalize(expand(blendedNormal));
}

float3 getVanillaLightingAtt(float3 lightDir, float att, float3 lightColor, float3 viewDir, float3 normal, float3 albedo, float gloss, float glossPower) {
    lightDir = normalize(lightDir);
    viewDir = normalize(viewDir);
    float3 halfwayDir = normalize(lightDir + viewDir);
    
    float NdotL = shades(normal.xyz, lightDir.xyz);
    
    float specStrength = gloss * pow(abs(shades(normal.xyz, halfwayDir.xyz)), glossPower);
    float3 lighting = albedo.rgb * NdotL * lightColor.rgb * att;
    lighting += saturate(((0.2 >= NdotL ? (specStrength * saturate(NdotL + 0.5)) : specStrength) * lightColor.rgb) * att);
    
    return lighting;
}

float3 getPointLightLighting(float3 lightDir, float att, float3 lightColor, float3 eyeDir, float3 normal, float3 albedo, float gloss = 0.0, float glossPower = 0.0, float metallicness = 1.0) {
    float3 pointlightColor = lightColor * LinScale(TESR_TerrainData.z);

    [branch]
    if (TESR_TerrainExtraData.x){
        // PBR. 
        float roughness = saturate((1 - gloss) * TESR_TerrainData.y);
        float3 lighting = PBR(saturate(metallicness * TESR_TerrainData.x), roughness, albedo, normal, eyeDir, lightDir, pointlightColor);
        
        return max(0, lighting * att);
    } else {
        // Vanilla.    
        lightDir = normalize(lightDir);
        
        float3 lighting = getVanillaLightingAtt(lightDir, att, lightColor, eyeDir, normal, albedo, gloss, glossPower);
        
        return lighting;
    }
}

float3 getSunLighting(float3 lightDir, float3 sunColor, float3 eyeDir, float3 normal, float3 AmbientColor, float3 albedo, float gloss = 0.0, float glossPower = 0.0, float metallicness = 1.0, float parallaxMultiplier = 1.0, float3 worldNormal = float3(0.0f, 0.0f, 1.0f)) {
    float3 lightColor = sunColor * LinScale(TESR_TerrainData.z) * parallaxMultiplier;
    float3 ambientColor = AmbientColor * LinScale(TESR_TerrainData.w);

    // Hemisphere skylight, matching getAmbientLighting in Object.hlsl. worldNormal is the
    // geometric world normal, not the tangent-space shading normal used above. AmbientScale
    // (TESR_TerrainData.w) scales the weather ambient above but not this: the sky is a second,
    // independent light source, so SkylightingScale is its only strength knob and it survives
    // AmbientScale = 0.
    ambientColor += LinSky(SkyAmbientRadiance(worldNormal, TESR_TerrainSkyData.y)) * LinScale(SKY_AMBIENT_STRENGTH);
    float3 color = albedo;
    color = lerp(luma(albedo), color, TESR_TerrainExtraData.y);

    [branch]
    if (TESR_TerrainExtraData.x) {
        // PBR.
        float roughness = saturate((1 - gloss) * TESR_TerrainData.y);
        float3 lighting = PBRSun(saturate(metallicness * TESR_TerrainData.x), roughness, color, normal, eyeDir, lightDir, lightColor);
        return max(0, lighting + ambientColor * color);
    } else {
        // Vanilla, no specular.
        float3 lighting = getVanillaLightingAtt(lightDir, 1.0, sunColor, eyeDir, normal, albedo, gloss, glossPower);
        
        return lighting + ambientColor * color;
    }
}
