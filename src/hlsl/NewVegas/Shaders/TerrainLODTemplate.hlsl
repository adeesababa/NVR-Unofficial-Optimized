// Template for LOD terrain shaders.

#if defined(__INTELLISENSE__)
    #define PS
#endif

#define REVERSED_DEPTH

#include "includes/Helpers.hlsl"
// UNOFFICIAL linear lighting (Includes/LinearLight.hlsl, [Shaders.PBR.Linear]): the far land lights in linear space while it
// is on, as TerrainTemplate.hlsl. Compiled in only when on at startup (LINEAR_LIGHTING_ON, ShaderRecord::LoadShader).
#ifndef LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING_ON 0
#endif
#if defined(PS) && LINEAR_LIGHTING_ON
    #define LINEAR_LIGHTING
#endif
#include "includes/Terrain.hlsl"
#include "includes/Shadow.hlsl"
#include "includes/TerrainErosion.hlsl"


struct VS_INPUT {
    float4 position : POSITION;
    float4 uv : TEXCOORD0;
    float geomorphHeight : TEXCOORD1;
};

struct VS_OUTPUT {
    float4 fog : COLOR1;
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 sunDirection : TEXCOORD1;
    float3 lPosition : TEXCOORD2;
    float3 eyePosition : TEXCOORD3;
    float3 shadowWorldPos : TEXCOORD4;   // TEXCOORD4-7 unused by this template
};

#ifdef VS

row_major float4x4 ModelViewProj : register(c0);
row_major float4x4 ObjToCubeSpace : register(c8);

float4 HighDetailRange : register(c12);
float4 FogParam : register(c14);
float3 FogColor : register(c15);
float4 EyePosition : register(c16);
float4 GeomorphParams : register(c19);
float4 LightData : register(c25);

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float4 posLS = IN.position;
    float4 geomorphPos = IN.position;
    geomorphPos.z = lerp(IN.geomorphHeight.x, IN.position.z, GeomorphParams.x);

    float q0 = (abs(dot(ObjToCubeSpace[1].xyzw, geomorphPos.xyzw) - HighDetailRange.y) < HighDetailRange.w ? 1.0 : 0.0);
    float q1 = (abs(dot(ObjToCubeSpace[0].xyzw, geomorphPos.xyzw) - HighDetailRange.x) < HighDetailRange.z ? 1.0 : 0.0);

    posLS.z = geomorphPos.z - ((q0.x * q1.x) * GeomorphParams.y);
    float4 posPS = mul(ModelViewProj, posLS.xyzw);

    OUT.position.xyzw = posPS.xyzw;
    OUT.uv.xy = IN.uv.xy;
    OUT.sunDirection = LightData.xyz;
    
    // Fog.
    float3 fogPos = OUT.position.xyz;
    #ifdef REVERSED_DEPTH
        fogPos.z = OUT.position.w - fogPos.z;
    #endif
    float fogStrength = 1 - saturate((FogParam.x - length(fogPos)) / FogParam.y);
    OUT.fog.rgb = FogColor.rgb;
    OUT.fog.a = pow(fogStrength, FogParam.z);

    OUT.lPosition.xyz = posLS.xyz;
    OUT.eyePosition.xyz = EyePosition.xyz;
    OUT.shadowWorldPos = GetShadowWorldPos(posPS);

    return OUT;
};

#endif  // Vertex shader.

