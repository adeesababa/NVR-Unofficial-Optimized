// UNOFFICIAL 2026-10-15: Conor's skin shader ([Shaders.Skin.NewSkin] Enabled, read at startup). CREDIT: Conor (mrfury97), from
// his NVR testbed (github.com/mrfury97/Conor-sClaudeHell-NVR-SAL-Fork, branch testbed: SkinTemplate.hlsl, commits a9ca336a,
// 2aae8ebd, a43537e9). Every vanilla SKIN20xx vertex and pixel shader is built from this one file (SkinShaders::Templates()
// in src/effects/Skin.h); with the setting off this file is not used and the skin shaders are this build's SKIN20xx files.
// Lighting is in Includes/SkinLighting.hlsl. Ported to this build:
//   - lamp shadows are this build's forward interior shadows (Includes/PointShadowForward.hlsl, INTERIOR_SHADOWS) instead
//     of his packed PointShadow.hlsl; lamps keep vanilla's 1 - |d|^2 falloff (his inverse square and merged lamp passes are
//     not in this build)
//   - rain on the first-person arms is this build's HudRain.hlsl (HudDrops) instead of his HudRaindrops.hlsl
//   - linear lighting is this build's ([Shaders.PBR.Linear]: LinColor / LinEncode)
//   - 2026-10-19: optional details after Community Shaders' Advanced Skin (GPL-3.0), off by default (SkinLighting.hlsl)
//
// His notes follow. Both halves are replaced, so the interface between them is NVR's own; only the engine's
// constants and the vertex declarations have to match vanilla's. Those were read off the vanilla
// shaders (shaderpackage010.sdp):
//
//   VS constants: ModelViewProj c0 / SkinModelViewProj c1, FogParam c14, FogColor c15,
//   ShadowProj c18-c23, LightData c25+ (OBJECT space: [0] the sun direction, then point light
//   position + radius; in the DIFFUSE family every entry is a point light), Bones c44.
//   PS constants: AmbientColor c1, EmittanceColor c2 (.a = light count, many-light family),
//   PSLightColor c3+, Toggles c27 (not in the AD and DIFFUSE families).
//
// Variants (defines):
//   SKIN         bone-skinned geometry
//   POINTS=n     point lights after the sun (DIFFUSE: point lights in all)
//   PROJ_SHADOW  vanilla's projected actor shadow (never with more than 2 point lights)
//   ONLY_LIGHT   AD pass: writes light only (ambient included), the engine multiplies by the
//                texture afterwards. No fog, vertex colour or Toggles.
//   DIFFUSE      point-light-only additive pass. No ambient, base texture, fog or Toggles.
//   MANY         the many-light family: EmittanceColor.a says how many lights are live.
//
// Direct lighting is in TANGENT space, as vanilla's is: the vertex shader takes the light and
// view vectors into the vertex's tangent frame, so nothing depends on the projection. That
// matters because the engine draws first-person arms with their own camera (FOV and near
// plane), while NVR's inverse projection constants are the world camera's: world vectors
// rebuilt from clip space are skewed there. They are only used for the sky terms, the sun
// shadow and the curvature, where a skew costs little.
//
// VS -> PS:
//   TEXCOORD0  uv.xy, sun.xy (tangent)                  TEXCOORD4  world position, view.z
//   TEXCOORD1  world tangent, sun.z                     TEXCOORD5  point light 1, light 4.x
//   TEXCOORD2  world binormal, view.x (tangent)         TEXCOORD6  point light 2, light 4.y
//   TEXCOORD3  world normal, view.y                     TEXCOORD7  point light 3, light 4.z --
//                                                                  or the PROJ_SHADOW uvs
//   COLOR0/1   vertex colour, fog (base and MANY families only)
// All ten of ps_3_0's input registers in the largest variants. A point light's tangent-space
// light vector divided by its radius gives both the direction and the attenuation,
// 1 - |v|^2, vanilla's own falloff.

#if defined(__INTELLISENSE__)
    #define PS
    #define POINTS 2
#endif

#ifndef POINTS
    #define POINTS 0
#endif

#if defined(DIFFUSE)
    #define ONLY_LIGHT
#endif

#if defined(ONLY_LIGHT)
    #define OPT
#else
    #define FOG_COLOR
#endif

// PROJ_SHADOW shares TEXCOORD7 with the third point light; vanilla never combines them.

