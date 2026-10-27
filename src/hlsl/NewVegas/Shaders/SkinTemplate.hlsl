// The skin shader (UNOFFICIAL 2026-10-25: rewritten on this build's own stack; see Includes/Skin.hlsl for the lighting and
// where each model comes from). Every vanilla SKIN20xx vertex and pixel shader is built from this one file
// (SkinShaders::Templates() in src/effects/Skin.h), the way ObjectTemplate.hlsl builds the object shaders. It takes
//   - the sun's shadows from Includes/Shadow.hlsl (cascades, PCF / contact hardening, the character shadow map, the
//     smooth vertex normal and the skin's edge softness and reach: SHADOW_SKIN_SOFTNESS),
//   - the lamps' shadows indoors from Includes/PointShadowForward.hlsl (INTERIOR_SHADOWS records),
//   - the lamps' falloff from Includes/LampFalloff.hlsl, linear lighting from Includes/LinearLight.hlsl,
//   - rain on the first-person arms from Includes/HudRain.hlsl (HudDrops),
//   - and the screen-space skin blur's inputs (Effects/SkinScattering.fx.hlsl): COLOR1 / COLOR2 below.
//
// The engine's side, read off the vanilla shaders (shaderpackage010.sdp); only this has to match them:
//   VS constants: ModelViewProj c0 / SkinModelViewProj c1, FogParam c14, FogColor c15, EyePosition c16, ShadowProj c18-c23,
//   LightData c25+ (OBJECT space: [0] the sun's direction, then each lamp's position + radius; in the DIFFUSE family every
//   entry is a lamp), Bones c44-c97.
//   PS constants: AmbientColor c1, EmittanceColor c2 (.a = the live light count in the MANY family), PSLightColor c3+,
//   Toggles c27 (x vertex colour, y fog, w alpha test; not in the AD and DIFFUSE families).
//   Samplers: full passes s0 base, s1 normal, s2 / s3 the FaceGen maps, s6 / s7 the projected actor shadow; light-only
//   passes s0 base, s1 normal, s5 / s6 that shadow; DIFFUSE passes s0 the normal map alone.
//
// Variants (defines, from the map in Skin.h):
//   SKIN         bone-skinned geometry
//   POINTS=n     lamps after the sun (DIFFUSE: lamps in all)
//   PROJ_SHADOW  vanilla's projected actor shadow (never with more than 2 lamps)
//   ONLY_LIGHT   the AD pass: writes light only (the ambient included); the engine multiplies the texture in afterwards.
//                No fog, vertex colour or Toggles. The highlights are divided by that texture in advance (the FaceGen blend
//                on faces, which the SkinShader hook binds: NVR_SkinDraw.y), so they come out right after the multiply.
//   DIFFUSE      an added lamp-only pass. No ambient, base texture, fog or Toggles (and no texture to divide the highlights
//                by: a lamp-only pass's highlights come out tinted by the texture, as the old skin's did).
//   MANY         the many-light family (4 lamps): EmittanceColor.a says how many are live; the rest are skipped, not
//                multiplied by 0 (an unused slot can hold anything).
//
// Direct lighting is done in TANGENT space, as vanilla's is: the vertex shader takes the light and view vectors into the
// vertex's tangent frame, so nothing depends on the projection (the engine draws the first-person arms with their own
// camera; the world camera's inverse matrices only serve the sky, the shadows and the curvature, where a skew costs
// little). The world frame (the tangent, binormal and the smooth vertex normal in camera-relative world space) is rebuilt
// the way Shadow.hlsl rebuilds the position, so it is right for whatever camera drew the vertex.
//
// VS -> PS (all ten of ps_3_0's inputs in the largest variants):
//   TEXCOORD0  uv.xy, the sun's tangent-space direction .xy    TEXCOORD4  camera-relative world position, view.z
//   TEXCOORD1  world tangent, sun .z                           TEXCOORD5  lamp 1, lamp 4 .x
//   TEXCOORD2  world binormal, view.x (tangent space)          TEXCOORD6  lamp 2, lamp 4 .y
//   TEXCOORD3  world normal, view.y                            TEXCOORD7  lamp 3, lamp 4 .z, or the PROJ_SHADOW uvs
//   COLOR0/1   vertex colour, fog (the full and MANY families)
// A lamp's tangent-space vector divided by its radius gives both its direction and vanilla's falloff, 1 - |v|^2, exactly
// (the vector is rescaled to its object-space length: a skinned tangent frame can be sheared).
// Every SKIN vertex shader name is built from this file, so the pixel shader assumes its own vertex shader ran.

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

