

float4 TESR_ReciprocalResolution;
float4 TESR_TAAData;
float4 TESR_TAAPrevProjection;
float4 TESR_TAACameraDelta;
float4x4 TESR_TAAPrevViewTransform;
float4 TESR_TAAWeapon;
float4 TESR_TAASharpen;

sampler2D TESR_SourceBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_TAAHistoryBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Depth.hlsl"

static const float historyWeight = TESR_TAAData.x;
static const float clipGamma = TESR_TAAData.y;
static const float historyValid = TESR_TAAData.z;
static const float debugView = TESR_TAAData.w;
static const float weaponHistoryScale = TESR_TAAWeapon.x;

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

float3 RGBToYCoCg(float3 c)
{
	return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
	              0.5f * c.r - 0.5f * c.b,
	             -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}

float3 YCoCgToRGB(float3 c)
{
	return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float2 Reproject(float2 uv, out float inFront)
{
	float3 cameraVector = toWorld(uv) * readDepth(uv);
	float3 fromPrevCamera = cameraVector + TESR_TAACameraDelta.xyz;

	float3 prevRight   = float3(TESR_TAAPrevViewTransform[0][0], TESR_TAAPrevViewTransform[1][0], TESR_TAAPrevViewTransform[2][0]);
	float3 prevUp      = float3(TESR_TAAPrevViewTransform[0][1], TESR_TAAPrevViewTransform[1][1], TESR_TAAPrevViewTransform[2][1]);
	float3 prevForward = float3(TESR_TAAPrevViewTransform[0][2], TESR_TAAPrevViewTransform[1][2], TESR_TAAPrevViewTransform[2][2]);

	float viewZ = dot(fromPrevCamera, prevForward);
	inFront = viewZ > 0.0f ? 1.0f : 0.0f;

	float2 ndc = float2(dot(fromPrevCamera, prevRight) * TESR_TAAPrevProjection.x,
	                    dot(fromPrevCamera, prevUp) * TESR_TAAPrevProjection.y) / max(viewZ, 1e-4f);
	return float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
}

float3 SampleHistory(float2 uv)
{
	float2 texelSize = TESR_ReciprocalResolution.xy;
	float2 samplePos = uv / texelSize;
	float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
	float2 f = samplePos - texPos1;

	float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
	float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
	float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
	float2 w3 = f * f * (-0.5f + 0.5f * f);

	float2 w12 = w1 + w2;
	float2 offset12 = w2 / w12;

	float2 texPos0 = (texPos1 - 1.0f) * texelSize;
	float2 texPos3 = (texPos1 + 2.0f) * texelSize;
	float2 texPos12 = (texPos1 + offset12) * texelSize;

	float3 result = 0.0f;
	result += tex2D(TESR_TAAHistoryBuffer, float2(texPos12.x, texPos0.y)).rgb  * (w12.x * w0.y);
	result += tex2D(TESR_TAAHistoryBuffer, float2(texPos0.x,  texPos12.y)).rgb * (w0.x  * w12.y);
	result += tex2D(TESR_TAAHistoryBuffer, float2(texPos12.x, texPos12.y)).rgb * (w12.x * w12.y);
	result += tex2D(TESR_TAAHistoryBuffer, float2(texPos3.x,  texPos12.y)).rgb * (w3.x  * w12.y);
	result += tex2D(TESR_TAAHistoryBuffer, float2(texPos12.x, texPos3.y)).rgb  * (w12.x * w3.y);
	float weights = w12.x * (w0.y + w12.y + w3.y) + w12.y * (w0.x + w3.x);

	return max(result / weights, 0.0f);
}

float3 ClipToBox(float3 history, float3 boxMin, float3 boxMax)
{
	float3 center = 0.5f * (boxMax + boxMin);
	float3 extents = 0.5f * (boxMax - boxMin) + 1e-4f;
	float3 offset = history - center;
	float3 units = abs(offset / extents);
	float maxUnit = max(units.x, max(units.y, units.z));
	return maxUnit > 1.0f ? center + offset / maxUnit : history;
}

struct TAAResult
{
	float3 color;
	float3 current;
	float3 historyRaw;
	float2 motionPixels;
	float weight;
	float clipAmount;
	float isViewModel;
	float depth;
};

TAAResult ResolveCore(float2 uv)
{
	TAAResult result;
	float2 texelSize = TESR_ReciprocalResolution.xy;

	float3 current = 0.0f;
	float3 moment1 = 0.0f;
	float3 moment2 = 0.0f;
	float3 neighbourMin = 1e5f;
	float3 neighbourMax = -1e5f;
	[unroll]
	for (int y = -1; y <= 1; y++) {
		[unroll]
		for (int x = -1; x <= 1; x++) {
			float3 c = RGBToYCoCg(tex2D(TESR_SourceBuffer, uv + float2(x, y) * texelSize).rgb);
			if (x == 0 && y == 0) current = c;
			moment1 += c;
			moment2 += c * c;
			neighbourMin = min(neighbourMin, c);
			neighbourMax = max(neighbourMax, c);
		}
	}
	moment1 /= 9.0f;
	moment2 /= 9.0f;
	float3 sigma = sqrt(max(moment2 - moment1 * moment1, 0.0f));

	float3 boxMin = max(neighbourMin, moment1 - clipGamma * sigma);
	float3 boxMax = min(neighbourMax, moment1 + clipGamma * sigma);

	float inFront;
	float2 prevUV = Reproject(uv, inFront);

	float isViewModel = tex2D(TESR_DepthBufferViewModel, uv).x > 0.0f ? 1.0f : 0.0f;
	prevUV = lerp(prevUV, uv, isViewModel);
	inFront = max(inFront, isViewModel);

	float2 inside = step(0.0f, prevUV) * step(prevUV, 1.0f);
	float onScreen = inside.x * inside.y;

	float3 historyRaw = SampleHistory(prevUV);
	float3 historyRawYCoCg = RGBToYCoCg(historyRaw);
	float3 history = ClipToBox(historyRawYCoCg, boxMin, boxMax);

	float weight = historyWeight * historyValid * onScreen * inFront;

	weight *= lerp(1.0f, weaponHistoryScale, isViewModel);

	result.color = YCoCgToRGB(lerp(current, history, weight));
	result.current = YCoCgToRGB(current);
	result.historyRaw = historyRaw;
	result.motionPixels = (prevUV - uv) / texelSize;
	result.weight = weight;
	result.clipAmount = length(history - historyRawYCoCg);
	result.isViewModel = isViewModel;
	result.depth = readDepth(uv);
	return result;
}

float4 ResolvePS(VSOUT IN) : COLOR0
{
	return float4(ResolveCore(IN.UVCoord).color, 1.0f);
}

float4 DebugPS(VSOUT IN) : COLOR0
{
	TAAResult r = ResolveCore(IN.UVCoord);

	float3 motion = float3(saturate(abs(r.motionPixels) / 16.0f), 0.0f);
	float3 error = saturate(abs(r.historyRaw - r.current) * 4.0f);
	float3 historyUse = float3(saturate(r.weight + r.clipAmount * 8.0f), r.weight, r.weight);
	float3 weaponMask = lerp(r.current * 0.35f, float3(1.0f, 0.0f, 1.0f), r.isViewModel * 0.75f);
	float3 depth = saturate(log2(1.0f + r.depth) / log2(1.0f + farZ)).xxx;

	float3 view = r.color;
	view = debugView > 0.5f && debugView < 1.5f ? motion : view;
	view = debugView > 1.5f && debugView < 2.5f ? error : view;
	view = debugView > 2.5f && debugView < 3.5f ? historyUse : view;
	view = debugView > 3.5f && debugView < 4.5f ? weaponMask : view;
	view = debugView > 4.5f ? depth : view;
	return float4(view, 1.0f);
}

float4 OutputPS(VSOUT IN) : COLOR0
{
	float3 center = tex2D(TESR_TAAHistoryBuffer, IN.UVCoord).rgb;
	float sharpness = TESR_TAASharpen.x;
	[branch] if (sharpness <= 0.0f) return float4(center, 1.0f);

	float2 texel = TESR_ReciprocalResolution.xy;
	float3 up    = tex2D(TESR_TAAHistoryBuffer, IN.UVCoord + float2(0.0f, -texel.y)).rgb;
	float3 down  = tex2D(TESR_TAAHistoryBuffer, IN.UVCoord + float2(0.0f,  texel.y)).rgb;
	float3 left  = tex2D(TESR_TAAHistoryBuffer, IN.UVCoord + float2(-texel.x, 0.0f)).rgb;
	float3 right = tex2D(TESR_TAAHistoryBuffer, IN.UVCoord + float2( texel.x, 0.0f)).rgb;

	float3 lo = min(center, min(min(up, down), min(left, right)));
	float3 hi = max(center, max(max(up, down), max(left, right)));

	float lumaLo = dot(lo, float3(0.2126f, 0.7152f, 0.0722f));
	float lumaHi = dot(hi, float3(0.2126f, 0.7152f, 0.0722f));
	float contrast = saturate((lumaHi - lumaLo) / (lumaHi + 0.05f));
	float amount = sharpness * 2.0f * (1.0f - 0.75f * contrast);

	float3 blurred = 0.25f * (up + down + left + right);
	float3 sharpened = center + (center - blurred) * amount;
	return float4(clamp(sharpened, lo, hi), 1.0f);
}

technique Resolve
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ResolvePS();
	}
}

technique Output
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 OutputPS();
	}
}

technique Debug
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DebugPS();
	}
}
