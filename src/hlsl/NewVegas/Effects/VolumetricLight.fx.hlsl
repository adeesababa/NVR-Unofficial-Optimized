
float4 TESR_ReciprocalResolution;
float4 TESR_SunColor;
float4 TESR_ShadowFade;
float4 TESR_VolumetricLightData;
float4 TESR_VolumetricLightData2;
float4 TESR_VolumetricLightData3;
float4 TESR_VolumetricLightLayout;
float4x4 TESR_VolumetricLightReproject;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ShadowAtlas : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightMarch : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightTemp : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightHistory : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightGrid : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightGridTemp : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
float4 TESR_VolumetricLightGridLayout;
float4 TESR_VolumetricLightGridData;
float4 TESR_FogData;
float4 TESR_SunAmount;
float4 TESR_GameTime;
float4 TESR_VolumetricFogDensity;
float4 TESR_VolumetricFogShape;
float4 TESR_VolumetricFogWind;
float4 TESR_VolumetricFogScatter;
float4 TESR_VolumetricFogWeather;
float4 TESR_VolumetricFogNight;
float4 TESR_VolumetricFogNightScatter;
sampler3D NVR_FogNoise : register(s9) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
texture NoTexture;

static const float Strength = TESR_VolumetricLightData.x;
static const float Darken = TESR_VolumetricLightData.y;
static const float Density = TESR_VolumetricLightData.z;
static const float MarchDistance = TESR_VolumetricLightData.w;
static const float Steps = TESR_VolumetricLightData2.x;
static const float Anisotropy = TESR_VolumetricLightData2.y;
static const float Fade = TESR_VolumetricLightData2.z;
static const float NoiseFrame = TESR_VolumetricLightData2.w;
static const float HistoryWeight = TESR_VolumetricLightData3.x;
static const float BlurRadius = TESR_VolumetricLightData3.y;
static const float2 LowTexel = TESR_VolumetricLightLayout.xy;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"

float GradientNoise(float2 pixel) {
	pixel += NoiseFrame * 5.588238;
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

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
	[branch] if (TESR_ShadowFormatData.x == 3.0) return ShadowPCF2x2(TESR_ShadowAtlas, lightSpace.xy * 0.5 + quadrant, lightSpace.z - 0.0002, TESR_ShadowBlur.x);
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
	float depth = readDepthLod(uv - 0.5 * (LowTexel - TESR_ReciprocalResolution.xy));
	float isSky = depth >= farZ * 0.996 ? 1.0 : 0.0;
	float planar = isSky > 0.0 ? MarchDistance : min(depth, MarchDistance);

	float3 ray = toWorld(uv);
	float sigma = Density * length(ray);
	float dt = planar / Steps;
	float offset = GradientNoise(vpos);

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

float HistoryAt(float2 position, float expected01, out float2 history) {
	position = position / LowTexel - 0.5;
	float2 base = floor(position);
	float2 f = position - base;
	float4 t00 = tex2Dlod(TESR_VolumetricLightHistory, float4((base + 0.5) * LowTexel, 0, 0));
	float4 t10 = tex2Dlod(TESR_VolumetricLightHistory, float4((base + float2(1.5, 0.5)) * LowTexel, 0, 0));
	float4 t01 = tex2Dlod(TESR_VolumetricLightHistory, float4((base + float2(0.5, 1.5)) * LowTexel, 0, 0));
	float4 t11 = tex2Dlod(TESR_VolumetricLightHistory, float4((base + 1.5) * LowTexel, 0, 0));
	float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	float tolerance = max(expected01 * 0.05, 0.0002);
	w *= abs(float4(t00.b, t10.b, t01.b, t11.b) - expected01) < tolerance;
	float sum = dot(w, 1.0);
	history = (t00.rg * w.x + t10.rg * w.y + t01.rg * w.z + t11.rg * w.w) / max(sum, 1.0e-4);
	return sum;
}

float4 TemporalPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * LowTexel;
	float4 current = tex2Dlod(TESR_VolumetricLightMarch, float4(uv, 0, 0));

	float tolerance = max(current.b * 0.05, 0.00002);
	float2 low = current.rg, high = current.rg;
	[unroll] for (int y = -1; y <= 1; y++) {
		[unroll] for (int x = -1; x <= 1; x++) {
			if (x == 0 && y == 0) continue;
			float4 tap = tex2Dlod(TESR_VolumetricLightMarch, float4(uv + float2(x, y) * LowTexel, 0, 0));
			if (abs(tap.b - current.b) < tolerance) { low = min(low, tap.rg); high = max(high, tap.rg); }
		}
	}

	float3 position = toWorld(uv) * (current.b * farZ);
	float4 clip = mul(float4(position, 1.0), TESR_VolumetricLightReproject);
	float w = max(clip.w, 0.001);
	float2 last = screenToRect(float2(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5));
	float2 history;
	float found = HistoryAt(last, clip.w / farZ, history);
	found *= all(last > 0.0) && all(last < TESR_RenderScale.xy);

	float2 margin = 0.1 * (high - low) + 1.0e-4;
	history = clamp(history, low - margin, high + margin);
	float weight = HistoryWeight * saturate(found);
	return float4(lerp(current.rg, history, weight), current.b, 1.0);
}

