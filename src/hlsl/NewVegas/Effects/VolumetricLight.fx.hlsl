float4 TESR_ReciprocalResolution;
float4 TESR_SunColor;
float4 TESR_ShadowFade;
float4 TESR_VolumetricLightData;
float4 TESR_VolumetricLightData2;
float4 TESR_VolumetricLightLayout;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ShadowAtlas : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightMarch : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
texture NoTexture;

static const float Strength = TESR_VolumetricLightData.x;
static const float Darken = TESR_VolumetricLightData.y;
static const float Density = TESR_VolumetricLightData.z;
static const float MarchDistance = TESR_VolumetricLightData.w;
static const float Steps = TESR_VolumetricLightData2.x;
static const float Anisotropy = TESR_VolumetricLightData2.y;
static const float Fade = TESR_VolumetricLightData2.z;
static const float2 LowTexel = TESR_VolumetricLightLayout.xy;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"

float Bayer2(float2 a) { a = floor(a); return frac(a.x * 0.5 + a.y * a.y * 0.75); }
float Bayer4(float2 a) { return Bayer2(0.5 * a) * 0.25 + Bayer2(a); }

float MarchShadow(float3 position) {
	float4x4 lightTransform;
	float2 quadrant;
	float bleed;
	[branch] if (length(position - TESR_ShadowNearCenter.xyz) < TESR_ShadowNearCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformNear; quadrant = float2(0.0, 0.0); bleed = 0.1;
	}
	else if (length(position - TESR_ShadowMiddleCenter.xyz) < TESR_ShadowMiddleCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformMiddle; quadrant = float2(0.5, 0.0); bleed = 0.2;
	}
	else if (length(position - TESR_ShadowFarCenter.xyz) < TESR_ShadowFarCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformFar; quadrant = float2(0.0, 0.5); bleed = 0.6;
	}
	else if (length(position - TESR_ShadowLodCenter.xyz) < TESR_ShadowLodCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformLod; quadrant = float2(0.5, 0.5); bleed = 0.8;
	}
	else return 1.0;
	float4 lightSpace = SunCascadeTexCoord(mul(float4(position, 1.0), lightTransform));
	float4 moments = tex2Dlod(TESR_ShadowAtlas, float4(lightSpace.xy * 0.5 + quadrant, 0.0, 0.0));
	float bias = TESR_ShadowFormatData.x == 0.0 ? 0.00001 : 0.01;
	if (TESR_ShadowFormatData.x == 0.0) return GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleed);
	if (TESR_ShadowFormatData.x == 1.0) return GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleed, TESR_ShadowFormatData.y);
	float2 exponents = GetEVSMExponents(40.0, 5.0, TESR_ShadowFormatData.y);
	float warped = WarpDepth(lightSpace.z, exponents).x;
	float depthScale = bias * exponents.x * warped;
	return ChebyshevUpperBound(moments.xz, warped, depthScale * depthScale, bleed);
}

float4 MarchPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * LowTexel;
	float depth = readDepthLod(uv - 0.5 * TESR_ReciprocalResolution.xy);
	float isSky = depth >= farZ * 0.996 ? 1.0 : 0.0;
	float planar = isSky > 0.0 ? MarchDistance : min(depth, MarchDistance);

	float3 ray = toWorld(uv);
	float sigma = Density * length(ray);
	float dt = planar / Steps;
	float offset = Bayer4(vpos);

	float lit = 0.0, all = 0.0;
	[loop] for (float i = 0.0; i < Steps; i += 1.0) {
		float t = (i + offset) * dt;
		float transmittance = exp(-sigma * t);
		lit += MarchShadow(ray * t) * transmittance;
		all += transmittance;
	}
	float scattered = sigma * dt;
	return float4(lit * scattered, all * scattered, depth / farZ, 1.0);
}

float4 BlurPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float4 centre = tex2Dlod(TESR_VolumetricLightMarch, float4((vpos + 0.5) * LowTexel, 0, 0));
	float tolerance = max(centre.b * 0.05, 0.00002);
	float2 sum = 0.0;
	float weights = 0.0;
	[unroll] for (int y = -2; y < 2; y++) {
		[unroll] for (int x = -2; x < 2; x++) {
			float4 tap = tex2Dlod(TESR_VolumetricLightMarch, float4((vpos + float2(x, y) + 0.5) * LowTexel, 0, 0));
			float weight = 1.0 / (1.0 + abs(tap.b - centre.b) / tolerance);
			sum += tap.rg * weight;
			weights += weight;
		}
	}
	return float4(sum / weights, centre.b, 1.0);
}

float4 CompositePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);

	float2 position = uv / LowTexel;
	float2 base = floor(position);
	float2 fraction = position - base;
	float depth01 = depth / farZ;
	float tolerance = max(depth01 * 0.04, 0.00002);
	float2 air = 0.0;
	float weights = 1.0e-5;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float4 tap = tex2Dlod(TESR_VolumetricLightBuffer, float4((base + float2(x, y) + 0.5) * LowTexel, 0, 0));
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1.0 + abs(tap.b - depth01) / tolerance;
			air += tap.rg * weight;
			weights += weight;
		}
	}
	air /= weights;

	float cosTheta = dot(normalize(toWorld(uv)), TESR_SmoothedSunDir.xyz);
	float g = Anisotropy;
	float phase = min((1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-4), 1.5), 8.0);

	float3 lin = linearize(max(color.rgb, 0.0));
	float shadedAir = saturate(air.g - air.r);
	lin = lin * (1.0 - Darken * Fade * shadedAir) + linearize(TESR_SunColor.rgb) * (Strength * Fade * phase * air.r);
	return float4(delinearize(max(lin, 0.0)), color.a);
}

technique Composite {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CompositePS();
	}
}

technique March {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 MarchPS();
		Texture[3] = <NoTexture>;
	}
}

technique Blur {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurPS();
		Texture[4] = <NoTexture>;
	}
}