#include "includes/Helpers.hlsl"
// The camera matrices Shadow.hlsl rebuilds world positions with are moved out of c100-c107 in vertex
// shaders: the game's bone upload goes past Bones[54] (c97) and overwrote them in skinned ones (see Shadow.hlsl).
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#define SHADOW_SKIN_SOFTNESS 1   // UNOFFICIAL 2026-10-20 CharacterShadowSoftness (Includes/Shadow.hlsl ShadowMinPenumbra)
#include "includes/Shadow.hlsl"

#if defined(VS)

#include "includes/SkinHelpers.hlsl"

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
#ifdef FOG_COLOR
    float4 color : COLOR0;
#endif
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

struct VS_OUTPUT {
    float4 position : POSITION;
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float4 binormal : TEXCOORD2;
    float4 normal : TEXCOORD3;
    float4 shadowWorldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7;
#elif defined(PROJ_SHADOW)
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef FOG_COLOR
    float4 color : COLOR0;
    float4 fog : COLOR1;
#endif
};

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
    #define MVP ModelViewProj
#else
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 Bones[54] : register(c44);
    #define MVP SkinModelViewProj
#endif

#ifdef FOG_COLOR
    float4 FogParam : register(c14);
    float3 FogColor : register(c15);
#endif

#ifdef PROJ_SHADOW
    row_major float4x4 ShadowProj : register(c18);
    float4 ShadowProjData : register(c22);
    float4 ShadowProjTransform : register(c23);
#endif

float4 EyePosition : register(c16);
float4 LightData[10] : register(c25);

// An object-space direction to (scaled) world space, through the same transform as the position.
// The w after the inverse projection is dropped: it is 0 for anything drawn with the world
// camera, and for first-person geometry (a different near plane) it would add the camera's
// position into a direction.
float3 ToWorld(float3 v) {
    float4 viewDir = mul(mul(MVP, float4(v, 0.0f)), TESR_InvProjectionTransform);
    viewDir.w = 0.0f;
    return mul(viewDir, TESR_InvViewTransform).xyz;
}

#ifdef DIFFUSE
    #define FIRST_POINT 0
#else
    #define FIRST_POINT 1
#endif

// Tangent-space light vector / radius (both in object units).
float3 PointLightVector(int i, float3 position, float3x3 tbn) {
    float4 light = LightData[FIRST_POINT + i];
    return mul(tbn, (light.xyz - position) / light.w);
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 position = float4(IN.position.xyz, 1.0f);
    #ifndef SKIN
        float3x3 tbn = float3x3(normalize(IN.tangent), normalize(IN.binormal), normalize(IN.normal));
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
    #endif

    OUT.position = mul(MVP, position);

    // World frame for the sky terms; tangent-space vectors for direct lighting (see the header).
    float3 viewTS = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.tangent = float4(normalize(ToWorld(tbn[0])), 0.0f);
    OUT.binormal = float4(normalize(ToWorld(tbn[1])), viewTS.x);
    OUT.normal = float4(normalize(ToWorld(tbn[2])), viewTS.y);
    OUT.shadowWorldPos = float4(GetShadowWorldPos(OUT.position), viewTS.z);

    #ifndef DIFFUSE
        float3 sunTS = normalize(mul(tbn, LightData[0].xyz));
    #else
        float3 sunTS = 0.0f;
    #endif
    OUT.uv = float4(IN.uv.xy, sunTS.xy);
    OUT.tangent.w = sunTS.z;

    float3 light4 = 0.0f;
    #if POINTS > 3
        light4 = PointLightVector(3, position.xyz, tbn);
    #endif
    #if POINTS > 0
        OUT.light1 = float4(PointLightVector(0, position.xyz, tbn), light4.x);
    #endif
    #if POINTS > 1
        OUT.light2 = float4(PointLightVector(1, position.xyz, tbn), light4.y);
    #endif
    #if POINTS > 2
        OUT.light3 = float4(PointLightVector(2, position.xyz, tbn), light4.z);
    #elif defined(PROJ_SHADOW)
        float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    #ifdef FOG_COLOR
        // Vanilla's fog, from the clip-space xyz distance.
        float fogStrength = saturate((FogParam.x - length(OUT.position.xyz)) / FogParam.y);
        OUT.fog = float4(FogColor, pow(1.0f - fogStrength, FogParam.z));
        OUT.color = IN.color;
    #endif

    return OUT;
}

