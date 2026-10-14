//  Template for terrain shaders for blending up to 7 textures and using up to 8 pointlights.

#if defined(__INTELLISENSE__)
    #define PS
    #define NUM_PT_LIGHTS 24
#endif

#define TERRAIN

#define REVERSED_DEPTH

#ifndef TEX_COUNT
    #define TEX_COUNT 1
#endif

#if defined(NUM_PT_LIGHTS) && (NUM_PT_LIGHTS > 24)
    #define NUM_PT_LIGHTS 24
#endif

#include "includes/Helpers.hlsl"
// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl, [Shaders.PBR.Linear]): the land lights in linear space while it is on.
// Compiled in only when on at startup (LINEAR_LIGHTING_ON, ShaderRecord::LoadShader).
#ifndef LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING_ON 0
#endif
#if defined(PS) && LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING
#endif
#include "includes/Terrain.hlsl"
#include "includes/Parallax.hlsl"

// BaseMap[7] holds s0-s6 and NormalMap[7] holds s7-s13, so the atlas cannot use the s9
// default. s14/s15 are the only free sampler slots in ps_3_0 here.
#define SHADOW_ATLAS_SAMPLER_REG s14
#define SHADOW_OLD_ATLAS_SAMPLER_REG s15 // the sun cross-fade's kept atlas (SUN_CROSSFADE only)
#include "includes/Shadow.hlsl"

// UNOFFICIAL terrain blending ([Shaders.Terrain.Blending]), ported from Balebandro's Physically-Based-Reloaded fork
// (Alejandro Martinez, fork commit a23a624b), which follows the idea of Community Shaders' Terrain Blending (Skyrim).
// Compiled in only when Enabled at startup (TERRAIN_BLEND from ShaderRecord::LoadShader), so that otherwise these shaders
// are exactly what they were. It needs s15, which the sun cross-fade's kept atlas takes: not with SUN_CROSSFADE.
#ifndef TERRAIN_BLEND
    #define TERRAIN_BLEND 0
#endif
#if TERRAIN_BLEND && !SUN_CROSSFADE
    #define TERRAIN_BLEND_ACTIVE 1
#else
    #define TERRAIN_BLEND_ACTIVE 0
#endif


struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float4 blend_0 : TEXCOORD1;
    float4 blend_1 : TEXCOORD2;
};

struct VS_OUTPUT {
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
    float3 vertex_color : TEXCOORD1;
    float3 lPosition : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 binormal : TEXCOORD4;
    float3 normal : TEXCOORD5;
    float4 projectionPosition : TEXCOORD6;
    float3 viewPosition : TEXCOORD7;
};

#ifdef VS

row_major float4x4 ModelViewProj : register(c0);

float4 FogParam : register(c14);
float3 FogColor : register(c15);
float4 EyePosition : register(c16);

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 posPS = mul(float4x4(ModelViewProj[0].xyzw, ModelViewProj[1].xyzw, ModelViewProj[2].xyzw, ModelViewProj[3].xyzw), IN.position.xyzw);

    OUT.blend_0 = IN.blend_0;
    OUT.blend_1 = IN.blend_1;
    
    OUT.sPosition.xyzw = posPS;
    OUT.uv.xy = IN.uv.xy;
    OUT.vertex_color.xyz = clamp(IN.vertex_color.rgb, 0.0f, 1.0f);
    OUT.lPosition.xyz = IN.position.xyz;
    OUT.tangent.xyz = IN.tangent.xyz;
    OUT.binormal.xyz = IN.binormal.xyz;
    OUT.normal.xyz = IN.normal.xyz;
    OUT.projectionPosition.xyzw = posPS;
    OUT.viewPosition.xyz = EyePosition.xyz;

    return OUT;
};

#endif  // Vertex shader.

