
float SkyShare(float3 worldNormal, float sunMask)
{
	float ambientE = TESR_AmbientOcclusionSky2.z;
	float sunE = TESR_AmbientOcclusionSky2.w * saturate(dot(worldNormal, TESR_SmoothedSunDir.xyz)) * sunMask;
	return ambientE / max(ambientE + sunE, 1.0e-5f);
}

float SkyFactor(float visibility, float3 worldNormal, float sunMask, float covered)
{
	float share = TESR_AmbientOcclusionSky3.z > 0.5f ? SkyShare(worldNormal, sunMask) : 1.0f;
	float factor = 1.0f - saturate(TESR_AmbientOcclusionSky2.y * (1.0f - visibility) * share);
	float view = TESR_AmbientOcclusionSky3.w;
	if (view > 0.5f) factor = view < 1.5f ? visibility : (view < 2.5f ? share : (view < 3.5f ? factor : 1.0f - covered));
	return factor;
}
