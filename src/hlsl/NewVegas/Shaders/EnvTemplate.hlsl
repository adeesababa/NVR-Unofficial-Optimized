// UNOFFICIAL: the environment-map pass ("chrome" reflections on guns, glass, metal props), replacing the game's
// SLS2050/SLS2051 vertex shaders and SLS2057/SLS2058 pixel shaders (src/effects/PBR.h). It is drawn on top of the
// object's own lighting: the object's cube map, along the reflected view, times its mask (the normal map's alpha or a
// custom mask) and the material's strength.
//
// The game's version adds that cube map at the same brightness whatever the light, with no reaction to it: a gun
// gleamed like chrome in a pitch-dark cave and looked the same at noon. Here it is first worked out exactly as the game
// did, then scaled by the light round the object ([Shaders.PBR.EnvMaps]): the ambient light and the sky along the
// reflected view, the same light the object shaders use (Object.hlsl getAmbientLighting). Lighting 0 gives the game's
// look. Where the first-person metal look is on (Object.hlsl getMetalAmbient), the metal already reflects its
// surroundings, so this pass is turned down there (WithMetal) rather than adding the reflection twice.
//
// VS: SLS2050 (and SLS2051, SKIN): the cube-space (world-aligned, camera-centred) tangent frame in TEXCOORD1-3 .xyz
//     -- tangent and binormal scaled by 0.1, as the game does, which flattens the normal map's bumps tenfold in the
//     reflection -- the view direction in their .w, the fog's visibility in TEXCOORD4.x, the vertex colour in COLOR0.
// PS: SLS2057, and SLS2058 (NEGATE_VIEW: the game's twin, which reads the view the other way round).
// Both stages must be shader model 3 together (D3D9 rejects a 2.x shader paired with a 3.0 one), so all four are
// replaced; SLS2050/2051 are the only game vertex shaders with this output layout.

#include "includes/Helpers.hlsl"

#if defined(VS)

struct VS_INPUT {
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float4 vertexColor : COLOR0;
#ifdef SKIN
    float3 blendWeight : BLENDWEIGHT;
    float4 blendIndices : BLENDINDICES;
#endif
};

struct VS_OUTPUT {
    float4 sPosition : POSITION;
    float2 uv : TEXCOORD0;
    float4 frameX : TEXCOORD1;   // cube-space x of tangent * 0.1, binormal * 0.1, normal; w: view x
    float4 frameY : TEXCOORD2;   // ... y
    float4 frameZ : TEXCOORD3;   // ... z
    float fog : TEXCOORD4;       // 1 - fog
    float3 vertexColor : COLOR0;
};

#ifndef SKIN
    row_major float4x4 ModelViewProj : register(c0);
    float4 ObjToCubeSpace[3] : register(c8);
#else
    #include "includes/SkinHelpers.hlsl"
    row_major float4x4 SkinModelViewProj : register(c1);
    float4 ObjToCubeSpace[3] : register(c9);    // the game's SkinToCubeSpace
    float4 Bones[54] : register(c44);