#endif // VS

#if defined(PS)

#include "includes/SkinLighting.hlsl"
// This build's forward lamp shadows (interiors): light k of the draw is PSLightColor[k] (the skin passes all go through
// SetupGeometryConstants_Lights), looked up once at the top into ptShadow<k>. POINT_SHADOW_SCALAR: plain numbers, the
// debug views' tint applied where they are used.
#define POINT_SHADOW_SCALAR
#include "includes/PointShadowForward.hlsl"
// Rain on the first-person arms (Wet World HudDrops), compiled in only when HudDrops is on at startup.
#if defined(HUD_RAIN_ON) && HUD_RAIN_ON
    #define HUD_RAIN 1
#endif
#include "includes/HudRain.hlsl"

// The draw's light index of point light 0-3 (literal numbers: PointShadowForward.hlsl pastes them into names).
#ifdef DIFFUSE
    #define FIRST_POINT 0
    #define SKIN_LAMP0 0
    #define SKIN_LAMP1 1
    #define SKIN_LAMP2 2
    #define SKIN_LAMP3 3
#else
    #define FIRST_POINT 1
    #define SKIN_LAMP0 1
    #define SKIN_LAMP1 2
    #define SKIN_LAMP2 3
    #define SKIN_LAMP3 4
#endif
// Two levels, so SKIN_LAMPn becomes its number before it is pasted: ptShadow<k> = lamp k's shadow, d its light vector / radius.
#define SKIN_LAMP_SET(k, d) SKIN_LAMP_SET_(k, d)
#define SKIN_LAMP_SET_(k, d) ptShadow##k = POINT_SHADOW_LOOKUP(k, saturate(1.0f - dot(d, d)))
#define SKIN_LAMP_SHADOW(k) POINT_SHADOW(k, 1.0f)

struct PS_INPUT {
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1;
    float4 binormal : TEXCOORD2;
    float4 normal : TEXCOORD3;
    float4 shadowWorldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7;
#elif defined(PROJ_SHADOW)
    float4 shadowUVs : TEXCOORD7;
#endif
#ifdef FOG_COLOR
    float4 color : COLOR0;
    float4 fog : COLOR1;
#endif
};

// Vanilla's sampler slots per family.
#if defined(DIFFUSE)
    sampler2D NormalMap : register(s0);
#elif defined(ONLY_LIGHT)
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
    sampler2D FaceGenMap0 : register(s2);   // bound by the SkinShader hook on faces (SkinScreenSpaceScatter.y)
    sampler2D FaceGenMap1 : register(s3);
    #ifdef PROJ_SHADOW
        sampler2D ShadowMap : register(s5);
        sampler2D ShadowMaskMap : register(s6);
    #endif
#else
    sampler2D BaseMap : register(s0);
    sampler2D NormalMap : register(s1);
    sampler2D FaceGenMap0 : register(s2);
    sampler2D FaceGenMap1 : register(s3);
    #ifdef PROJ_SHADOW
        sampler2D ShadowMap : register(s6);
        sampler2D ShadowMaskMap : register(s7);
    #endif
#endif

float4 AmbientColor : register(c1);
float4 EmittanceColor : register(c2);
float4 PSLightColor[10] : register(c3);
#ifndef OPT
    float4 Toggles : register(c27);   // x vertex colour, y fog, z shine (unused), w alpha test ref
#endif

#if FORWARD_SHADOWS
// Port: whether a point SKIN_SELF_SHADOW_REACH toward the sun is lit, for SkinLight's selfShadowEscape. One plain tap of the
// Near cascade, or the Middle one past it (no cascade cross-fade, contact hardening or kept-atlas fade): a yes/no in free air,
// called only where GetSunShadow already passed its own checks (the sun is up and the maps are live). Inside a branch:
// SampleShadowAtlas reads with tex2Dlod.
float SkinSunLitBeyond(float3 position) {
#if SHADOW_FIXED_MODE == 0
    const float bias = 0.00001f;
#elif SHADOW_FIXED_MODE == 3
    const float bias = 0.0002f;
#else
    const float bias = 0.01f;
#endif
    const bool near = length(position - TESR_ShadowNearCenter.xyz) < TESR_ShadowNearCenter.w;
    float4x4 lightTransform = near ? TESR_ShadowCameraToLightTransformNear : TESR_ShadowCameraToLightTransformMiddle;
    return saturate(SampleShadowAtlas(mul(float4(position, 1.0f), lightTransform), near ? float2(0.0f, 0.0f) : float2(0.5f, 0.0f), bias, near ? 0.1f : 0.2f));
}
#endif

