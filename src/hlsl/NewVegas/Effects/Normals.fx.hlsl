float4 TESR_ReciprocalResolution;

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
// UNOFFICIAL 2026-10-23 (sharednormals): technique NormalsSmooth, see the end of the file.
float4 TESR_NormalsSmoothData;	// x: reach in pixels (0 off)
sampler2D TESR_NormalsBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_NormalsSmoothTemp : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };


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

#include "Includes/Depth.hlsl"
#include "Includes/Helpers.hlsl"

// (linear depth, post-projection depth) for a texel; the second is recomputed from the first
// because the depth buffer may be a single channel (see projectedDepthFromLinear).
float2 DepthPair(float2 uv)
{
	float linear01 = tex2D(TESR_DepthBuffer, uv).x;
	return float2(linear01, projectedDepthFromLinear(linear01));
}

float3 ReconstructPositionFromDepth(float2 uv, float depth)
{
	float4 viewSpace = mul(float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, depth, 1), TESR_InvProjectionTransform);
	return viewSpace.xyz / viewSpace.w;
}

float4 ComputeNormals(VSOUT IN) :COLOR0
{
	float2 uv = IN.UVCoord;

	// improved normal reconstruction algorithm from 
	// https://gist.github.com/bgolus/a07ed65602c009d5e2f753826e8078a0

	// store coordinates at 1 and 2 pixels from center in all directions
	float4 rightUv = uv.xyxy + float4(1.0, 0.0, 2.0, 0.0) * TESR_ReciprocalResolution.xyxy; 
	float4 leftUv = uv.xyxy + float4(-1.0, 0.0, -2.0, 0.0) * TESR_ReciprocalResolution.xyxy; 
	float4 bottomUv = uv.xyxy + float4(0.0, 1.0, 0.0, 2.0) * TESR_ReciprocalResolution.xyxy; 
	float4 topUv =uv.xyxy + float4(0.0, -1.0, 0.0, -2.0) * TESR_ReciprocalResolution.xyxy; 

	// Each sample contains linear depth in x and device depth in y. Reuse the same nine
	// reads for edge selection and position reconstruction; the old shader fetched the five
	// reconstruction samples a second time.
	float2 centerDepth = DepthPair(uv);
	float2 rightDepth1 = DepthPair(rightUv.xy);
	float2 leftDepth1 = DepthPair(leftUv.xy);
	float2 rightDepth2 = DepthPair(rightUv.zw);
	float2 leftDepth2 = DepthPair(leftUv.zw);
	float2 topDepth1 = DepthPair(topUv.xy);
	float2 bottomDepth1 = DepthPair(bottomUv.xy);
	float2 topDepth2 = DepthPair(topUv.zw);
	float2 bottomDepth2 = DepthPair(bottomUv.zw);

	float depth = centerDepth.x * farZ;
	float4 H = float4(rightDepth1.x, leftDepth1.x, rightDepth2.x, leftDepth2.x) * farZ;
	float4 V = float4(topDepth1.x, bottomDepth1.x, topDepth2.x, bottomDepth2.x) * farZ;

	float2 he = abs((2 * H.xy - H.zw) - depth);
	float2 ve = abs((2 * V.xy - V.zw) - depth);

	// pick horizontal and vertical diff with the smallest depth difference from slopes
	float3 centerPoint = ReconstructPositionFromDepth(uv, centerDepth.y);
	float3 rightPoint = ReconstructPositionFromDepth(rightUv.xy, rightDepth1.y);
	float3 leftPoint = ReconstructPositionFromDepth(leftUv.xy, leftDepth1.y);
	float3 topPoint = ReconstructPositionFromDepth(topUv.xy, topDepth1.y);
	float3 bottomPoint = ReconstructPositionFromDepth(bottomUv.xy, bottomDepth1.y);
	float3 left = centerPoint - leftPoint;
	float3 right = rightPoint - centerPoint;
	float3 down = centerPoint - bottomPoint;
	float3 up = topPoint - centerPoint;

	float3 hDeriv = he.x > he.y ? left : right;
	float3 vDeriv = ve.x > ve.y ? down : up;

	// get view space normal from the cross product of the best derivatives
	// half3 viewNormal = normalize(cross(hDeriv, vDeriv));
	float3 viewNormal = normalize(cross(vDeriv, hDeriv));

	return float4 (compress(viewNormal), 1.0);
}


