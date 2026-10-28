#ifndef NVR_CAPTURED_NORMALS_READ_HLSL
#define NVR_CAPTURED_NORMALS_READ_HLSL

float4 TESR_CapturedNormalsData;

float3 CapturedDecodeNormal(float2 e)
{
	float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
	float t = saturate(-n.z);
	n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);
	return normalize(n);
}

bool CapturedNormal(float2 uv, float distance, out float3 n)
{
	n = float3(0.0f, 0.0f, 1.0f);
	[branch] if (TESR_CapturedNormalsData.x <= 0.0f) return false;
	float4 c = tex2Dlod(TESR_CapturedNormalsBuffer, float4(uv, 0.0f, 0.0f));
	n = CapturedDecodeNormal(c.rg);
	return c.a > 0.99f && abs(c.b - distance) <= distance * TESR_CapturedNormalsData.y + TESR_CapturedNormalsData.z;
}

bool CapturedActor(float2 uv, float distance)
{
	[branch] if (TESR_CapturedNormalsData.x <= 0.0f) return false;
	float4 c = tex2Dlod(TESR_CapturedNormalsBuffer, float4(uv, 0.0f, 0.0f));
	return c.a > 1.5f && abs(c.b - distance) <= distance * TESR_CapturedNormalsData.y + TESR_CapturedNormalsData.z;
}

bool CapturedNormalAgrees(float2 uv, float distance, float3 rebuilt, out float3 n)
{
	return CapturedNormal(uv, distance, n) && dot(n, rebuilt) > TESR_CapturedNormalsData.w;
}

#endif