// Point light i: d is its light vector / radius; shadow its lamp shadow (SKIN_LAMP_SHADOW, 1 without one), applied to its
// diffuse and highlight with vanilla's 1 - |d|^2 falloff.
float3 PointLight(int i, float3 d, float3 shadow, float3 albedo, float3 N, float3 Nsoft, float3 geometricNormal, float3 V, out float3 specular) {
    // Port (2026-10-15): the falloff multiplies the light AFTER SkinLight decodes its colour (with shadow below), as this build's
    // other skin, object and terrain shaders apply it with [Shaders.PBR.Linear] on. Conor's version took the falloff into the
    // colour before the decode, so with linear lighting on a lamp faded as falloff^2.2 instead of falloff: lamp-lit skin came
    // out up to about half as bright as the old skin shader's (a lamp faded to 30 % lit it at 30 % instead of 58 %). With
    // linear lighting off the two are the same.
    float3 lightColor = PSLightColor[FIRST_POINT + i].rgb;
    shadow *= LampAttOther(saturate(1.0f - dot(d, d)));	// UNOFFICIAL 2026-10-21 LampFalloff (Includes/LampFalloff.hlsl): as before, the objects' decode or the shape
    #ifdef MANY
        // Unused slots (EmittanceColor.a lights are live) can hold anything, NaN included, so
        // they are branched around, not multiplied by 0. The branch also keeps the compiler
        // from interleaving all four lights, which overflows ps_3_0's 32 temporaries.
        float3 diffuse = 0.0f;
        specular = 0.0f;
        [branch] if ((FIRST_POINT + i) < EmittanceColor.a && any(shadow > 0.0f))
            diffuse = SkinLight(albedo, N, Nsoft, geometricNormal, V, d, lightColor, 1.0f, 1.0f, false, specular);
        specular *= shadow;
        return diffuse * shadow;
    #else
        float3 diffuse = SkinLight(albedo, N, Nsoft, geometricNormal, V, d, lightColor, 1.0f, 1.0f, false, specular);
        specular *= shadow;
        return diffuse * shadow;
    #endif
}

// [Shaders.Skin.Debug] DebugView 1-7, replacing the colour this pass writes:
//   1 which pass drew the skin: green one full pass (the sun and up to one point light), cyan one
//     full pass with up to four point lights, yellow a light-only pass the engine multiplies by
//     the texture afterwards, red added for every extra point-light pass on top. Indoors, skin
//     usually goes the yellow and red route.
//   2 which settings are in use: blue Interiors, orange outdoors; bright where
//     the screen-space blur takes this skin, dark where per-pixel diffusion stands in
//   3 direct light (sun and lamps, with the scattering and translucency) on white skin
//   4 ambient and sky light on white skin
//   5 highlights alone: the lights' and the sky reflection
//   6 material: red roughness, green highlight strength (specular mask x SpecularStrength), blue
//     the VanillaMatchedHighlights boost (full at its 12x cap)
//   7 curvature: red the per-pixel diffusion (PerPixelWidth; 0 where the blur takes over), green
//     thin features, where Translucency can show
// The light-only passes are divided by the texture the engine multiplies them by afterwards, so
// every view reads true. The extra point-light passes add to the pass under them: in 3 and 5 they
// add their own light (the engine's texture pass tints it), elsewhere nothing, or red in 1.
float3 SkinDebugView(float view, float3 direct, float3 ambient, float3 highlights, float3 albedo, float3 adAlbedo) {
    #if defined(DIFFUSE)
        if (view < 1.5f) return float3(0.5f, 0.0f, 0.0f);
        if (view > 2.5f && view < 3.5f) return LinEncode(direct);
        if (view > 4.5f && view < 5.5f) return LinEncode(highlights);
        return 0.0f;
    #else
        float3 onWhite = 1.0f / max(albedo, 0.02f);   // light as it would fall on white skin
        float3 c;
        if (view < 1.5f) {
            #if defined(ONLY_LIGHT)
                c = float3(1.0f, 1.0f, 0.0f);
            #elif defined(MANY)
                c = float3(0.0f, 1.0f, 1.0f);
            #else
                c = float3(0.0f, 1.0f, 0.0f);
            #endif
        }
        else if (view < 2.5f) {
            c = TESR_SkinDebugData.y > 0.0f ? float3(0.2f, 0.45f, 1.0f) : float3(1.0f, 0.55f, 0.1f);
            c *= SkinScreenSpaceScatter.x > 0.0f ? 1.0f : 0.3f;
        }
        else if (view < 3.5f) c = LinEncode(direct * onWhite);
        else if (view < 4.5f) c = LinEncode(ambient * onWhite);
        else if (view < 5.5f) c = LinEncode(highlights);
        else if (view < 6.5f) c = float3(skinRoughness, saturate(skinSpecScale * 0.5f), saturate((skinVanillaMatch - 1.0f) / 11.0f));
        else c = float3(skinCurvature, skinThinness, 0.0f);
        #if defined(ONLY_LIGHT)
            c /= max(adAlbedo, 0.05f);
        #endif
        return c;
    #endif
}

