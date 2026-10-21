float4 TESR_ReciprocalResolution;
float4 TESR_WaterSettings;
float4 TESR_ShadowFade;
float4 TESR_ShadowSunLight;
float4 TESR_ShadowAmbientLight;
float4 TESR_PBRData;
float4 TESR_PBRExtraData;
float4 TESR_TerrainData;
float4 TESR_TerrainSkyData;
float4 TESR_SkyIrradiance[9];
float4 TESR_TerrainShadowData;
float4 TESR_TerrainShadowRange;
float4 TESR_TerrainShadowParams;
float4 TESR_SunFarData;
float4 TESR_SunFarClouds;
float4 TESR_SunFarCloudOffset[2];
float4 TESR_SunFarCloudWeight;
float4 TESR_SunFarCloudBlend;
float4 TESR_SunFarVolume;
float4 TESR_SunDirection;

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ShadowAtlas : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_TerrainShadowBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_TerrainShadowGround : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D NVR_CloudLayer0 : register(s5) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudLayer1 : register(s6) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudLayer2 : register(s7) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudLayer3 : register(s8) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudBlend0 : register(s9) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudBlend1 : register(s10) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudBlend2 : register(s11) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_CloudBlend3 : register(s12) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_VolumeCloudShadow : register(s13) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"

VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

float CascadeLight(float3 positionWS, float3 normal) {
	float4 radii = float4(TESR_ShadowNearCenter.w, TESR_ShadowMiddleCenter.w, TESR_ShadowFarCenter.w, TESR_ShadowLodCenter.w);
	float4 distances = float4(length(positionWS - TESR_ShadowNearCenter.xyz), length(positionWS - TESR_ShadowMiddleCenter.xyz),
	                          length(positionWS - TESR_ShadowFarCenter.xyz), length(positionWS - TESR_ShadowLodCenter.xyz));
	float c = distances.x < radii.x ? 0.0f : distances.y < radii.y ? 1.0f : distances.z < radii.z ? 2.0f : distances.w < radii.w ? 3.0f : 4.0f;
	[branch] if (c > 3.5f) return 1.0f;
	float4x4 lightTransform = c < 0.5f ? TESR_ShadowCameraToLightTransformNear : c < 1.5f ? TESR_ShadowCameraToLightTransformMiddle :
	                          c < 2.5f ? TESR_ShadowCameraToLightTransformFar : TESR_ShadowCameraToLightTransformLod;
	float2 quadrant = c < 0.5f ? float2(0.0f, 0.0f) : c < 1.5f ? float2(0.5f, 0.0f) : c < 2.5f ? float2(0.0f, 0.5f) : float2(0.5f, 0.5f);
	float radius = c < 0.5f ? radii.x : c < 1.5f ? radii.y : c < 2.5f ? radii.z : radii.w;
	float bleed = c < 0.5f ? 0.1f : c < 1.5f ? 0.2f : c < 2.5f ? 0.6f : 0.8f;
	float offsetScale = saturate(1.0f - dot(normal, TESR_SmoothedSunDir.xyz));
	float offset = offsetScale * 2.5f * 4.0f * radius * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
	float bias = (TESR_ShadowFormatData.x == 0.0f ? 0.00001f : 0.01f) * (1.0f + offsetScale);
	return SunCascadeValue(lightTransform, float4(positionWS + offset * normal, 1.0f), quadrant.x, quadrant.y, bias, bleed);
}

float3 LightColor(float3 c) { return pow(max(c, 1e-6f), TESR_SunFarData.z); }
float LightScale(float s) { return pow(max(s, 1e-6f), TESR_SunFarData.z); }

float3 SkyIrradiance(float3 n) {
	float3 irradiance = TESR_SkyIrradiance[0].rgb
		+ TESR_SkyIrradiance[1].rgb * n.y
		+ TESR_SkyIrradiance[2].rgb * n.z
		+ TESR_SkyIrradiance[3].rgb * n.x
		+ TESR_SkyIrradiance[4].rgb * (n.x * n.y)
		+ TESR_SkyIrradiance[5].rgb * (n.y * n.z)
		+ TESR_SkyIrradiance[6].rgb * (3.0f * n.z * n.z - 1.0f)
		+ TESR_SkyIrradiance[7].rgb * (n.x * n.z)
		+ TESR_SkyIrradiance[8].rgb * (n.x * n.x - n.y * n.y);
	return sqrt(max(irradiance, 0.0f));
}