float4 BlurAxis(sampler2D source, float2 vpos, float2 axis) {
	float4 centre = tex2Dlod(source, float4((vpos + 0.5) * LowTexel, 0, 0));
	float tolerance = max(centre.b * 0.05, 0.00002);
	float sigma = max(BlurRadius * 0.5, 0.5);
	float falloff = -0.5 / (sigma * sigma);
	float2 sum = centre.rg;
	float weights = 1.0;
	[loop] for (int i = 1; i <= 8; i++) {
		if (i > BlurRadius) break;
		float gauss = exp(falloff * i * i);
		float4 a = tex2Dlod(source, float4(clampToRectIfOn((vpos + 0.5 + axis * i) * LowTexel, LowTexel), 0, 0));
		float4 b = tex2Dlod(source, float4(clampToRectIfOn((vpos + 0.5 - axis * i) * LowTexel, LowTexel), 0, 0));
		float wa = gauss / (1.0 + abs(a.b - centre.b) / tolerance);
		float wb = gauss / (1.0 + abs(b.b - centre.b) / tolerance);
		sum += a.rg * wa + b.rg * wb;
		weights += wa + wb;
	}
	return float4(sum / weights, centre.b, 1.0);
}

float4 BlurXPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 { return BlurAxis(TESR_VolumetricLightTemp, vpos, float2(1.0, 0.0)); }
float4 BlurYPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 { return BlurAxis(TESR_VolumetricLightMarch, vpos, float2(0.0, 1.0)); }

float4 CompositeAir(float2 uv, float4 color, float2 air) {
	float cosTheta = dot(normalize(toWorld(uv)), TESR_SmoothedSunDir.xyz);
	float g = Anisotropy;
	float phase = min((1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-4), 1.5), 8.0);

	float3 lin = linearize(max(color.rgb, 0.0));
	float shadedAir = saturate(air.g - air.r);
	lin = lin * (1.0 - Darken * Fade * shadedAir) + linearize(TESR_SunColor.rgb) * (Strength * Fade * phase * air.r);
	return float4(delinearize(max(lin, 0.0)), color.a);
}

float4 CompositePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);

	float2 position = uv / LowTexel - 0.5;
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
	return CompositeAir(uv, color, air);
}

static const float2 GridSize = TESR_VolumetricLightGridLayout.xy;
static const float2 GridAtlasTexel = TESR_VolumetricLightGridLayout.zw;

float GridDepth(float s) { s /= 64.0; return MarchDistance * s * s; }

float2 GridUV(float z, float2 local) {
	float2 tile = float2(fmod(z, 8.0), floor(z / 8.0));
	return (tile * GridSize + clamp(local, 0.5, GridSize - 0.5)) * GridAtlasTexel;
}

float FogHeightWeight(float height, float nightFactor) {
	float fogPower = lerp(1.0, TESR_FogData.w, saturate(TESR_VolumetricFogDensity.y));
	float heightFalloff = max(0.0001, TESR_VolumetricFogShape.x) * lerp(1.0, max(0.0, TESR_VolumetricFogNight.z), nightFactor);
	float falloff = 1.5e-4 / (fogPower * heightFalloff);
	return lerp(1.0, exp(clamp(-falloff * height, -20.0, 10.0)), saturate(TESR_VolumetricFogScatter.w));
}