#endif
float4 FogParam : register(c14);

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;
    float4 position = IN.position;
    #ifndef SKIN
        float3 tangent = IN.tangent, binormal = IN.binormal, normal = IN.normal;
        OUT.sPosition = mul(ModelViewProj, position);
    #else
        float4 offset = IN.blendIndices.zyxw * 765.01001;
        float4 blend = IN.blendWeight.xyzz;
        blend.w = 1 - weight(IN.blendWeight.xyz);
        float3x3 tbn = BonesTransformTBN(Bones, offset, blend, IN.tangent, IN.binormal, IN.normal);
        float3 tangent = tbn[0], binormal = tbn[1], normal = tbn[2];
        position.w = 1;
        position.xyz = BonesTransformPosition(Bones, offset, blend, position);
        OUT.sPosition = mul(SkinModelViewProj, position);
    #endif
    tangent *= 0.1f;
    binormal *= 0.1f;
    OUT.frameX.xyz = float3(dot(tangent, ObjToCubeSpace[0].xyz), dot(binormal, ObjToCubeSpace[0].xyz), dot(normal, ObjToCubeSpace[0].xyz));
    OUT.frameY.xyz = float3(dot(tangent, ObjToCubeSpace[1].xyz), dot(binormal, ObjToCubeSpace[1].xyz), dot(normal, ObjToCubeSpace[1].xyz));
    OUT.frameZ.xyz = float3(dot(tangent, ObjToCubeSpace[2].xyz), dot(binormal, ObjToCubeSpace[2].xyz), dot(normal, ObjToCubeSpace[2].xyz));
    // The view: from the vertex to the camera, which sits at cube space's origin.
    float3 view = normalize(-float3(dot(ObjToCubeSpace[0], position), dot(ObjToCubeSpace[1], position), dot(ObjToCubeSpace[2], position)));
    OUT.frameX.w = view.x;
    OUT.frameY.w = view.y;
    OUT.frameZ.w = view.z;
    // Fog as the object shaders work it out (ObjectTemplate.hlsl), reversed depth included; the game's own shader used
    // the clip position as it was.
    float3 fogPos = OUT.sPosition.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = OUT.sPosition.w - fogPos.z;
    #endif
    OUT.fog = 1 - pow(1 - saturate((FogParam.x - length(fogPos)) / FogParam.y), FogParam.z);
    OUT.uv = IN.uv;
    OUT.vertexColor = IN.vertexColor.rgb;
    return OUT;
}

#elif defined(PS)

#include "includes/SkyAmbient.hlsl"

struct PS_INPUT {
    float2 uv : TEXCOORD0;
    float4 frameX : TEXCOORD1_centroid;
    float4 frameY : TEXCOORD2_centroid;
    float4 frameZ : TEXCOORD3_centroid;
    float fog : TEXCOORD4;
    float3 vertexColor : COLOR0;
};

sampler2D NormalMap : register(s0);
samplerCUBE EnvironmentCubeMap : register(s1);
sampler2D CustomEnvMask : register(s3);
float4 AmbientColor : register(c1);      // rgb: the ambient light, a: the material's alpha (all the game's version used)
float4 EnvToggles : register(c27);       // x: tint by the vertex colour, z: reflection strength, w: 1 = custom mask, 0 = normal map alpha

float4 TESR_PBRData : register(c32);     // w: AmbientScale
float4 TESR_PBRExtraData : register(c33); // y: SkylightingScale, z: SkylightingDirectionality
float4 TESR_PBRMetal : register(c34);    // x: the metal look's strength for this pass (Object.hlsl)
float4 TESR_PBREnv : register(c35);      // x: Lighting (0 the game's fixed brightness, 1 by the light), y: Strength, z: WithMetal

float4 main(PS_INPUT IN) : COLOR0 {
    // The game's reflection, as it worked it out.
    float4 normal = tex2D(NormalMap, IN.uv);
    float3 view = float3(IN.frameX.w, IN.frameY.w, IN.frameZ.w);
    #ifdef NEGATE_VIEW
        view = -view;
    #endif
    view = normalize(view);
    float3 n = normalize((normal.xyz - 0.5f) * 2.0f);
    float3 N = normalize(float3(dot(n, IN.frameX.xyz), dot(n, IN.frameY.xyz), dot(n, IN.frameZ.xyz)));
    float3 reflected = 2.0f * dot(N, view) * N - view * dot(N, N);
    float3 color = texCUBE(EnvironmentCubeMap, reflected).rgb;
    color *= lerp(normal.a, tex2D(CustomEnvMask, IN.uv).x, EnvToggles.w) * EnvToggles.z * AmbientColor.a;
    color = EnvToggles.x > 0.0f ? color * IN.vertexColor : color;

    // UNOFFICIAL: by the light round the object -- the ambient light and the sky along the reflected view (cube space is
    // world-aligned), scaled as the object shaders scale them.
    float3 surroundings = AmbientColor.rgb * TESR_PBRData.w + SkyAmbientRadiance(normalize(reflected), TESR_PBRExtraData.z) * TESR_PBRExtraData.y;
    color *= lerp((1.0f).xxx, surroundings * TESR_PBREnv.y, TESR_PBREnv.x);
    // Less where the metal look already reflects the surroundings (the first-person model).
    color *= lerp(1.0f, TESR_PBREnv.z, saturate(TESR_PBRMetal.x));
    return float4(color * IN.fog, 1.0f);
}

#endif
