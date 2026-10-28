
float4 TESR_SkyDitherData;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

float GradientNoise(float2 pixel) {
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 DitherPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float4 color = tex2D(TESR_RenderedBuffer, IN.UVCoord);
	[branch] if (TESR_SkyDitherData.y > 0.0 && tex2D(TESR_DepthBuffer, IN.UVCoord).x < 0.996) return color;

	float2 pixel = vpos + TESR_SkyDitherData.z * float2(5.588238, 3.235);
	float noise = GradientNoise(pixel) + GradientNoise(pixel + float2(47.0, 17.0)) - 1.0;
	color.rgb += noise * TESR_SkyDitherData.x;
	return color;
}

technique {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DitherPS();
	}
}
