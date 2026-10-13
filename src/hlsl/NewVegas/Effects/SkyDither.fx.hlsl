// UNOFFICIAL: sky dither (src/effects/SkyDither.cpp). The last colour change before the picture goes to the 8-bit screen:
// smooth gradients (the sky above all) round to 256 levels there and show bands. Adding a faint noise of about one level
// first makes neighbouring pixels round up or down in proportion to where the true value lies, so the eye sees the smooth
// gradient instead of the steps. Triangular noise (two uniform values added) spreads the rounding error evenly at every
// brightness, which plain uniform noise does not. Sky only by default; the weapon and the world are left alone.

float4 TESR_SkyDitherData;   // x: noise size (1/255 = one 8-bit step), y: 1 = sky only, z: noise frame offset

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

// Interleaved gradient noise (Jimenez 2014) in [0, 1).
float GradientNoise(float2 pixel) {
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 DitherPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float4 color = tex2D(TESR_RenderedBuffer, IN.UVCoord);
	[branch] if (TESR_SkyDitherData.y > 0.0 && tex2D(TESR_DepthBuffer, IN.UVCoord).x < 0.996) return color;

	// Two noise values from different spots of the pattern, both moved on every frame; their sum is triangular.
	float2 pixel = vpos + TESR_SkyDitherData.z * float2(5.588238, 3.235);
	float noise = GradientNoise(pixel) + GradientNoise(pixel + float2(47.0, 17.0)) - 1.0;
	// Per channel the same noise: a grey step stays grey instead of picking up coloured speckle.
	color.rgb += noise * TESR_SkyDitherData.x;
	return color;
}

technique {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DitherPS();
	}
}
