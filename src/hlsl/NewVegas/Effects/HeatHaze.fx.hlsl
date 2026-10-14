float4 TESR_ReciprocalResolution;
float4 TESR_HeatHazeData;
float4 TESR_HeatHazeNoise;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Depth.hlsl"

static const float PI = 3.14159265f;

struct VSOUT
{
	float4 vertPos : POSITION;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN
{
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN)
{
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

float hash(float2 p)
{
	return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

float valueNoise(float2 p, float period, float seed)
{
	float2 i = floor(p);
	float2 f = frac(p);
	float2 u = f * f * (3.0f - 2.0f * f);
	float x0 = fmod(i.x, period);
	float x1 = fmod(i.x + 1.0f, period);
	float a = hash(float2(x0, i.y) + seed);
	float b = hash(float2(x1, i.y) + seed);
	float c = hash(float2(x0, i.y + 1.0f) + seed);
	float d = hash(float2(x1, i.y + 1.0f) + seed);
	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float hazeNoise(float2 p, float period, float seed)
{
	float n = valueNoise(p, period, seed) * 0.65f + valueNoise(p * 2.0f, period * 2.0f, seed + 17.0f) * 0.35f;
	return n * 2.0f - 1.0f;
}

float4 HeatHazePS(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float3 color = tex2D(TESR_RenderedBuffer, uv).rgb;

	float startDistance = TESR_HeatHazeData.y;
	float fullDistance = TESR_HeatHazeData.z;

	float depth = readDepth(uv);
	float distanceFactor = smoothstep(startDistance, fullDistance, depth);
	[branch] if (distanceFactor <= 0.0f) return float4(color, 1.0f);

	float3 dir = normalize(toWorld(uv));
	float horizon = 1.0f - smoothstep(0.0f, TESR_HeatHazeData.w, abs(dir.z));
	float amount = TESR_HeatHazeData.x * distanceFactor * horizon;
	[branch] if (amount <= 1e-6f) return float4(color, 1.0f);

	float period = max(1.0f, floor(TESR_HeatHazeNoise.x * 2.0f * PI));
	float around = (atan2(dir.y, dir.x) / (2.0f * PI) + 0.5f) * period;
	float up = dir.z * TESR_HeatHazeNoise.x * 3.0f - TESR_HeatHazeNoise.z * TESR_HeatHazeNoise.y;
	float2 p = float2(around, up);

	float2 offset = float2(hazeNoise(p, period, 3.0f) * 0.5f, hazeNoise(p, period, 41.0f)) * amount;
	offset.x /= TESR_ReciprocalResolution.z;

	float2 sampleUV = uv + offset;
	if (readDepth(sampleUV) < startDistance) return float4(color, 1.0f);

	return float4(tex2D(TESR_RenderedBuffer, sampleUV).rgb, 1.0f);
}

technique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 HeatHazePS();
	}
}