float TerrainShadowCoverage(float3 worldPos) {
	if (TESR_TerrainShadowData.w <= 0.0f) return 0.0f;
	float2 uv = float2((worldPos.x - TESR_TerrainShadowData.x) * TESR_TerrainShadowData.z, (TESR_TerrainShadowData.y - worldPos.y) * TESR_TerrainShadowParams.x);
	return all(uv >= 0.0f) && all(uv <= 1.0f) ? 1.0f : 0.0f;
}

float TerrainShadow(float3 worldPos) {
	[branch] if (TESR_TerrainShadowData.w <= 0.0f) return 1.0f;
	float2 uv = float2((worldPos.x - TESR_TerrainShadowData.x) * TESR_TerrainShadowData.z, (TESR_TerrainShadowData.y - worldPos.y) * TESR_TerrainShadowParams.x);
	float2 edge = min(uv, 1.0f - uv);
	float inside = saturate(min(edge.x, edge.y) * 20.0f);
	[branch] if (inside <= 0.0f) return 1.0f;
	float3 encoded = float3(tex2Dlod(TESR_TerrainShadowBuffer, float4(uv, 0.0f, 0.0f)).rg, tex2Dlod(TESR_TerrainShadowGround, float4(uv, 0.0f, 0.0f)).r) * TESR_TerrainShadowRange.y + TESR_TerrainShadowRange.x;
	float2 heights = encoded.xy - TESR_TerrainShadowRange.z;
	float z = max(worldPos.z, encoded.z);
	float lower = heights.y - TESR_TerrainShadowRange.w;
	float transition = heights.x + TESR_TerrainShadowRange.w - lower;
	float lit = transition <= 0.0f ? (z >= heights.x ? 1.0f : 0.0f) : saturate((z - lower) / transition);
	lit = saturate((lit - 0.5f) * TESR_TerrainShadowParams.w + 0.5f);
	return lerp(1.0f, lit, TESR_TerrainShadowData.w * inside);
}

float CloudCover(sampler2D layer, sampler2D incoming, float2 uv, float weight, float blend) {
	float a = tex2Dlod(layer, float4(uv, 0.0f, TESR_SunFarClouds.z)).a;
	a = lerp(a, tex2Dlod(incoming, float4(uv, 0.0f, TESR_SunFarClouds.z)).a, blend);
	return saturate(a * weight);
}

float CloudShadow(float3 rel) {
	[branch] if (TESR_SunFarData.x <= 0.0f) return 1.0f;
	float3 light = TESR_SmoothedSunDir.xyz;
	[branch] if (light.z <= 0.02f) return 1.0f;
	float2 meet = rel.xy + light.xy * ((TESR_SunFarClouds.x - rel.z) / light.z);
	float2 world = meet + TESR_CameraPosition.xy;
	float s, c;
	sincos(TESR_SunFarClouds.w, s, c);
	float2 uv = float2(world.x * c - world.y * s, world.x * s + world.y * c) * TESR_SunFarClouds.y;
	float clear = (1.0f - CloudCover(NVR_CloudLayer0, NVR_CloudBlend0, uv + TESR_SunFarCloudOffset[0].xy, TESR_SunFarCloudWeight.x, TESR_SunFarCloudBlend.x))
	            * (1.0f - CloudCover(NVR_CloudLayer1, NVR_CloudBlend1, uv + TESR_SunFarCloudOffset[0].zw, TESR_SunFarCloudWeight.y, TESR_SunFarCloudBlend.y))
	            * (1.0f - CloudCover(NVR_CloudLayer2, NVR_CloudBlend2, uv + TESR_SunFarCloudOffset[1].xy, TESR_SunFarCloudWeight.z, TESR_SunFarCloudBlend.z))
	            * (1.0f - CloudCover(NVR_CloudLayer3, NVR_CloudBlend3, uv + TESR_SunFarCloudOffset[1].zw, TESR_SunFarCloudWeight.w, TESR_SunFarCloudBlend.w));
	return saturate(1.0f - (1.0f - clear) * TESR_SunFarData.x);
}

