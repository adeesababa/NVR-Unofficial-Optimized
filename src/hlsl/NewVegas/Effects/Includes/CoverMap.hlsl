
float4 TESR_CoverMapData;
float4 TESR_CoverMapWindow;
float4 TESR_CoverMapGlobal;
float4 TESR_CoverMapTrail;

float CoverMapTrodden(float stored)
{
	float d = (1.0f - stored) * TESR_CoverMapTrail.z;
	return stored > 0.0001f ? TESR_CoverMapTrail.y * (1.0f - smoothstep(0.35f * TESR_CoverMapTrail.x, TESR_CoverMapTrail.x, d)) : 0.0f;
}

float4 CoverMapReadAll(float2 rel, out float inside)
{
	float4 stored = tex2Dlod(TESR_CoverMapBuffer, float4(rel * TESR_CoverMapData.x + TESR_CoverMapData.yz, 0.0f, 0.0f));
	float2 fromCorner = rel - TESR_CoverMapWindow.xy;
	float2 edge = min(fromCorner, TESR_CoverMapWindow.z - fromCorner);
	inside = saturate(min(edge.x, edge.y) / max(TESR_CoverMapWindow.w, 1.0f)) * TESR_CoverMapGlobal.w;
	return float4(lerp(TESR_CoverMapGlobal.xyz, stored.rgb, inside), stored.a * inside);
}

float4 CoverMapRead(float2 rel)
{
	float inside;
	float4 values = CoverMapReadAll(rel, inside);
	return float4(values.xyz, inside);
}
