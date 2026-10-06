#include "Includes/Blur.hlsl"

/*
* A depth aware blur that requires the Depthbuffer and related functions from Depth.hlsl
*/

#ifndef BLUR_FULL_WRITE_ONLY
// perform depth aware 12 taps blur along the direction of the offsetmask
float4 DepthBlur(VSOUT IN, uniform sampler2D buffer, uniform float2 OffsetMask, uniform float blurRadius,uniform float depthDrop,uniform float endFade) : COLOR0
{
	float WeightSum = 0.114725602f;
	float4 color1 = tex2D(buffer, IN.UVCoord) * WeightSum;

	float depth1 = readDepth(IN.UVCoord);
	clip(endFade - depth1);

	depthDrop *= (depth1 / farZ);

	[unroll]
    for (int i = 0; i < cKernelSize; i++)
    {
		float2 uv = IN.UVCoord + (BlurOffsets[i] * OffsetMask) * blurRadius;
		float4 color2 = tex2D(buffer,  uv);
		float depth2 = readDepth(uv);
		float diff = abs(float(depth1 - depth2));

		int useForBlur = (diff <= depthDrop);
		color1 += BlurWeights[i] * color2 * useForBlur;
		WeightSum += BlurWeights[i] * useForBlur;
    }
	color1 /= WeightSum;
    return float4(color1.rgb, 1);
}
#endif

float4 DepthBlurFullValue(float2 uv, uniform sampler2D buffer, uniform float2 OffsetMask, uniform float blurRadius, uniform float depthDrop, uniform float endFade)
{
	float4 center = tex2Dlod(buffer, float4(uv, 0, 0));
	float depth1 = readDepthLod(uv);
	[branch] if (depth1 > endFade) return center;

	float WeightSum = 0.114725602f;
	float4 color1 = center * WeightSum;
	depthDrop *= (depth1 / farZ);

	[unroll]
    for (int i = 0; i < cKernelSize; i++)
    {
		float2 tapUV = uv + (BlurOffsets[i] * OffsetMask) * blurRadius;
		float4 color2 = tex2Dlod(buffer, float4(tapUV, 0, 0));
		float depth2 = readDepthLod(tapUV);
		float diff = abs(float(depth1 - depth2));

		int useForBlur = (diff <= depthDrop);
		color1 += BlurWeights[i] * color2 * useForBlur;
		WeightSum += BlurWeights[i] * useForBlur;
    }
	color1 /= WeightSum;
    return float4(color1.rgb, 1);
}

float4 DepthBlurFull(VSOUT IN, uniform sampler2D buffer, uniform float2 OffsetMask, uniform float blurRadius, uniform float depthDrop, uniform float endFade) : COLOR0
{
	return DepthBlurFullValue(IN.UVCoord, buffer, OffsetMask, blurRadius, depthDrop, endFade);
}

float4 DepthBlurKeep(VSOUT IN, uniform sampler2D buffer, uniform float2 OffsetMask, uniform float blurRadius,uniform float depthDrop,uniform float endFade) : COLOR0
{
	float4 center = tex2Dlod(buffer, float4(IN.UVCoord, 0, 0));
	float depth1 = readDepthLod(IN.UVCoord);
	[branch] if (depth1 > endFade) return center;

	float4 taps[cKernelSize];
	float4 spread = 0;
	[unroll]
	for (int t = 0; t < cKernelSize; t++) {
		taps[t] = tex2Dlod(buffer, float4(IN.UVCoord + (BlurOffsets[t] * OffsetMask) * blurRadius, 0, 0));
		spread = max(spread, abs(taps[t] - center));
	}
	[branch] if (max(spread.r, spread.g) < 0.0001f) return float4(center.rgb, 1);

	float WeightSum = 0.114725602f;
	float4 color1 = center * WeightSum;
	depthDrop *= (depth1 / farZ);

	[unroll]
    for (int i = 0; i < cKernelSize; i++)
    {
		float depth2 = readDepthLod(IN.UVCoord + (BlurOffsets[i] * OffsetMask) * blurRadius);
		float diff = abs(float(depth1 - depth2));

		int useForBlur = (diff <= depthDrop);
		color1 += BlurWeights[i] * taps[i] * useForBlur;
		WeightSum += BlurWeights[i] * useForBlur;
    }
	color1 /= WeightSum;
    return float4(color1.rgb, 1);
}