#include "includes/Helpers.hlsl"
// The camera matrices Shadow.hlsl rebuilds positions with move out of c100-c107 in the vertex shaders: the game's bone
// upload reaches past Bones[54] (c97) and overwrote them in skinned ones (see Shadow.hlsl).
#ifdef VS
    #define SHADOW_INVPROJ_REG c240
    #define SHADOW_INVVIEW_REG c244
#endif
#define SHADOW_SKIN_SOFTNESS 1   // CharacterShadowSoftness and CharacterShadowReach (Includes/Shadow.hlsl): skin only
// No sun cross-fade on skin (SunSmoothing CrossFade): a second full cascade lookup takes these shaders to 4,900-5,500 slots with
// PCF and contact hardening, past what D3D9 drivers create (about 3,600), and the game would then draw skin with its own shaders.
// A sun step shows as one small shadow move on skin instead of a fade; the ground and objects keep their fade.
#undef SUN_CROSSFADE
#define SUN_CROSSFADE 0
#ifdef PS
    #define SHADOW_CHARACTER_MAP_IN 1   // CharacterShadowMap (Includes/Shadow.hlsl CharacterMapShadow; compiled in only when on at startup)
    // TESR_SkinDebug (Includes/Skin.hlsl) declared here so Shadow.hlsl can read its LegacyFaceShadows switch (z): with it, the
    // CharacterShadowReach cap is the old one along the sun's ray (SHADOW_REACH_ALONG_RAY, Includes/Shadow.hlsl).
    float4 TESR_SkinDebug : register(c195);
    #define SKIN_DEBUG_DECLARED
    #define SHADOW_REACH_ALONG_RAY (TESR_SkinDebug.z < 0.5f)
    // SmithCorrelated ([Shaders.PBR.Main], restart): the same masking term as the object shaders (Includes/PBR.hlsl BRDF).
    #if defined(SMITH_CORRELATED_ON) && SMITH_CORRELATED_ON
        #define PBR_SMITH_CORRELATED 1
    #endif
#endif
#include "includes/Shadow.hlsl"

// How far toward the sun the pixel shader looks a second time into the shadow map, in game units (16 = 23 cm: past the far
// side of a hand or a head): lit there, a shadow here is the skin's own (Includes/Skin.hlsl SkinLight, selfLit).
#define SKIN_SELF_SHADOW_REACH 16.0f

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
    float4 worldPos : TEXCOORD4;
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

#ifdef DIFFUSE
    #define FIRST_POINT 0
#else
    #define FIRST_POINT 1
#endif

