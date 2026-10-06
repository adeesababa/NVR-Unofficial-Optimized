// The sun's shadow at a world point, from NVR's cascaded shadow maps (TESR_ShadowAtlas): the same lookup as
// SunShadows.fx.hlsl's GetLightAmount (single tap per cascade, the cascade blend, the per-cascade normal offset), for
// effects that need the sun's visibility themselves -- with ForwardShadows on (the default) the game shaders apply the
// sun's shadow to the sun's light only, and TESR_PointShadowBuffer holds the contact shadows alone.
// The including effect declares sampler2D TESR_ShadowAtlas (in its sampler order) before including this, and includes
// Includes/Shadows.hlsl (the VSM / EVSM evaluation) first.

float4x4 TESR_ShadowCameraToLightTransformNear;
float4x4 TESR_ShadowCameraToLightTransformMiddle;
float4x4 TESR_ShadowCameraToLightTransformFar;
float4x4 TESR_ShadowCameraToLightTransformLod;
float4 TESR_SmoothedSunDir;
float4 TESR_ShadowFormatData;   // x: mode (0 VSM, 1 EVSM2, 2 EVSM4), y: format bits
float4 TESR_ShadowBlur;         // x: 1 / atlas resolution
float4 TESR_ShadowNearCenter;   // xyz: centre (camera-relative world), w: radius
float4 TESR_ShadowMiddleCenter;
float4 TESR_ShadowFarCenter;
float4 TESR_ShadowLodCenter;

float4 SunCascadeTexCoord(float4 coord) {
	coord.xyz /= coord.w;
	coord.x = coord.x * 0.5f + 0.5f;
	coord.y = coord.y * -0.5f + 0.5f;
	return coord;
}

float SunCascadeValue(float4x4 lightTransform, float4 coord, float offsetX, float offsetY, float bias, float bleedReduction) {
	float4 lightSpace = SunCascadeTexCoord(mul(coord, lightTransform));
	lightSpace.xy = lightSpace.xy * 0.5 + float2(offsetX, offsetY);   // the cascade's quadrant of the atlas
	float4 moments = tex2Dlod(TESR_ShadowAtlas, float4(lightSpace.xy, 0.0f, 0.0f));
	float shadow;
	[branch] if (TESR_ShadowFormatData.x == 0.0f)
		shadow = GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleedReduction);
	else if (TESR_ShadowFormatData.x == 1.0f)
		shadow = GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleedReduction, TESR_ShadowFormatData.y);
	else
		shadow = GetLightAmountValueEVSM4(moments, lightSpace.z, bias, bleedReduction, TESR_ShadowFormatData.y);
	return shadow;
}

// 1 lit .. 0 in the sun's shadow (camera-relative world position, world normal).
float GetSunCascadeLight(float3 positionWS, float3 normal) {
	float offsetScale = saturate(1.0f - dot(normal, TESR_SmoothedSunDir.xyz));
	float4 radii = float4(TESR_ShadowNearCenter.w, TESR_ShadowMiddleCenter.w, TESR_ShadowFarCenter.w, TESR_ShadowLodCenter.w);
	float4 offsetDistance = offsetScale * 2.5f * 4.0f * radii * max(TESR_ShadowBlur.x, 1.0f / 16384.0f);
	float bias = (TESR_ShadowFormatData.x == 0.0f ? 0.00001f : 0.01f) * (1.0f + offsetScale);
	const float blend = 0.9f;
#define SC_NEAR SunCascadeValue(TESR_ShadowCameraToLightTransformNear, float4(positionWS + offsetDistance.x * normal, 1), 0, 0, bias, 0.1f)
#define SC_MIDDLE SunCascadeValue(TESR_ShadowCameraToLightTransformMiddle, float4(positionWS + offsetDistance.y * normal, 1), 0.5, 0, bias, 0.2f)
#define SC_FAR SunCascadeValue(TESR_ShadowCameraToLightTransformFar, float4(positionWS + offsetDistance.z * normal, 1), 0, 0.5, bias, 0.6f)
#define SC_LOD SunCascadeValue(TESR_ShadowCameraToLightTransformLod, float4(positionWS + offsetDistance.w * normal, 1), 0.5, 0.5, bias, 0.8f)
	float4 distances = float4(length(positionWS - TESR_ShadowNearCenter.xyz), length(positionWS - TESR_ShadowMiddleCenter.xyz),
	                          length(positionWS - TESR_ShadowFarCenter.xyz), length(positionWS - TESR_ShadowLodCenter.xyz));
	float light = 1.0f;
	[branch] if (distances.x < radii.x) {
		light = distances.x < radii.x * blend ? SC_NEAR : lerp(SC_NEAR, SC_MIDDLE, smoothstep(radii.x * blend, radii.x, distances.x));
	}
	else if (distances.y < radii.y) {
		light = distances.y < radii.y * blend ? SC_MIDDLE : lerp(SC_MIDDLE, SC_FAR, smoothstep(radii.y * blend, radii.y, distances.y));
	}
	else if (distances.z < radii.z) {
		light = distances.z < radii.z * blend ? SC_FAR : lerp(SC_FAR, SC_LOD, smoothstep(radii.z * blend, radii.z, distances.z));
	}
	else if (distances.w < radii.w) {
		light = distances.w < radii.w * blend ? SC_LOD : lerp(SC_LOD, 1.0f, smoothstep(radii.w * blend, radii.w, distances.w));
	}
#undef SC_NEAR
#undef SC_MIDDLE
#undef SC_FAR
#undef SC_LOD
	return light;
}
