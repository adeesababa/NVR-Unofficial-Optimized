// UNOFFICIAL 2026-10-27 FitToDepth ([Shaders.ShadowsExteriors.Main] FitToDepth / FitToDepthDebug; src\core\SunDepthFit.h, which
// loads and draws this file itself: it is not one of the menu's effects). The sun cascades fitted to the depth actually on screen.
//   Reduce      : the linear depth buffer -> a grid of 8 x 8 pixel blocks. R: the block's farthest surface, G: its nearest surface
//                 (1e9 when it has none), B: 1 when it shows sky (view depth in game units, as the cascade splits).
//   Count       : drawn over the block grid inside a D3D9 occlusion query, once per candidate distance: the blocks that survive the
//                 clip are counted (TESR_SunDepthFitData.y 0: farthest surface past .x, 1: blocks with sky, 2: nearest surface before .x).
//   CascadeView : the debug view (FitToDepthDebug): 1 = each sun cascade coloured as the lit shaders pick it, 2 = the measured blocks.
// Native D3D9 and DXVK alike: no readback of a texture, the counts come back through the queries a few frames later.

float4 TESR_ReciprocalResolution;   // x: 1 / width, y: 1 / height (the depth buffer is screen-sized)
float4 TESR_SunDepthFitData;        // Count: x the distance (view depth), y what is counted (above), zw: 1 / the block grid's size
float4 TESR_SunDepthFitInfo;        // CascadeView: x the view (1, 2), y how far the cascades reach now, z without the fit, w the nearest visible depth used
float4 TESR_SunDepthFitSplits;      // CascadeView: the four split depths in use (view depth, game units)
float4 TESR_SunDepthFitOriginal;    // CascadeView: the four split depths without the fit
float4 TESR_ShadowNearCenter;       // xyz: centre (camera-relative world), w: radius -- as the lit shaders select the cascades
float4 TESR_ShadowMiddleCenter;
float4 TESR_ShadowFarCenter;
float4 TESR_ShadowLodCenter;
float4 TESR_ShadowForwardData;      // w: HandoverBlend (world units)

// TESR_ samplers bind in declaration order: s0, s1. The block grid is not a TESR_ texture: SunDepthFit.h sets it on stage 2.
sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_SunDepthFitBlocks : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

static const float BlockPixels = 8.0;	// SunDepthFit::BlockSize
static const float SkyDepth = 0.98;		// linear depth (viewZ / farZ) from which a pixel is sky, as the other effects test it
static const float NoSurface = 1.0e9;

struct VSOUT {
	float4 vertPos : POSITION;
	float2 UVCoord : TEXCOORD0;
};

struct VSIN {
	float4 vertPos : POSITION0;
	float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN) {
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

// One output texel = one 8 x 8 block of screen pixels (blocks at the right and bottom edges read the edge pixels again: the
// clamp, harmless for a farthest / nearest).
float4 ReducePS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 first = floor(vpos) * BlockPixels + 0.5;
	float farthest = 0.0, nearest = 2.0, sky = 0.0;
	[loop] for (int y = 0; y < 8; y++) {
		[unroll] for (int x = 0; x < 8; x++) {
			float d = tex2Dlod(TESR_DepthBuffer, float4((first + float2(x, y)) * TESR_ReciprocalResolution.xy, 0.0, 0.0)).x;
			bool surface = d > 0.0 && d < SkyDepth;
			sky = d >= SkyDepth ? 1.0 : sky;
			farthest = surface ? max(farthest, d) : farthest;
			nearest = surface ? min(nearest, d) : nearest;
		}
	}
	return float4(farthest * farZ, nearest < 1.5 ? nearest * farZ : NoSurface, sky, 1.0);
}

float4 CountPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float3 block = tex2Dlod(NVR_SunDepthFitBlocks, float4((floor(vpos) + 0.5) * TESR_SunDepthFitData.zw, 0.0, 0.0)).xyz;
	float mode = TESR_SunDepthFitData.y;
	float keep = mode < 0.5 ? block.x - TESR_SunDepthFitData.x : mode < 1.5 ? block.z - 0.5 : TESR_SunDepthFitData.x - block.y;
	clip(keep);	// what is left is counted by the occlusion query
	return float4(1.0, 1.0, 1.0, 1.0);
}

// The fit bar along the top: view depth on a square-root scale up to the shadow distance, coloured by cascade (red Near, green
// Middle, blue Far, yellow Lod, grey past the last); the thick upper bar is the layout in use, the thin one under it the layout
// without the fit. The nearest visible depth used is marked white on the upper bar.
float3 BarColour(float depth, float4 splits) {
	return depth < splits.x ? float3(0.9, 0.2, 0.2) : depth < splits.y ? float3(0.2, 0.85, 0.2) : depth < splits.z ? float3(0.25, 0.4, 1.0) :
		depth < splits.w ? float3(0.95, 0.9, 0.2) : float3(0.15, 0.15, 0.15);
}