// Lamp i's tangent-space vector divided by its radius, at its object-space length.
float3 LampVector(int i, float3 position, float3x3 tbn) {
    const float4 lamp = LightData[FIRST_POINT + i];
    const float3 v = (lamp.xyz - position) / lamp.w;
    const float3 t = mul(tbn, v);
    return t * sqrt(dot(v, v) / max(dot(t, t), 1e-12f));
}

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 position = float4(IN.position.xyz, 1.0f);
    #ifndef SKIN
        const float3x3 tbn = float3x3(normalize(IN.tangent), normalize(IN.binormal), normalize(IN.normal));
    #else
        const float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        const float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
    #endif

    OUT.position = mul(MVP, position);

    // The world frame for the sky and the shadows (Shadow.hlsl's round trip through the draw's own projection), the
    // tangent-space view and sun for the direct light.
    const float3 worldPos = GetShadowWorldPos(OUT.position);
    const float3 viewTS = mul(tbn, EyePosition.xyz - position.xyz);
    OUT.tangent = float4(GetShadowSmoothNormal(MVP, position.xyz, tbn[0], worldPos).xyz, 0.0f);
    OUT.binormal = float4(GetShadowSmoothNormal(MVP, position.xyz, tbn[1], worldPos).xyz, viewTS.x);
    OUT.normal = float4(GetShadowSmoothNormal(MVP, position.xyz, tbn[2], worldPos).xyz, viewTS.y);
    OUT.worldPos = float4(worldPos, viewTS.z);

    #ifndef DIFFUSE
        const float3 sunTS = normalize(mul(tbn, LightData[0].xyz));
    #else
        const float3 sunTS = 0.0f;
    #endif
    OUT.uv = float4(IN.uv.xy, sunTS.xy);
    OUT.tangent.w = sunTS.z;

    float3 lamp4 = 0.0f;
    #if POINTS > 3
        lamp4 = LampVector(3, position.xyz, tbn);
    #endif
    #if POINTS > 0
        OUT.light1 = float4(LampVector(0, position.xyz, tbn), lamp4.x);
    #endif
    #if POINTS > 1
        OUT.light2 = float4(LampVector(1, position.xyz, tbn), lamp4.y);
    #endif
    #if POINTS > 2
        OUT.light3 = float4(LampVector(2, position.xyz, tbn), lamp4.z);
    #elif defined(PROJ_SHADOW)
        const float shadowParam = dot(ShadowProj[3].xyzw, position.xyzw);
        float2 shadowUV;
        shadowUV.x = dot(ShadowProj[0].xyzw, position.xyzw);
        shadowUV.y = dot(ShadowProj[1].xyzw, position.xyzw);
        OUT.shadowUVs.xy = ((shadowParam * ShadowProjTransform.xy) + shadowUV) / (shadowParam * ShadowProjTransform.w);
        OUT.shadowUVs.zw = ((shadowUV.xy - ShadowProjData.xy) / ShadowProjData.w) * float2(1, -1) + float2(0, 1);
    #endif

    #ifdef FOG_COLOR
        // Vanilla's fog, from the clip-space distance.
        const float fogStrength = saturate((FogParam.x - length(OUT.position.xyz)) / FogParam.y);
        OUT.fog = float4(FogColor, pow(1.0f - fogStrength, FogParam.z));
        OUT.color = IN.color;
    #endif

    return OUT;
}

#endif // VS

#if defined(PS)

#include "includes/Skin.hlsl"
// The lamps' shadows indoors (INTERIOR_SHADOWS records): lamp k of the draw is PSLightColor[k], looked up once at the top
// into ptShadow<k> (POINT_SHADOW_SCALAR: plain numbers, the debug views' tint applied where they are used).
#define POINT_SHADOW_SCALAR
#include "includes/PointShadowForward.hlsl"
// Rain on the first-person arms (Wet World HudDrops), compiled in only when on at startup.
#if defined(HUD_RAIN_ON) && HUD_RAIN_ON
    #define HUD_RAIN 1
#endif
#include "includes/HudRain.hlsl"

// The draw's light index of lamp 0-3 (literal numbers: PointShadowForward.hlsl pastes them into names).
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
// Two levels, so SKIN_LAMPn becomes its number before it is pasted: ptShadow<k> = lamp k's shadow, d its vector / radius.
#define SKIN_LAMP_SET(k, d) SKIN_LAMP_SET_(k, d)
#define SKIN_LAMP_SET_(k, d) ptShadow##k = POINT_SHADOW_LOOKUP(k, saturate(1.0f - dot(d, d)))
#define SKIN_LAMP_SHADOW(k) POINT_SHADOW(k, 1.0f)

