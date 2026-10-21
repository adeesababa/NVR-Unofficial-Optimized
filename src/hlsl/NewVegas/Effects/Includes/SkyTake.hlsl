float3 SkyTake(float3 linearColor, float factor, float visibility, float cap, float noLimit)
{
	float take = min(1.0f - factor, ((1.0f - visibility) * cap + noLimit) / max(luma(linearColor), 1.0e-6f));
	return linearColor * (1.0f - take);
}