technique
{
	pass
	{ 
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ComputeNormals();
	}
}

// UNOFFICIAL 2026-10-23 (sharednormals; [Main.Main.Quality] SmoothNormals = reach in pixels, 0 off): every effect rebuilds its surface
// normal from the depth buffer (above), so a low-poly rock or terrain gets one flat normal per triangle and the cloud shadows
// (SunFar, N.L), the sun shadows' face offset (SunShadows) and the snow (SnowAccumulation) step at every triangle edge. This smooths
// TESR_NormalsBuffer ONCE, right after it is drawn, in place (pass X into a spare texture, pass Y back), so every effect that reads it
// gets the same smoothed normal; AmbientLight then skips its own half-resolution NormalSmoothing (same method, Effects/AmbientLight.fx.hlsl
// SmoothNormal): 3 taps each side at r/3, 2r/3 and r, each counted as far as it lies on the pixel's tangent plane (a silhouette is a
// depth step and keeps its edge; the tolerance grows with the distance so a neighbouring face bent up to ~30 degrees still counts)
// and as far as its normal is within about 40-60 degrees of the pixel's (a wall meeting the floor stays a crease). Full resolution:
// the tolerance per pixel is half the half-resolution one's per texel.
float3 PositionFromViewZ(float2 uv, float z) {
	return float3((uv.x * 2.0f - 1.0f) / TESR_ProjectionTransform[0][0], (1.0f - uv.y * 2.0f) / TESR_ProjectionTransform[1][1], 1.0f) * z;
}

float4 SmoothNormals(VSOUT IN, uniform bool alongY) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 source = alongY ? tex2Dlod(TESR_NormalsSmoothTemp, float4(uv, 0.0f, 0.0f)) : tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f));
	float3 normal = normalize(expand(source.xyz));
	float viewZ = readDepthLod(uv);
	[branch] if (TESR_NormalsSmoothData.x > 0.5f && viewZ < farZ * 0.99f) {
		float3 origin = PositionFromViewZ(uv, viewZ);
		float2 axis = alongY ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f);
		float spacing = TESR_NormalsSmoothData.x / 3.0f;
		float3 sum = normal;
		[unroll] for (int k = -3; k <= 3; k++) {
			if (k == 0) continue;
			float dist = abs((float)k) * spacing;
			float2 tapUV = uv + axis * (k * spacing) * TESR_ReciprocalResolution.xy;
			float onScreen = step(0.0f, tapUV.x) * step(tapUV.x, 1.0f) * step(0.0f, tapUV.y) * step(tapUV.y, 1.0f);
			tapUV = saturate(tapUV);
			float3 tapNormal = normalize(expand((alongY ? tex2Dlod(TESR_NormalsSmoothTemp, float4(tapUV, 0.0f, 0.0f)) : tex2Dlod(TESR_NormalsBuffer, float4(tapUV, 0.0f, 0.0f))).xyz));
			float tapZ = readDepthLod(tapUV);
			float tolerance = max(viewZ * 0.0005f * dist, 0.5f);	// a 30-degree bend at this distance stays within it; a rock's thickness does not
			float plane = abs(dot(PositionFromViewZ(tapUV, tapZ) - origin, normal));
			float weight = saturate(1.0f - plane / tolerance) * smoothstep(0.5f, 0.8f, dot(tapNormal, normal)) * onScreen;
			sum += tapNormal * weight;
		}
		normal = normalize(sum);
	}
	return float4(compress(normal), source.w);
}

technique NormalsSmooth {
	pass SmoothX { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 SmoothNormals(false); }
	pass SmoothY { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 SmoothNormals(true); }
}