struct PS_INPUT
{
    float2 uv : TEXCOORD0;
    float3 vertex_color : TEXCOORD1_centroid;
    float3 lPosition : TEXCOORD2_centroid;
    float3 tangent : TEXCOORD3_centroid;
    float3 binormal : TEXCOORD4_centroid;
    float3 normal : TEXCOORD5_centroid;
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 projectionPosition : TEXCOORD6_centroid;
    float3 viewPosition : TEXCOORD7_centroid;
    float4 sPosition : POSITION1;
#if TERRAIN_BLEND_ACTIVE
    float2 screenPos : VPOS;
#endif
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#ifdef PS

sampler2D BaseMap[7]:register(s0);
sampler2D NormalMap[7]:register(s7);

float4 AmbientColor : register(c1);
float4 SunColor : register(c3);
float4 SunDir : register(c18);

float4 LandSpec[2] : register(c32);
float4 LandHeight[2] : register(c34);
float4 FogParam : register(c36);
float4 FogColor : register(c37);

#ifdef NUM_PT_LIGHTS
float4 PointLightColor[NUM_PT_LIGHTS] : register(c39);
float4 PointLightPosition[NUM_PT_LIGHTS] : register(c63);
float PointLightCount : register(c88);
#endif

#if TERRAIN_BLEND_ACTIVE
// The world's depth as the first land draw of the scene started (TerrainShaders::PrepareBlending) holds the objects
// drawn before the land: rocks, walls, props. Where the land covers one of them by less than the blend distance, the
// land fades out over that depth, so the object's buried part shows through and it sinks into the ground instead of
// ending in a hard line. Smooth (default): the land is drawn see-through there (its alpha; Terrain.cpp turns blending on
// for its opaque pass) over the object, which the game drew before the land - a smooth fade without TAA. Off: a dither
// (clip), which averages into a fade only with TAA (it moves the pattern every frame), a fine still stipple without.
// Registers pinned clear of the game's (land PS tops out at c93) and of every other NVR constant.
float4 TESR_TerrainBlendData : register(c176);  // x: on for this draw, y: 1 / blend distance, z: max distance, w: debug view
float4 TESR_TerrainBlendExtra : register(c177); // xy: 1 / resolution, z: dither frame (0 without TAA), w: 1 Smooth (blended, not dithered)
// One line (ShaderTextureValue::GetSamplerStateString).
sampler2D TESR_TerrainBlendDepth : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

// View depth of a depth-buffer value through the camera's inverse projection: only its z and w columns depend on the
// depth (the jitter and the screen position live in x and y). Used for the land and the scene alike, so both match.
float terrainBlendViewDepth(float depth) {
    float2 zw = depth * float2(TESR_InvProjectionTransform[2][2], TESR_InvProjectionTransform[2][3]) +
        float2(TESR_InvProjectionTransform[3][2], TESR_InvProjectionTransform[3][3]);
    return zw.x / zw.y;
}

// The land's view depth here (x) and how far behind it the object drawn before it is (y; 0 or less: in front, or none).
float2 terrainBlendDepths(float4 projectionPosition, float2 screenPos) {
    float terrainDepth = terrainBlendViewDepth(projectionPosition.z / projectionPosition.w);
    float2 uv = (screenPos + 0.5f) * TESR_TerrainBlendExtra.xy;
    float sceneDepth = terrainBlendViewDepth(tex2Dlod(TESR_TerrainBlendDepth, float4(uv, 0.0f, 0.0f)).x);
    return float2(terrainDepth, sceneDepth - terrainDepth);
}

// How much of the land to keep at this pixel: 1 opaque, 0 gone; -1 where blending does not apply. coveredWidth: how much
// `covered` changes from one pixel to the next.
float getTerrainBlend(float terrainDepth, float covered, float coveredWidth) {
    float maxDistance = TESR_TerrainBlendData.z;
    if (terrainDepth > maxDistance) return -1.0f;
    if (covered <= 0.0f) return -1.0f;          // the object is in front: the depth test handles it

    // UNOFFICIAL 2026-10-14 (user: a faint shimmer along the seams with Smooth): a few metres away the band (BlendDistance
    // deep) was only a pixel or two wide on screen and crawled as the view moved. It is now at least about three pixels
    // wide, at most four times BlendDistance (so a hill in front of a distant rock never turns see-through at its edge).
    float range = clamp(coveredWidth * 3.0f, 1.0f / TESR_TerrainBlendData.y, 4.0f / TESR_TerrainBlendData.y);
    float keep = saturate(covered / range);
    // Back to a hard edge over the last quarter before the max distance, so there is no visible line.
    return lerp(keep, 1.0f, saturate((terrainDepth / maxDistance - 0.75f) * 4.0f));
}

// Interleaved gradient noise (Jimenez); the frame offset moves it only while TAA runs.
float terrainBlendNoise(float2 screenPos) {
    float2 p = screenPos + 5.588238f * TESR_TerrainBlendExtra.z;
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}
#endif


PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

#if TERRAIN_BLEND_ACTIVE
    // First, so a dithered-away pixel skips the rest where the GPU can (whole quads).
    float terrainKeep = -1.0f;
    // (Read outside the branch: the screen derivative needs every pixel of the 2 x 2 quad.)
    float2 blendDepths = terrainBlendDepths(IN.projectionPosition, IN.screenPos);
    float coveredWidth = fwidth(blendDepths.y);
    [branch] if (TESR_TerrainBlendData.x > 0.0f) {
        terrainKeep = getTerrainBlend(blendDepths.x, blendDepths.y, coveredWidth);
        // UNOFFICIAL 2026-10-13 (user: blend instead, so it needs no TAA): Smooth draws the land see-through there instead
        // (its alpha below; Terrain.cpp turns blending on for the land's opaque pass): the object, drawn before the land,
        // shows through it in a smooth fade. A pixel with next to nothing of the land left is skipped.
        if (TESR_TerrainBlendData.w <= 0.0f && terrainKeep >= 0.0f && terrainKeep < 1.0f)
            clip(TESR_TerrainBlendExtra.w > 0.5f ? terrainKeep - 0.004f : terrainKeep - terrainBlendNoise(IN.screenPos));
    }
#endif
    
