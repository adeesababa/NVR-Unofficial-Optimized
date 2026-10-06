float4 TESR_ReciprocalResolution;
float4 TESR_SpecularData;
float4 TESR_SpecularEffects;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SkyColor;
float4 TESR_HorizonColor;
float4 TESR_WaterSettings;
float4 TESR_ShadowFade;
float4 NVR_BounceData;
float4 NVR_BounceLayout;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_PointShadowBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SpecularBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SpecularNormals : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ShadowAtlas : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D NVR_BounceBuffer : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
texture NoTexture;

static const float SunStrength = TESR_SpecularData.x;
static const float Roughness = TESR_SpecularData.y;
static const float SkyStrength = TESR_SpecularData.z;
static const float DrawDistance = TESR_SpecularData.w;
static const float AAStrength = TESR_SpecularEffects.x;
static const float F0 = TESR_SpecularEffects.y;
static const float PI = 3.14159265;
static const float LowScale = 4.0;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"

float3 ViewToWorld(float3 viewNormal) { return mul(TESR_ViewTransform, float4(viewNormal, 1.0)).xyz; }

float2 EnvBRDFApprox(float roughness, float NdotV) {
	const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
	const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
	float4 r = roughness * c0 + c1;
	float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
	return float2(-1.04, 1.04) * a004 + r.zw;
}

float4 SpecularPrepPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 px = TESR_ReciprocalResolution.xy;
	float2 uv = (vpos * LowScale + 0.5 * LowScale) * px;
	float depth = readDepthLod(uv - 0.5 * px);
	float tolerance = 0.12 * depth + 2.0;
	float3 sum = tex2Dlod(TESR_NormalsBuffer, float4(uv - px, 0, 0)).xyz * 2.0 - 1.0;
	float weight = 1.0;
	const float2 q[3] = { float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0) };
	[unroll] for (int k = 0; k < 3; k++) {
		float w = abs(readDepthLod(uv + q[k] * 0.5 * px) - depth) < tolerance ? 1.0 : 0.0;
		sum += w * (tex2Dlod(TESR_NormalsBuffer, float4(uv + q[k] * px, 0, 0)).xyz * 2.0 - 1.0);
		weight += w;
	}
	return float4(sum / weight, depth);
}

float4 SpecularLowPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 px = TESR_ReciprocalResolution.xy, lowPx = px * LowScale;
	float2 uv = (vpos + 0.5) * lowPx;
	float4 centre = tex2Dlod(TESR_SpecularNormals, float4(uv, 0, 0));
	float depth = centre.a;
	[branch] if (depth >= DrawDistance || depth >= farZ * 0.98) return float4(0.0, 0.0, 0.0, depth);

	float3 eyeVector = toWorld(uv) * depth;
	float3 V = -normalize(eyeVector);

	float3 sum = centre.xyz;
	float weight = 1.0;
	float tolerance = 0.05 * depth + 4.0;
	const float2 ring[16] = { float2(1, 0), float2(1, 1), float2(0, 1), float2(-1, 1), float2(-1, 0), float2(-1, -1), float2(0, -1), float2(1, -1),
	                          float2(3, 1), float2(1, 3), float2(-1, 3), float2(-3, 1), float2(-3, -1), float2(-1, -3), float2(1, -3), float2(3, -1) };
	[unroll] for (int k = 0; k < 16; k++) {
		float4 s = tex2Dlod(TESR_SpecularNormals, float4(uv + ring[k] * lowPx, 0, 0));
		float w = abs(s.a - depth) < tolerance ? 1.0 : 0.0;
		sum += w * s.xyz;
		weight += w;
	}
	float3 meanNormal = sum / weight;
	float agreement = saturate(length(meanNormal));
	float3 N = normalize(ViewToWorld(meanNormal));

	float3 worldPos = TESR_CameraPosition.xyz + eyeVector;
	[branch] if (N.z > 0.9 && abs(worldPos.z - TESR_WaterSettings.x) < (depth / farZ) * 200.0) return float4(0.0, 0.0, 0.0, depth);

	float alpha = Roughness * Roughness;
	float variance = (1.0 - agreement) / max(agreement, 0.001);
	float alpha2 = saturate(alpha * alpha + AAStrength * 2.0 * variance);
	float roughAA = sqrt(sqrt(alpha2));

	float3 L = normalize(TESR_SunDirection.xyz);
	float3 Hv = normalize(L + V);
	float NdotL = saturate(dot(N, L));
	float NdotV = max(dot(N, V), 0.0001);
	float NdotH = saturate(dot(N, Hv));
	float VdotH = saturate(dot(V, Hv));
	float dn = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
	float D = alpha2 / (PI * dn * dn);
	float vis = 0.5 / (NdotL * sqrt(NdotV * NdotV * (1.0 - alpha2) + alpha2) + NdotV * sqrt(NdotL * NdotL * (1.0 - alpha2) + alpha2) + 0.00001);
	float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
	float sunSpec = min(D * vis * F * NdotL * PI, 2.0) * SunStrength;