struct PS_INPUT {
    float4 uv : TEXCOORD0;
    float4 tangent : TEXCOORD1_centroid;
    float4 binormal : TEXCOORD2_centroid;
    float4 normal : TEXCOORD3_centroid;
    float4 worldPos : TEXCOORD4;
#if POINTS > 0
    float4 light1 : TEXCOORD5_centroid;
#endif
#if POINTS > 1
    float4 light2 : TEXCOORD6_centroid;
#endif
#if POINTS > 2
    float4 light3 : TEXCOORD7_centroid;
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
    sampler2D FaceGenMap0 : register(s2);   // bound by the SkinShader hook on faces (NVR_SkinDraw.y)
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
    float4 Toggles : register(c27);   // x vertex colour, y fog, z shine (unused: the roughness is the setting's), w alpha test ref
#endif

// Lamp i: d its tangent-space vector / radius, shadow its lamp shadow (SKIN_LAMP_SHADOW, 1 without one). Vanilla's falloff
// 1 - |d|^2 through LampAttOther (the objects' decode or the physical shape, Includes/LampFalloff.hlsl), applied to the
// decoded light as the object shaders apply theirs.
float3 SkinLamp(int i, float3 d, float3 shadow, float3 albedo, float3 N, float3 Nsoft, float3 V, out float3 specular) {
    const float att = LampAttOther(saturate(1.0f - dot(d, d)));
    const float3 light = PBRLight(PSLightColor[FIRST_POINT + i].rgb) * att;
    float3 diffuse = 0.0f;
    specular = 0.0f;
    #ifdef MANY
        // Only the live lamps (EmittanceColor.a), and only within reach: the branch also keeps the compiler from
        // interleaving all four lamps, which overflows ps_3_0's 32 temporaries.
        [branch] if ((FIRST_POINT + i) < EmittanceColor.a && att > 0.0f)
    #endif
        diffuse = SkinLight(albedo, N, Nsoft, normalize(d).z, V, d, light, 1.0f, 0.0f, 1.0f, specular);
    specular *= shadow;
    return diffuse * shadow;
}

// [Shaders.Skin.Debug] DebugView 1-7, in place of the colour this pass writes:
//   1 which pass drew the skin: green one full pass (the sun and up to one lamp), cyan the many-lamp full pass, yellow a
//     light-only pass the engine multiplies by the texture afterwards, red added for every extra lamp pass on top
//   2 which settings are in use: blue Interiors, orange outdoors; bright where the screen-space blur takes this skin, dark
//     where the per-pixel diffusion stands in
//   3 direct light (sun and lamps, with the diffusion and transmission) on white skin
//   4 ambient and sky light on white skin
//   5 highlights alone: the lights' lobes, the sheen and the sky reflection
//   6 material: red roughness, green highlight strength (the mask x SpecularStrength), blue wetness
//   7 curvature: red the per-pixel diffusion (0 where the blur takes over), green thin features (transmission)
// The light-only passes are divided by the texture the engine multiplies them by afterwards, so every view reads true.
float3 SkinDebugView(float view, float3 direct, float3 ambient, float3 highlights, float3 albedo, float3 adAlbedo) {
    #if defined(DIFFUSE)
        if (view < 1.5f) return float3(0.5f, 0.0f, 0.0f);
        if (view > 2.5f && view < 3.5f) return LinEncode(direct);
        if (view > 4.5f && view < 5.5f) return LinEncode(highlights);
        return 0.0f;
    #else
        const float3 onWhite = 1.0f / max(albedo, 0.02f);
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
            c = TESR_SkinDebug.y > 0.0f ? float3(0.2f, 0.45f, 1.0f) : float3(1.0f, 0.55f, 0.1f);
            c *= NVR_SkinDraw.x > 0.0f ? 1.0f : 0.3f;
        }
        else if (view < 3.5f) c = LinEncode(direct * onWhite);
        else if (view < 4.5f) c = LinEncode(ambient * onWhite);
        else if (view < 5.5f) c = LinEncode(highlights);
        else if (view < 6.5f) c = float3(skinRoughness, saturate(skinSpecScale * 0.5f), saturate(TESR_SkinLook.y));
        else c = float3(skinCurvature, skinThinness, 0.0f);
        #if defined(ONLY_LIGHT)
            c /= max(adAlbedo, 0.05f);
        #endif
        return c;
    #endif
}

// COLOR1 and COLOR2 go to the screen-space blur's targets, bound only during skin draws in the world scene
// (src/effects/SkinScattering.h); anywhere else they are discarded.
//   COLOR1 rgb  the highlights' share of COLOR0 (gamma, fogged): taken out before the blur, put back after
//          a    the camera distance, which the blur checks against the depth buffer
//   COLOR2 rgb  the colour the light is multiplied by, divided out before the blur so only light diffuses
//          a    the luminance of the colour this draw leaves in the scene: anything drawn over the skin afterwards changes it
struct PS_OUTPUT {
    float4 color : COLOR0;
    float4 scatter : COLOR1;
    float4 albedo : COLOR2;
};