struct PS_INPUT
{
    float4 fog : COLOR1;
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float3 sunDirection : TEXCOORD1_centroid;
    float3 lPosition : TEXCOORD2_centroid;
    float3 eyePosition : TEXCOORD3_centroid;
    float3 shadowWorldPos : TEXCOORD4_centroid;
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

#ifdef PS

sampler2D BaseMap : register(s0);
sampler2D NormalMap : register(s1);
sampler2D LODParentTex : register(s4);
sampler2D LODParentNormals : register(s6);
sampler2D LODLandNoise : register(s7);

float4 AmbientColor : register(c1);
float4 PSLightColor : register(c3);
float4 LODTexParams : register(c31);

float4 LandLODSpec : register(c38);


static const float fUVScale = 1.f / 128.f;
static const float fUVScaleQuant = 127.f / 128.f;
static const float fUVOffset = 1.f / 256.f;
static const float fNoiseScale = 0.8f;
static const float fNoiseOffset = 0.55f;

PS_OUTPUT main(PS_INPUT IN) {
    PS_OUTPUT OUT;

    float4 normal = tex2D(NormalMap, IN.uv);
    float4 parentNormal = tex2D(LODParentNormals, (IN.uv * 0.5f) + LODTexParams.xy);

    float noise = tex2D(LODLandNoise, IN.uv * TESR_TerrainExtraData.w).r;

    normal.rgba = LODTexParams.w >= 1 ? normal.rgba : lerp(parentNormal, normal.rgba, LODTexParams.w);
    normal.rgb = expand(normal.rgb);
    normal.rgb = normalize(normal.rgb);
    normal.a *= LandLODSpec.x > 0.0f ? 1.0f : 0.0f;

    float2 uv = (IN.uv * fUVScaleQuant) + fUVOffset;
    float3 parentColor = tex2D(LODParentTex, (0.5f * uv) + lerp(LODTexParams.xy, 0.25f, fUVScale)).rgb;
    float3 baseColor = tex2D(BaseMap, uv).rgb;
    
    baseColor = LODTexParams.w >= 1 ? baseColor : lerp(parentColor, baseColor, LODTexParams.w);
    // UNOFFICIAL 2026-10-19 LOD Blending ([Shaders.Terrain.LODBlending]; after Community Shaders' LOD Blending, GPL-3.0): the far
    // land's colour by a gamma and a brightness, to match the near land. Skipped while off.
    [branch] if (TESR_LODBlendData.w > 0.0f) baseColor = pow(abs(baseColor), TESR_LODBlendData.y) * TESR_LODBlendData.x;
    
    // UNOFFICIAL erosion relief ([Shaders.Terrain.Erosion], after Physically-Based-Reloaded): the LOD normal map's slopes
    // carved with branching gullies; their floors darker. The pixel's footprint (top level: derivatives) fades the layers
    // too fine to show. Skipped while off.
    float3 footprintX = ddx(IN.shadowWorldPos);
    float3 footprintY = ddy(IN.shadowWorldPos);
    float footprint = max(1.0f, max(length(footprintX.xy), length(footprintY.xy)));
    [branch] if (TESR_TerrainErosionData.x > 0.0f) {
        float cavity;
        normal.xyz = GetErodedNormal(normal.xyz, IN.shadowWorldPos, footprint, cavity);
        baseColor *= 1.0f - cavity * TESR_TerrainErosionData.z;
    }

    float3 eyeDir = normalize(IN.eyePosition.xyz - IN.lPosition.xyz);
    
    // Forward sun shadows. Passed as parallaxMultiplier, which getSunLighting applies to
    // the sun colour only; ambient is added afterwards and stays untouched.
    // ddx/ddy must stay at top level, outside dynamic flow control.
    float3 shadowNormal = GetShadowGeometricNormal(IN.shadowWorldPos);
    float sunShadow = FORWARD_SHADOWS ? GetSunShadow(IN.shadowWorldPos, shadowNormal) : 1.0f;

    float3 lighting = getSunLighting(IN.sunDirection.xyz, LinColor(PSLightColor.rgb), eyeDir, normal.xyz, LinColor(AmbientColor.rgb), LinColor(baseColor), normal.a, LandLODSpec.x, 1.0, sunShadow, shadowNormal);

    float3 final = LinEncode(lighting);	// UNOFFICIAL linear lighting: back to the stored (gamma) space, before the fog
    final = lerp(final, final * (0.8 * noise + 0.55), saturate(TESR_TerrainExtraData.z)); // Apply noise.
    final = lerp(final, IN.fog.rgb, IN.fog.a);

    OUT.color_0.rgb = final;
    OUT.color_0.a = 1;

    return OUT;
};

#endif  // Pixel shader.
