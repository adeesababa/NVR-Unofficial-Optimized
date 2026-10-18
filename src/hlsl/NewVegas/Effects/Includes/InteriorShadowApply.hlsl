float4 ApplyInteriorShadowDepth(float2 uv, float4 color, float shadowSample, float depth) {
    color.rgb = pows(color.rgb, 2.2);
	float3 eyeDir = toWorld(uv);
	float uniformDepth = length(depth * eyeDir);

	float Shadow = saturate(shadowSample);
	Shadow = lerp(Shadow, 1.0, invlerps(300, INTERIOR_SHADOW_MAXDISTANCE, uniformDepth));
	float darkness = scaledReinhard(TESR_ShadowData.y, 5.0);
    darkness = saturate(1 - darkness * TESR_ShadowFade.y);

	Shadow = saturate(lerp(darkness, 1, Shadow));
	float shadowPower = 1 - Shadow;

	float4 finalColor = saturate(Shadow * (pow(saturate(color), 1 - shadowPower * 0.2)));
	finalColor = lerp(luma(finalColor), finalColor, 1 + shadowPower * 0.3);

	finalColor += max(color - 1, 0.0);
    finalColor.rgb = pows(finalColor.rgb, 1.0/2.2);

	return float4(finalColor.rgb, 1);
}

float4 ApplyInteriorShadow(float2 uv, float4 color, float shadowSample) {
	return ApplyInteriorShadowDepth(uv, color, shadowSample, readDepth(uv));
}

float4 ApplyInteriorShadowLod(float2 uv, float4 color, float shadowSample) {
	return ApplyInteriorShadowDepth(uv, color, shadowSample, readDepthLod(uv));
}