// COLOR1 goes to the skin scattering effect's target, bound only during NVR's skin draws in the
// main scene (src/effects/SkinScattering.h); anywhere else it is discarded.
//   rgb  the highlights' share of COLOR0 (gamma, fogged), which the effect takes out before it
//        blurs the skin and puts back after
//   a    the camera distance, which the effect checks against the depth buffer
// COLOR2 goes to its albedo target:
//   rgb  the colour the light is multiplied by, divided out before the blur so only light diffuses
//        and the texture stays sharp
//   a    the luminance of the colour this draw leaves in the scene: anything drawn over the skin
//        afterwards changes it, which is how the effect tells covered skin (hair cards, decals)
//        from visible skin even where the covering writes no depth
struct PS_OUTPUT {
    float4 color : COLOR0;
    float4 scatter : COLOR1;
    float4 albedo : COLOR2;
};

PS_OUTPUT main(PS_INPUT IN) {
    float3 worldPos = IN.shadowWorldPos.xyz;

    // Rain on the first-person arms (Includes/HudRain.hlsl): the textures are read under the drops.
    HudRain(IN.uv.xy, worldPos, true);
    float2 uv = HudRainUV(IN.uv.xy);

    // --- lamp shadows first, while little else is held (this build's PointShadowForward.hlsl; nothing without INTERIOR_SHADOWS)
    #if INTERIOR_SHADOWS && POINTS > 0
        float3 ptWorldPos = worldPos;
        float3 ptNormal = normalize(IN.normal.xyz);   // the interpolated world normal: only the offset's side matters
        float ptValid = 1.0f;                          // this template always sends the world position
        float ptShadow0 = 1.0f, ptShadow1 = 1.0f, ptShadow2 = 1.0f, ptShadow3 = 1.0f, ptShadow4 = 1.0f;
        [branch] if (NVR_PointShadowParams.x > 0.0f) {
            #if POINTS > 0
                SKIN_LAMP_SET(SKIN_LAMP0, IN.light1.xyz);
            #endif
            #if POINTS > 1
                SKIN_LAMP_SET(SKIN_LAMP1, IN.light2.xyz);
            #endif
            #if POINTS > 2
                SKIN_LAMP_SET(SKIN_LAMP2, IN.light3.xyz);
            #endif
            #if POINTS > 3
                SKIN_LAMP_SET(SKIN_LAMP3, float3(IN.light1.w, IN.light2.w, IN.light3.w));
            #endif
        }
    #endif

    // --- material
    #if defined(DIFFUSE)
        float4 baseColor = 1.0f;
        float3 adAlbedo = 1.0f;   // no base texture bound to divide by
    #elif defined(ONLY_LIGHT)
        float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        // What the engine multiplies this pass by afterwards: the base texture, or on faces the
        // FaceGen blend its texture pass (SLS1005) applies, the same as the full pass's. The FaceGen
        // maps are read on faces only (bodies and hands have none bound), with the derivatives taken
        // out here: none are allowed inside the branch.
        float3 adAlbedo = baseColor.rgb;
        float2 uvDx = ddx(IN.uv.xy);
        float2 uvDy = ddy(IN.uv.xy);
        [branch] if (SkinScreenSpaceScatter.y > 0.0f)
            adAlbedo = 2.0f * ((expand(tex2Dgrad(FaceGenMap0, uv, uvDx, uvDy).rgb) + baseColor.rgb) * (2.0f * tex2Dgrad(FaceGenMap1, uv, uvDx, uvDy).rgb));
        baseColor.rgb = 1.0f;
    #else
        float4 baseTexture = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        clip(AmbientColor.a >= 1 ? 0 : (baseTexture.a - Toggles.w));

        float3 faceGenVar = HUD_RAIN_TEX(FaceGenMap0, IN.uv.xy).rgb;
        float3 faceGenBlend = HUD_RAIN_TEX(FaceGenMap1, IN.uv.xy).rgb;
        float4 baseColor = float4(2.0f * ((expand(faceGenVar) + baseTexture.rgb) * (2.0f * faceGenBlend)), baseTexture.a);
        baseColor.rgb = Toggles.x <= 0.0f ? baseColor.rgb : baseColor.rgb * IN.color.rgb;
        float3 adAlbedo = 1.0f;
    #endif

    // --- normals: tangent space for direct light, world for the sky
    float4 normalTexel = HUD_RAIN_TEX(NormalMap, IN.uv.xy);
    float3 N = HudRainNormal(normalize(expand(normalTexel.xyz)));
    float3 Nsoft = normalize(expand(tex2Dbias(NormalMap, float4(uv, 0.0f, 2.0f)).xyz));
    const float3 geometricNormal = float3(0.0f, 0.0f, 1.0f);
    float3 V = normalize(float3(IN.binormal.w, IN.normal.w, IN.shadowWorldPos.w));

    setupSkin(N, IN.normal.xyz, worldPos, normalTexel.a, adAlbedo);
    // Wet: water is smooth and shines at least as a full specular mask would (Includes/HudRain.hlsl).
    skinRoughness = HudRainRoughness(skinRoughness);
    #if HUD_RAIN
        skinSpecScale = lerp(skinSpecScale, max(skinSpecScale, 1.0f), hudRainGloss);
    #endif
    // 2026-10-19 Community Shaders' skin details ([Shaders.Skin.NewSkin] Fuzz, EnergyBalance, CreaseDamping, TransmitProfile;
    // Includes/SkinLighting.hlsl): top level, it takes derivatives of the bumped world normal.
    setupSkinCS(N, V, normalize(N.x * IN.tangent.xyz + N.y * IN.binormal.xyz + N.z * IN.normal.xyz));
    float3 albedo = LinColor(baseColor.rgb);

    // --- shadows (top level: both take derivatives)
    #if !defined(DIFFUSE)
        #if FORWARD_SHADOWS
            float3 shadowNormal = GetShadowGeometricNormal(worldPos);
            shadowNormal = ShadowLookupNormal(shadowNormal, float4(IN.normal.xyz, SHADOW_VS_SENTINEL));   // UNOFFICIAL 2026-10-20 SmoothCharacterShadows (Shadow.hlsl): the interpolated world normal
            // The same SKIN_SELF_SHADOW_REACH toward the sun: lit there, a shadow here is the skin's own.
            // SkinLight uses it only at the terminator (geometric N.L under 0.05), in shadow, with
            // ShadowScatter on: anywhere else it changes nothing, and the second lookup is skipped.
            // Port: that second look is one plain tap (SkinSunLitBeyond), not a second GetSunShadow: two copies of the full
            // lookup took this shader from about 800 to 2200 instruction slots, and to 4700 with shadow Mode 2 and the
            // cross-fade, past what some drivers accept. It only has to tell lit from shadowed, in free air past the skin.
            float sunShadow = GetSunShadow(worldPos, shadowNormal);
            float sunShadowBeyond = 1.0f;
            [branch] if (sunShadow < 1.0f && normalize(float3(IN.uv.zw, IN.tangent.w)).z < 0.05f && TESR_SkinLightExtra.z > 0.0f)
                sunShadowBeyond = SkinSunLitBeyond(worldPos + TESR_SmoothedSunDir.xyz * SKIN_SELF_SHADOW_REACH);
        #else
            float sunShadow = 1.0f;
            float sunShadowBeyond = 1.0f;
        #endif

        // Vanilla's projected actor shadow, a gamma factor on the sun's colour.
        float3 projShadow = 1.0f;
        #ifdef PROJ_SHADOW
            float3 shadowTexel = tex2D(ShadowMap, IN.shadowUVs.xy).rgb;
            projShadow = lerp(1.0f, shadowTexel, tex2D(ShadowMaskMap, IN.shadowUVs.zw).x);
        #endif
    #endif

    // --- lights: diffuse and highlights kept apart (see PS_OUTPUT)
    float3 diffuseLight = 0.0f;
    float3 specularLight = 0.0f;
    float3 ambientLight = 0.0f;   // kept apart for the debug views
    float3 specular;

    #if !defined(DIFFUSE)
        float3 sunDir = float3(IN.uv.zw, IN.tangent.w);
        diffuseLight += SkinLight(albedo, N, Nsoft, geometricNormal, V, sunDir, PSLightColor[0].rgb * projShadow, sunShadow, sunShadowBeyond, true, specular);
        specularLight += specular;
    #endif

    #if POINTS > 0
        diffuseLight += PointLight(0, IN.light1.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP0), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 1
        diffuseLight += PointLight(1, IN.light2.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP1), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 2
        diffuseLight += PointLight(2, IN.light3.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP2), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 3
        diffuseLight += PointLight(3, float3(IN.light1.w, IN.light2.w, IN.light3.w), SKIN_LAMP_SHADOW(SKIN_LAMP3), albedo, N, Nsoft, geometricNormal, V, specular);
        specularLight += specular;
    #endif

    // --- sky and ambient (reflection first: it sets the share of the ambient it takes)
    #if !defined(DIFFUSE)
        // Built here, not at the top: nine values held through every light overflow ps_3_0's
        // 32 temporaries in the many-light variants.
        float3x3 worldFrame = float3x3(normalize(IN.tangent.xyz), normalize(IN.binormal.xyz), normalize(IN.normal.xyz));
        float3 worldN = normalize(mul(N, worldFrame));

        specularLight += SkinSkyReflection(-normalize(worldPos), worldFrame[2], worldN);
        ambientLight = SkinAmbient(AmbientColor.rgb, albedo, worldN, normalize(mul(Nsoft, worldFrame)));
        diffuseLight += ambientLight;
    #endif

    float4 color;
    color.rgb = LinEncode(diffuseLight + specularLight * skinCompensation);

    // The highlights' share of the colour once the pass is done: the AD passes are multiplied by
    // the base texture afterwards, which the compensation above already allowed for.
    // Only when the scattering effect's target is bound (otherwise COLOR1 is discarded).
    float3 specularShare = 0.0f;
    [branch] if (SkinScreenSpaceScatter.x > 0.0f) {
        float3 finalDiffuse = diffuseLight;
        #if defined(ONLY_LIGHT) && !defined(DIFFUSE)
            finalDiffuse *= LinColor(adAlbedo);
        #endif
        specularShare = LinEncode(finalDiffuse + specularLight) - LinEncode(finalDiffuse);
    }

    #ifdef FOG_COLOR
        bool fogOn = Toggles.y > 0.0f;
        color.rgb = fogOn ? lerp(color.rgb, IN.fog.rgb, IN.fog.a) : color.rgb;
        specularShare *= fogOn ? 1.0f - IN.fog.a : 1.0f;
    #endif

    [branch] if (TESR_SkinDebugData.x > 0.0f)
        color.rgb = SkinDebugView(TESR_SkinDebugData.x, diffuseLight - ambientLight, ambientLight, specularLight, albedo, adAlbedo);

    #if defined(DIFFUSE)
        color.a = 1.0f;
    #elif defined(ONLY_LIGHT)
        color.a = baseColor.a;
    #else
        color.a = baseColor.a * AmbientColor.a;
    #endif

    PS_OUTPUT OUT;
    OUT.color = color;
    OUT.scatter = float4(specularShare, length(worldPos));
    #if defined(DIFFUSE)
        OUT.albedo = 1.0f;   // never written: additive passes leave the albedo target alone
    #elif defined(ONLY_LIGHT)
        // What the engine multiplies this pass by, and the colour once it has.
        OUT.albedo = float4(saturate(adAlbedo), luma(color.rgb * adAlbedo));
    #else
        OUT.albedo = float4(saturate(baseColor.rgb), luma(color.rgb));
    #endif
    return OUT;
}

#endif // PS