    int texCount = TEX_COUNT;  // Macro.
    float3 tangent = normalize(IN.tangent.xyz);
    float3 binormal = normalize(IN.binormal.xyz);
    float3 normal = normalize(IN.normal.xyz);
    float3x3 tbn = float3x3(tangent, binormal, normal);
    float3 eyeDir = normalize(mul(tbn, IN.viewPosition.xyz - IN.lPosition.xyz));
    float dist = length(IN.viewPosition.xyz - IN.lPosition.xyz);

    float2 dx, dy;
    dx = ddx(IN.uv.xy);
    dy = ddy(IN.uv.xy);
    
    float weights[7] = { 0, 0, 0, 0, 0, 0, 0 };
    float blends[7] = { IN.blend_0.x, IN.blend_0.y, IN.blend_0.z, IN.blend_0.w, IN.blend_1.x, IN.blend_1.y, IN.blend_1.z };
    float spec[7] = { LandSpec[0].x, LandSpec[0].y, LandSpec[0].z, LandSpec[0].w, LandSpec[1].x, LandSpec[1].y, LandSpec[1].z };
    float heightStatus[7] = { LandHeight[0].x, LandHeight[0].y, LandHeight[0].z, LandHeight[0].w, LandHeight[1].x, LandHeight[1].y, LandHeight[1].z };

    // Camera-relative world position, for the forward sun shadows below and for telling ground under the water.
    // No extra interpolator needed: projectionPosition is already the clip-space position,
    // and clip position is affine in object space, so it interpolates exactly.
    // ddx/ddy must stay at top level, outside dynamic flow control.
    float3 shadowWorldPos = GetShadowWorldPos(IN.projectionPosition);
    float3 shadowNormal = GetShadowGeometricNormal(shadowWorldPos);