float VolumeCloudShadow(float3 world) {
	[branch] if (TESR_SunFarVolume.x <= 0.0f) return 1.0f;
	float3 light = normalize(TESR_SunDirection.xyz);
	light.z = max(light.z, 0.25f);
	light = normalize(light);
	float3 p = world / 70.0f;
	float texel = TESR_SunFarVolume.y / TESR_SunFarVolume.z;
	float2 centre = floor(TESR_CameraPosition.xy / 70.0f / texel) * texel;
	float2 q = p.xy + light.xy * ((TESR_SunFarVolume.w - p.z) / light.z);
	float2 suv = (q - centre) / TESR_SunFarVolume.y + 0.5f;
	float2 edge = min(suv, 1.0f - suv);
	float inside = saturate(min(edge.x, edge.y) * 10.0f);
	return lerp(1.0f, tex2Dlod(NVR_VolumeCloudShadow, float4(suv, 0.0f, 0.0f)).r, inside * TESR_SunFarVolume.x);
}
float4 SunFarPS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float depth = readDepthLod(uv);
	[branch] if (depth <= 0.0f || depth >= farZ * 0.996f) return 1.0f;
	float3 rel = toWorld(uv) * depth;
	float3 world = TESR_CameraPosition.xyz + rel;

	float terrain = TerrainShadow(world);
	float cloud = CloudShadow(rel) * VolumeCloudShadow(world);
	float blocked = terrain * cloud;
	float shade = (1.0f - terrain) * TESR_TerrainShadowParams.z;

	[branch] if (TESR_SunFarData.y > 0.5f) {
		float3 tint = lerp(1.0f, float3(0.25f, 1.0f, 0.25f), (1.0f - terrain) * 0.8f) * lerp(1.0f, float3(0.35f, 0.5f, 1.0f), (1.0f - cloud) * 0.8f);
		if (TESR_SunFarData.y > 1.5f) tint *= lerp(1.0f, float3(0.85f, 0.9f, 1.0f), TerrainShadowCoverage(world));
		return float4(tint, 1.0f);
	}
	[branch] if (blocked >= 0.999f && shade <= 0.001f) return 1.0f;
	float3 normal = GetWorldNormalLod(uv);

	[branch] if (normal.z > 0.7f && abs(world.z - TESR_WaterSettings.x) < max(2.0f, depth * 0.002f)) return float4(1.0f, 1.0f, 1.0f, blocked);

	float cascade = 1.0f;
	[branch] if (TESR_ShadowFade.y > 0.0f) cascade = saturate(lerp(CascadeLight(rel, normal), 1.0f, TESR_ShadowFade.x));

	float terrainWeight = (TESR_TerrainData.z + TESR_TerrainData.w > 0.0f) ? smoothstep(0.5f, 0.8f, normal.z) : 0.0f;
	float3 scales = lerp(float3(TESR_PBRData.z, TESR_PBRData.w, TESR_PBRExtraData.y),
	                     float3(TESR_TerrainData.z, TESR_TerrainData.w, TESR_TerrainSkyData.x), terrainWeight);
	float NdotL = saturate(dot(normal, TESR_SmoothedSunDir.xyz));
	float3 sunLight = LightColor(TESR_ShadowSunLight.rgb) * (NdotL * LightScale(scales.x));
	float3 ambientLight = LightColor(TESR_ShadowAmbientLight.rgb) * LightScale(scales.y) + LightColor(SkyIrradiance(normal)) * LightScale(scales.z);
	float3 litNow = ambientLight + sunLight * cascade;
	float3 factor = saturate((ambientLight * (1.0f - shade) + sunLight * (cascade * blocked)) / max(litNow, 0.0001f));
	factor = pow(max(factor, 1e-6f), TESR_SunFarData.w);
	return float4(factor, blocked);
}

technique SunFar {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SunFarPS();
		ColorWriteEnable = 15;
	}
}