PS_OUTPUT main(PS_INPUT IN) {
    const float3 worldPos = IN.worldPos.xyz;

    // Rain on the first-person arms (Includes/HudRain.hlsl): the textures are read under the drops.
    HudRain(IN.uv.xy, worldPos, true);
    const float2 uv = HudRainUV(IN.uv.xy);

    // --- the lamps' shadows first, while little else is held (nothing without INTERIOR_SHADOWS)
    #if INTERIOR_SHADOWS && POINTS > 0
        const float3 ptWorldPos = worldPos;
        const float3 ptNormal = ShadowFacingNormal(IN.normal.xyz, worldPos);   // the smooth vertex normal, for the lookup's offset
        const float ptValid = 1.0f;
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

    // --- the material
    #if defined(DIFFUSE)
        float4 baseColor = 1.0f;
        const float3 adAlbedo = 1.0f;   // no texture to divide by
    #elif defined(ONLY_LIGHT)
        float4 baseColor = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        // What the engine multiplies this pass by afterwards: the base texture, or on faces the FaceGen blend its texture
        // pass applies, the same as the full pass's (the maps are bound on faces only; derivatives taken out here: none are
        // allowed inside the branch).
        float3 adAlbedo = baseColor.rgb;
        const float2 uvDx = ddx(IN.uv.xy);
        const float2 uvDy = ddy(IN.uv.xy);
        [branch] if (NVR_SkinDraw.y > 0.0f)
            adAlbedo = 2.0f * ((expand(tex2Dgrad(FaceGenMap0, uv, uvDx, uvDy).rgb) + baseColor.rgb) * (2.0f * tex2Dgrad(FaceGenMap1, uv, uvDx, uvDy).rgb));
        baseColor.rgb = 1.0f;
    #else
        const float4 baseTexture = HUD_RAIN_TEX(BaseMap, IN.uv.xy);
        clip(AmbientColor.a >= 1 ? 0 : (baseTexture.a - Toggles.w));

        const float3 faceGenVar = HUD_RAIN_TEX(FaceGenMap0, IN.uv.xy).rgb;
        const float3 faceGenBlend = HUD_RAIN_TEX(FaceGenMap1, IN.uv.xy).rgb;
        float4 baseColor = float4(2.0f * ((expand(faceGenVar) + baseTexture.rgb) * (2.0f * faceGenBlend)), baseTexture.a);
        baseColor.rgb = Toggles.x <= 0.0f ? baseColor.rgb : baseColor.rgb * IN.color.rgb;
        const float3 adAlbedo = 1.0f;
    #endif

    // --- normals: tangent space for the direct light, world for the sky
    const float4 normalTexel = HUD_RAIN_TEX(NormalMap, IN.uv.xy);
    const float3 N = HudRainNormal(normalize(expand(normalTexel.xyz)));
    const float3 Nsoft = normalize(expand(tex2Dbias(NormalMap, float4(uv, 0.0f, SKIN_SOFT_NORMAL_BIAS)).xyz));
    const float3 V = normalize(float3(IN.binormal.w, IN.normal.w, IN.worldPos.w));

    SkinSetup(N, IN.normal.xyz, worldPos, normalTexel.a, NVR_SkinDraw.x);
    skinRoughness = HudRainRoughness(skinRoughness);   // wet: smooth, and shining at least as a full mask would
    #if HUD_RAIN
        skinSpecScale = lerp(skinSpecScale, max(skinSpecScale, 1.0f), hudRainGloss);
    #endif
    const float3 albedo = LinColor(baseColor.rgb);

    // --- the sun's shadow (top level: derivatives), and vanilla's projected actor shadow
    #if !defined(DIFFUSE)
        const float3 sunTS = float3(IN.uv.zw, IN.tangent.w);
        float sunShadow = 1.0f;
        float sunShadowEdge = 0.0f;
        float selfLit = 1.0f;
        #if FORWARD_SHADOWS
            #if OBJECT_SMOOTH_NORMALS
                const float3 flatNormal = ShadowFacingNormal(IN.normal.xyz, worldPos);   // SmoothObjectNormals: no per-triangle normal is built
            #else
                const float3 flatNormal = GetShadowGeometricNormal(worldPos);
            #endif
            const float3 shadowNormal = ShadowLookupNormal(flatNormal, float4(IN.normal.xyz, SHADOW_VS_SENTINEL));   // SmoothCharacterShadows, and the skin's edge softness
            sunShadow = GetSunShadow(worldPos, shadowNormal);
            // How sharp the shadow's edge is here, on screen (Includes/Skin.hlsl SkinShadow: the red of light carried across a
            // shadow line shows only on penumbrae a few pixels wide). Top level: a derivative.
            sunShadowEdge = fwidth(sunShadow);
            // The surface's own far side (geometric N.L under 0.05), in shadow, with ShadowScatter on: is it the skin's
            // own shadow? One coarse look SKIN_SELF_SHADOW_REACH toward the sun (SkinLight uses it only there).
            [branch] if (sunShadow < 1.0f && sunTS.z < 0.05f && TESR_SkinLook.z > 0.0f)
                selfLit = SunShadowCoarse(worldPos + TESR_SmoothedSunDir.xyz * SKIN_SELF_SHADOW_REACH);
        #endif
        float3 projShadow = 1.0f;
        #ifdef PROJ_SHADOW
            projShadow = lerp(1.0f, tex2D(ShadowMap, IN.shadowUVs.xy).rgb, tex2D(ShadowMaskMap, IN.shadowUVs.zw).x);
        #endif
    #endif

    // --- the lights: diffuse and highlights kept apart (see PS_OUTPUT)
    float3 diffuseLight = 0.0f;
    float3 specularLight = 0.0f;
    float3 ambientLight = 0.0f;   // kept apart for the debug views
    float3 specular;

    #if !defined(DIFFUSE)
        const float3 sunLight = PBRLight(PSLightColor[0].rgb) * projShadow;
        diffuseLight += SkinLight(albedo, N, Nsoft, sunTS.z, V, sunTS, sunLight, sunShadow, sunShadowEdge, selfLit, specular);
        specularLight += specular;
        // The sun focused through each rain drop into a bright spot on its far side (Includes/HudRain.hlsl).
        diffuseLight += sunLight * (sunShadow * HudRainLens(sunTS).y) * albedo;
    #endif

    #if POINTS > 0
        diffuseLight += SkinLamp(0, IN.light1.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP0), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 1
        diffuseLight += SkinLamp(1, IN.light2.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP1), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 2
        diffuseLight += SkinLamp(2, IN.light3.xyz, SKIN_LAMP_SHADOW(SKIN_LAMP2), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif
    #if POINTS > 3
        diffuseLight += SkinLamp(3, float3(IN.light1.w, IN.light2.w, IN.light3.w), SKIN_LAMP_SHADOW(SKIN_LAMP3), albedo, N, Nsoft, V, specular);
        specularLight += specular;
    #endif

    // --- the sky and the ambient (the reflection first: it sets the share of the sky it takes)
    #if !defined(DIFFUSE)
        // Built here, not at the top: nine values held through every light overflow ps_3_0's 32 temporaries in the
        // many-light variants.
        const float3x3 worldFrame = float3x3(normalize(IN.tangent.xyz), normalize(IN.binormal.xyz), normalize(IN.normal.xyz));
        const float3 worldN = normalize(mul(N, worldFrame));
        specularLight += SkinSkyReflection(-normalize(worldPos), worldFrame[2], worldN);
        ambientLight = SkinAmbient(AmbientColor.rgb, albedo, worldN, normalize(mul(Nsoft, worldFrame)));
        diffuseLight += ambientLight;
    #endif

    // --- out. The light-only passes are multiplied by the texture afterwards: their highlights are divided by it here.
    const float3 compensation = 1.0f / max(LinColor(adAlbedo), 0.125f);
    float4 color;
    color.rgb = LinEncode(diffuseLight + specularLight * compensation);

    // The highlights' share of the colour once the pass is done, for the blur (only while its targets are bound).
    float3 specularShare = 0.0f;
    [branch] if (NVR_SkinDraw.x > 0.0f) {
        float3 finalDiffuse = diffuseLight;
        #if defined(ONLY_LIGHT) && !defined(DIFFUSE)
            finalDiffuse *= LinColor(adAlbedo);
        #endif
        specularShare = LinEncode(finalDiffuse + specularLight) - LinEncode(finalDiffuse);
    }

    #ifdef FOG_COLOR
        const bool fogOn = Toggles.y > 0.0f;
        color.rgb = fogOn ? lerp(color.rgb, IN.fog.rgb, IN.fog.a) : color.rgb;
        specularShare *= fogOn ? 1.0f - IN.fog.a : 1.0f;
    #endif

    [branch] if (TESR_SkinDebug.x > 0.0f)
        color.rgb = SkinDebugView(TESR_SkinDebug.x, diffuseLight - ambientLight, ambientLight, specularLight, albedo, adAlbedo);

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
        OUT.albedo = 1.0f;   // never written: the added passes leave the albedo target alone (its writes are masked off)
    #elif defined(ONLY_LIGHT)
        OUT.albedo = float4(saturate(adAlbedo), luma(color.rgb * adAlbedo));   // what the engine multiplies in, and the colour once it has
    #else
        OUT.albedo = float4(saturate(baseColor.rgb), luma(color.rgb));
    #endif
    return OUT;
}

#endif // PS