float4 CascadeViewPS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float3 color = tex2D(TESR_RenderedBuffer, uv).rgb;
	float2 pixel = floor(uv / TESR_ReciprocalResolution.xy);

	if (uv.x > 0.02 && uv.x < 0.98 && uv.y > 0.012 && uv.y < 0.042) {
		float t = (uv.x - 0.02) / 0.96;
		float depth = t * t * max(TESR_SunDepthFitInfo.z, 1.0);
		if (uv.y > 0.03 && uv.y < 0.033) return float4(0.0, 0.0, 0.0, 1.0);
		if (uv.y < 0.03) {
			float nearMark = TESR_SunDepthFitInfo.w;
			float markX = 0.02 + 0.96 * sqrt(saturate(nearMark / max(TESR_SunDepthFitInfo.z, 1.0)));
			if (nearMark > 0.0 && abs(uv.x - markX) < TESR_ReciprocalResolution.x * 1.5) return float4(1.0, 1.0, 1.0, 1.0);
			return float4(BarColour(depth, TESR_SunDepthFitSplits), 1.0);
		}
		return float4(BarColour(depth, TESR_SunDepthFitOriginal) * 0.8, 1.0);
	}

	if (TESR_SunDepthFitInfo.x > 1.5) {
		// 2: the measured block grid. Grey: the block's farthest surface (white = the shadow distance); magenta: past the cascades'
		// reach now (allowed for one block in 8192); orange: nearer than the nearest depth used; cyan: the block shows sky.
		float2 grid = ceil(1.0 / (TESR_ReciprocalResolution.xy * BlockPixels));
		float2 cell = floor(pixel / BlockPixels);
		float3 block = tex2Dlod(NVR_SunDepthFitBlocks, float4((cell + 0.5) / grid, 0.0, 0.0)).xyz;
		float3 shown = sqrt(saturate(block.x / max(TESR_SunDepthFitInfo.z, 1.0))).xxx;
		if (block.x > TESR_SunDepthFitInfo.y) shown = magenta.rgb;
		if (block.y < NoSurface * 0.5 && block.y < TESR_SunDepthFitInfo.w) shown = float3(1.0, 0.5, 0.0);
		if (block.z > 0.5) shown = lerp(shown, cyan.rgb, 0.6);
		return float4(lerp(color, shown, 0.75), 1.0);
	}

	// 1: the cascade each pixel's shadow comes from, chosen as Includes/Shadow.hlsl GetSunShadow does (the first sphere holding the
	// point); diagonal stripes where it cross-fades into the next (the hand-over band).
	float depth = readDepthLod(uv);
	if (depth <= 0.0 || depth >= farZ * SkyDepth) return float4(color, 1.0);
	float3 position = toWorld(uv) * depth;	// camera-relative, as the cascade centres
	float4 radii = float4(TESR_ShadowNearCenter.w, TESR_ShadowMiddleCenter.w, TESR_ShadowFarCenter.w, TESR_ShadowLodCenter.w);
	float4 distances = float4(length(position - TESR_ShadowNearCenter.xyz), length(position - TESR_ShadowMiddleCenter.xyz),
		length(position - TESR_ShadowFarCenter.xyz), length(position - TESR_ShadowLodCenter.xyz));
	float4 bands = max(0.1 * radii, min(TESR_ShadowForwardData.w, radii));	// Shadow.hlsl ShadowCascadeBands
	float3 tint = float3(0.45, 0.45, 0.45);	// past every cascade: no cascade shadow
	float band = 0.0;
	if (distances.x < radii.x) { tint = float3(1.0, 0.25, 0.25); band = distances.x > radii.x - bands.x ? 1.0 : 0.0; }
	else if (distances.y < radii.y) { tint = float3(0.25, 1.0, 0.25); band = distances.y > radii.y - bands.y ? 1.0 : 0.0; }
	else if (distances.z < radii.z) { tint = float3(0.3, 0.45, 1.0); band = distances.z > radii.z - bands.z ? 1.0 : 0.0; }
	else if (distances.w < radii.w) { tint = float3(1.0, 0.95, 0.25); band = distances.w > radii.w - bands.w ? 1.0 : 0.0; }
	float3 shown = tint * (0.3 + 0.7 * luma(color));	// the picture's own light and shadow kept inside the tint
	if (band > 0.5 && frac((pixel.x + pixel.y) / 10.0) < 0.5) shown *= 0.65;
	return float4(lerp(color, shown, 0.7), 1.0);
}

technique Reduce
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ReducePS();
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		AlphaBlendEnable = false;
		AlphaTestEnable = false;
		ScissorTestEnable = false;
		CullMode = None;
		ColorWriteEnable = 15;
	}
}

technique Count
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CountPS();
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		AlphaBlendEnable = false;
		AlphaTestEnable = false;
		ScissorTestEnable = false;
		CullMode = None;
		ColorWriteEnable = 15;
	}
}

technique CascadeView
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CascadeViewPS();
	}
}