float FogNoiseAt(float3 position, float nightFactor) {
	float windSpeed = TESR_VolumetricFogWind.z * lerp(1.0, max(0.0, TESR_VolumetricFogNightScatter.y), nightFactor);
	float3 windOffset = float3(TESR_VolumetricFogWind.xy * windSpeed * TESR_GameTime.x * 0.002, 0.0);
	float3 noisePos = (TESR_CameraPosition.xyz + position + windOffset) / (1500.0 * max(0.0001, TESR_VolumetricFogWind.w));
	return tex3Dlod(NVR_FogNoise, float4(noisePos / 4.0, 0.0)).g;
}

float4 GridLightPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 tile = floor(vpos / GridSize);
	float2 cell = vpos - tile * GridSize;
	float z = tile.y * 8.0 + tile.x;
	float near = GridDepth(z), far = GridDepth(z + 1.0);
	float3 positionA = toWorld((cell + 0.25) / GridSize) * lerp(near, far, 0.25);
	float3 positionB = toWorld((cell + 0.75) / GridSize) * lerp(near, far, 0.75);
	float shadowA = MarchShadow(positionA), shadowB = MarchShadow(positionB);
	float sigma = Density * length(toWorld((cell + 0.5) / GridSize));
	float4 cellValue = float4(0.5 * (shadowA + shadowB) * (exp(-sigma * near) - exp(-sigma * far)) * TESR_VolumetricLightGridData.y, 0.0, 0.0, 0.0);
	[branch] if (TESR_VolumetricLightGridData.x > 0.5) {
		float nightFactor = TESR_VolumetricFogWeather.y * (1.0 - smoothstep(0.4, 0.8, TESR_SunAmount.x));
		float share = 0.5 * (far - near) / MarchDistance;
		float hA = FogHeightWeight(positionA.z, nightFactor), hB = FogHeightWeight(positionB.z, nightFactor);
		float nA = FogNoiseAt(positionA, nightFactor), nB = FogNoiseAt(positionB, nightFactor);
		cellValue.gba = share * float3(hA + hB, hA * nA + hB * nB, hA * shadowA + hB * shadowB);
	}
	return cellValue;
}

float4 GridRowPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 tile = floor(vpos / GridSize);
	float2 cell = vpos - tile * GridSize + 0.5;
	float4 sum = 0.0;
	[unroll] for (int c = 0; c < 8; c++)
		if (c <= tile.x) sum += tex2Dlod(TESR_VolumetricLightGrid, float4(GridUV(tile.y * 8.0 + c, cell), 0, 0));
	return sum;
}

float4 GridCarryPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 tile = floor(vpos / GridSize);
	float2 cell = vpos - tile * GridSize + 0.5;
	float4 sum = tex2Dlod(TESR_VolumetricLightGridTemp, float4(GridUV(tile.y * 8.0 + tile.x, cell), 0, 0));
	[unroll] for (int r = 0; r < 7; r++)
		if (r < tile.y) sum += tex2Dlod(TESR_VolumetricLightGridTemp, float4(GridUV(r * 8.0 + 7.0, cell), 0, 0));
	return sum;
}

float4 CompositeGridPS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);
	float planar = depth >= farZ * 0.996 ? MarchDistance : min(depth, MarchDistance);

	float s = sqrt(planar / MarchDistance) * 64.0;
	float z = min(floor(s), 63.0);
	float2 local = uv * GridSize;
	float farSide = tex2Dlod(TESR_VolumetricLightGrid, float4(GridUV(z, local), 0, 0)).r;
	float nearSide = z > 0.5 ? tex2Dlod(TESR_VolumetricLightGrid, float4(GridUV(z - 1.0, local), 0, 0)).r : 0.0;
	float sigma = Density * length(toWorld(uv));
	float2 air = float2(lerp(nearSide, farSide, saturate(s - z)) / TESR_VolumetricLightGridData.y, 1.0 - exp(-sigma * planar));
	return CompositeAir(uv, color, air);
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
		Texture[5] = <NoTexture>;
	}
}

technique BlurX {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurXPS();
		Texture[3] = <NoTexture>;
	}
}

technique Temporal {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 TemporalPS();
		Texture[5] = <NoTexture>;
	}
}

technique BlurY {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurYPS();
		Texture[4] = <NoTexture>;
	}
}

technique GridLight {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GridLightPS();
		Texture[7] = <NoTexture>;
	}
}

technique GridRow {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GridRowPS();
		Texture[8] = <NoTexture>;
	}
}

technique GridCarry {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GridCarryPS();
		Texture[7] = <NoTexture>;
	}
}

technique CompositeGrid {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CompositeGridPS();
	}
}