#ifdef SPECULAR_SIM_SHADOWS
	sunSpec *= tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0, 0)).r;
#else
	[branch] if (sunSpec > 0.00001 && TESR_ShadowFade.y > 0.0) sunSpec *= GetSunCascadeLight(eyeVector, N);
#endif

	float3 R = reflect(-V, N);
	float skyAt = sqrt(saturate(R.z));
	float below = R.z >= 0.0 ? 1.0 : lerp(0.6, 0.25, saturate(-R.z * 4.0));
	float2 env = EnvBRDFApprox(roughAA, NdotV);
	float skyAmount = (F0 * env.x + env.y) * SkyStrength * below;

	float fade = 1.0 - smoothstep(0.7 * DrawDistance, DrawDistance, depth);
	return float4(skyAmount * fade, skyAt, sunSpec * fade, depth);
}

bool AddSpecular(float2 uv, float depth, inout float3 lin) {
	float viewmodelDepth = tex2Dlod(TESR_DepthBufferViewModel, float4(uv, 0, 0)).x;
	bool weapon = TESR_DepthConstants.z == 0.0 ? viewmodelDepth < 0.9 : viewmodelDepth > 0.01;
	[branch] if (depth >= DrawDistance || depth >= farZ * 0.98 || weapon) return false;

	float3 pixelPos = TESR_CameraPosition.xyz + toWorld(uv) * depth;
	float3 pixelNormal = ViewToWorld(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0, 0)).xyz * 2.0 - 1.0);
	[branch] if (normalize(pixelNormal).z > 0.7 && abs(pixelPos.z - TESR_WaterSettings.x) < max(2.0, depth * 0.002)) return false;

	float2 px = TESR_ReciprocalResolution.xy, lowPx = px * LowScale;
	float2 p = uv / lowPx - 0.5;
	float2 f = frac(p);
	float2 at = (floor(p) + 0.5) * lowPx;
	float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	float4 s00 = tex2Dlod(TESR_SpecularBuffer, float4(at, 0, 0));
	float4 s10 = tex2Dlod(TESR_SpecularBuffer, float4(at + float2(lowPx.x, 0.0), 0, 0));
	float4 s01 = tex2Dlod(TESR_SpecularBuffer, float4(at + float2(0.0, lowPx.y), 0, 0));
	float4 s11 = tex2Dlod(TESR_SpecularBuffer, float4(at + lowPx, 0, 0));
	float4 off = abs(float4(s00.a, s10.a, s01.a, s11.a) - depth) / (0.05 * depth + 4.0);
	off *= off;
	w *= 1.0 / (1.0 + off * off) + 0.0001;
	float3 light = (s00.rgb * w.x + s10.rgb * w.y + s01.rgb * w.z + s11.rgb * w.w) / dot(w, 1.0);

	float shadow = tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0, 0)).r;
	float sunUp = 1.0 - TESR_ShadowFade.x;
	float3 sky = lerp(linearize(TESR_HorizonColor.rgb), linearize(TESR_SkyColor.rgb), light.g);
	lin = lin + light.r * sky + light.b * shadow * sunUp * linearize(TESR_SunColor.rgb);
	return true;
}

float4 SpecularPS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float3 lin = linearize(color.rgb);
	[branch] if (!AddSpecular(uv, readDepthLod(uv), lin)) return color;
	return float4(delinearize(lin), color.a);
}

static const float BounceNearFade = 40.0;

bool AddBounce(float2 uv, float depth, inout float3 lin) {
	[branch] if (depth < BounceNearFade || depth > NVR_BounceData.z) return false;
	float2 position = uv / NVR_BounceLayout.zw - 0.5;
	float2 base = floor(position);
	float2 fraction = position - base;
	float tolerance = max(depth * 0.03, 2.0);
	float3 sum = 0.0;
	float weights = 1.0e-5;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float4 tap = tex2Dlod(NVR_BounceBuffer, float4((base + float2(x, y) + 0.5) * NVR_BounceLayout.zw, 0.0, 0.0));
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1.0 + abs(tap.a - depth) / tolerance;
			sum += tap.rgb * weight;
			weights += weight;
		}
	}
	float3 indirect = sum / weights;
	float peak = max(max(lin.r, lin.g), max(lin.b, 0.0001));
	float3 albedo = lin / peak * 0.6;
	lin += NVR_BounceData.x * indirect * albedo;
	return true;
}

float4 SpecularBouncePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);
	float3 lin = linearize(color.rgb);
	bool bounced = AddBounce(uv, depth, lin);
	bool lit = AddSpecular(uv, depth, lin);
	[branch] if (!bounced && !lit) return color;
	return float4(delinearize(lin), color.a);
}

technique Specular {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SpecularPS();
	}
}

technique SpecularPrep {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SpecularPrepPS();
		Texture[6] = <NoTexture>;
	}
}

technique SpecularLow {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SpecularLowPS();
		Texture[5] = <NoTexture>;
	}
}

technique SpecularBounce {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SpecularBouncePS();
	}
}