    // [Main.Main.ReducedQuality] CheapUnderwaterTerrain: ground below TESR_TerrainParallaxExtraData.w (camera-relative
    // height of the water line; -FLT_MAX when off) skips parallax and its shadows, exactly as with parallax switched off.
    // It is seen through moving, refracting water. The parallax functions take explicit gradients, so they may branch.
    bool parallax = shadowWorldPos.z >= TESR_TerrainParallaxExtraData.w;
    float2 offsetUV = IN.uv.xy;
    [branch] if (parallax) offsetUV = getParallaxCoords(dist, IN.uv.xy, dx, dy, eyeDir, texCount, BaseMap, blends, heightStatus, weights);
    else weights = blends;

    float gloss = 0.0f;
    float specExponent = 0.0f;
    float3 baseColor = LinColor(blendDiffuseMaps(IN.vertex_color, offsetUV, texCount, BaseMap, weights));	// UNOFFICIAL linear lighting: decoded
    float3 combinedNormal = blendNormalMaps(offsetUV, texCount, NormalMap, weights, spec, gloss, specExponent);

    float3 lightTS = mul(tbn, SunDir.xyz);
    float parallaxShadowMultiplier = 1.0;
    [branch] if (parallax) parallaxShadowMultiplier = getParallaxShadowMultipler(dist, offsetUV, dx, dy, lightTS, texCount, blends, heightStatus, BaseMap);

    // Forward sun shadows. Folded into parallaxShadowMultiplier, which getSunLighting
    // applies to the sun colour only -- ambient is added afterwards and stays untouched.
    #if FORWARD_SHADOWS
    parallaxShadowMultiplier *= GetSunShadow(shadowWorldPos, shadowNormal);
    #endif

    float3 lighting = getSunLighting(lightTS, LinColor(SunColor.rgb), eyeDir, combinedNormal, LinColor(AmbientColor.rgb), baseColor, gloss, specExponent, 1.0, parallaxShadowMultiplier, shadowNormal);

    #if defined(NUM_PT_LIGHTS)
        [loop] for (int i = 0; i < PointLightCount; i++) {
            float3 pointlightDir = PointLightPosition[i].xyz - IN.lPosition.xyz;
            float att = vanillaAtt(pointlightDir, PointLightPosition[i].w);
        
            [branch]
            if (att > 0.001) {
                pointlightDir = mul(tbn, pointlightDir);
                lighting += getPointLightLighting(pointlightDir, att, LinColor(PointLightColor[i].rgb), eyeDir, combinedNormal, baseColor, gloss, specExponent);
            }
        }
    #endif
    
    // Per pixel fog.
    float4 fog;
    float3 fogPos = IN.projectionPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = IN.projectionPosition.w - IN.projectionPosition.z;
    #endif
    float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
    fog.rgb = FogColor.rgb;
    fog.a = pow(fogStrength, FogParam.z);
    
    float3 finalColor = LinEncode(lighting);	// UNOFFICIAL linear lighting: back to the stored (gamma) space, before the fog
    finalColor = lerp(finalColor, fog.rgb, fog.a); // Apply fog.

#if TERRAIN_BLEND_ACTIVE
    // Debug: the blend band in magenta (full magenta = the land fully gone there), nothing dithered away.
    if (TESR_TerrainBlendData.w > 0.0f && terrainKeep >= 0.0f)
        finalColor = lerp(float3(1.0f, 0.0f, 1.0f), finalColor, terrainKeep);
#endif

    OUT.color_0.a = 1;
#if TERRAIN_BLEND_ACTIVE
    if (TESR_TerrainBlendExtra.w > 0.5f && TESR_TerrainBlendData.w <= 0.0f && terrainKeep >= 0.0f) OUT.color_0.a = terrainKeep;
#endif
    OUT.color_0.rgb = finalColor;

    return OUT;
};

#endif  // Pixel shader.
