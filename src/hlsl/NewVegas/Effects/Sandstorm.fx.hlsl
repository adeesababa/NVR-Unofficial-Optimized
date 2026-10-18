float4 TESR_ReciprocalResolution;
float4 TESR_FogColor;
float4 TESR_SunColor;
float4 TESR_SunAmbient;
float4 TESR_SunPosition;
float4 TESR_SunAmount;
float4 TESR_PBRData;
float4 TESR_SandstormData;
float4 TESR_SandstormWind;
float4 TESR_SandstormShape;
float4 TESR_SandstormColor;
float4 TESR_SandstormCover;
float4 TESR_SandstormLights;
float4 TESR_SandstormEmissive;
float4 TESR_SandstormWater;
float4 TESR_SandstormQuality;
float4 TESR_LightPosition[12];
float4 TESR_ShadowLightPosition[12];
float4 TESR_LightColor[24];
float4 TESR_ShadowFade;
float4x4 TESR_ShadowCameraToLightTransformOrtho;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_OrthoMapBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ShadowAtlas : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_SandstormGrid : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_SandstormGridTemp : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SandstormStreaks : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_SandstormGlow : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
float4 TESR_SandstormGridLayout;
float4 TESR_SandstormLowLayout;
texture NoTexture;

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Shadows.hlsl"
#include "Includes/SunCascades.hlsl"

static const int MaxSteps = 12;

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

float hash13(float3 p)
{
	p = frac(p * 0.1031f);
	p += dot(p, p.zyx + 31.32f);
	return frac((p.x + p.y) * p.z);
}

float valueNoise(float3 x)
{
	float3 i = floor(x);
	float3 f = frac(x);
	f = f * f * (3.0f - 2.0f * f);
	return lerp(lerp(lerp(hash13(i), hash13(i + float3(1, 0, 0)), f.x),
	                 lerp(hash13(i + float3(0, 1, 0)), hash13(i + float3(1, 1, 0)), f.x), f.y),
	            lerp(lerp(hash13(i + float3(0, 0, 1)), hash13(i + float3(1, 0, 1)), f.x),
	                 lerp(hash13(i + float3(0, 1, 1)), hash13(i + float3(1, 1, 1)), f.x), f.y), f.z);
}

float fbm(float3 p)
{
	float sum = valueNoise(p) * 0.55f;
	float weight = 0.55f;
	[branch] if (TESR_SandstormQuality.y > 1.5f) {
		sum += valueNoise(p * 2.03f + 17.1f) * 0.3f;
		weight += 0.3f;
		[branch] if (TESR_SandstormQuality.y > 2.5f) {
			sum += valueNoise(p * 4.01f + 41.7f) * 0.15f;
			weight += 0.15f;
		}
	}
	return sum / weight;
}

float phase(float cosTheta, float g)
{
	float g2 = g * g;
	return (1.0f - g2) / (4.0f * 3.14159265f * pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f));
}

float SunCascadeShadow(float3 position)
{
	float4x4 lightTransform;
	float2 quadrant;
	float bleed;
	[branch] if (length(position - TESR_ShadowNearCenter.xyz) < TESR_ShadowNearCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformNear; quadrant = float2(0.0f, 0.0f); bleed = 0.1f;
	}
	else if (length(position - TESR_ShadowMiddleCenter.xyz) < TESR_ShadowMiddleCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformMiddle; quadrant = float2(0.5f, 0.0f); bleed = 0.2f;
	}
	else if (length(position - TESR_ShadowFarCenter.xyz) < TESR_ShadowFarCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformFar; quadrant = float2(0.0f, 0.5f); bleed = 0.6f;
	}
	else if (length(position - TESR_ShadowLodCenter.xyz) < TESR_ShadowLodCenter.w) {
		lightTransform = TESR_ShadowCameraToLightTransformLod; quadrant = float2(0.5f, 0.5f); bleed = 0.8f;
	}
	else return 1.0f;
	float4 lightSpace = SunCascadeTexCoord(mul(float4(position, 1.0f), lightTransform));
	[branch] if (TESR_ShadowFormatData.x == 3.0f) return ShadowPCF2x2(TESR_ShadowAtlas, lightSpace.xy * 0.5f + quadrant, lightSpace.z - 0.0002f, TESR_ShadowBlur.x);
	float4 moments = tex2Dlod(TESR_ShadowAtlas, float4(lightSpace.xy * 0.5f + quadrant, 0.0f, 0.0f));
	float bias = TESR_ShadowFormatData.x == 0.0f ? 0.00001f : 0.01f;
	if (TESR_ShadowFormatData.x == 0.0f) return GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleed);
	if (TESR_ShadowFormatData.x == 1.0f) return GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleed, TESR_ShadowFormatData.y);
	float2 exponents = GetEVSMExponents(40.0f, 5.0f, TESR_ShadowFormatData.y);
	float warped = WarpDepth(lightSpace.z, exponents).x;
	float depthScale = bias * exponents.x * warped;
	return ChebyshevUpperBound(moments.xz, warped, depthScale * depthScale, bleed);
}

float SunVisibility(float3 relative)
{
	[branch] if (TESR_ShadowFade.y < 0.5f || TESR_ShadowFade.x >= 0.999f) return 1.0f;
	return lerp(saturate(SunCascadeShadow(relative)), 1.0f, saturate(TESR_ShadowFade.x));
}


float OpenAbove(float3 relative)
{
	[branch] if (TESR_SandstormCover.x < 0.5f) return 1.0f;
	float4 ortho = mul(float4(relative, 1.0f), TESR_ShadowCameraToLightTransformOrtho);
	ortho.xyz /= ortho.w;
	if (abs(ortho.x) > 1.0f || abs(ortho.y) > 1.0f || ortho.z < 0.0f || ortho.z > 1.0f) return 1.0f;
	float top = tex2Dlod(TESR_OrthoMapBuffer, float4(ortho.x * 0.5f + 0.5f, ortho.y * -0.5f + 0.5f, 0.0f, 0.0f)).r;
	return top > ortho.z - 0.0001f;
}

static const float LeeReach[8] = { 50.0f, 110.0f, 190.0f, 290.0f, 410.0f, 550.0f, 710.0f, 900.0f };

float InTheWind(float3 relative, float2 wind)
{
	float reached = 1.0f;
	[unroll] for (int k = 0; k < 8; k++)
		reached *= OpenAbove(relative + float3(-wind * LeeReach[k], LeeReach[k] * 0.2f));
	return reached;
}

float InTheWindCoarse(float3 relative, float2 wind)
{
	float reached = 1.0f;
	[unroll] for (int k = 1; k < 8; k += 2)
		reached *= OpenAbove(relative + float3(-wind * LeeReach[k], LeeReach[k] * 0.2f));
	return reached;
}


float ViewDistance(float2 coord, float visibility)
{
	float depth = readDepthLod(coord);
	if (depth / farZ >= 0.9961f) return visibility * 6.0f;
	return length(toWorld(coord) * depth);
}

float3 PointGlow(float4 light, float4 color, float3 camera, float3 dir, float distance, float density) {
    [branch] if (light.w <= 0.0f || color.w <= 0.0f) return 0.0f;
    float3 toLight = light.xyz - camera;
    float along = dot(toLight, dir);
    float across = sqrt(max(dot(toLight, toLight) - along * along, 0.0f) + light.w * light.w * 0.01f);
    float swept = (atan((distance - along) / across) + atan(along / across)) / across;
    float through = exp(-density * clamp(along, 0.0f, distance));
    return linearize(color.rgb) * color.w * light.w * light.w * swept * through;
}


float4 SandstormPS(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 color = linearize(tex2D(TESR_RenderedBuffer, uv));

	float intensity = TESR_SandstormData.x;
	float density = TESR_SandstormData.y * intensity;
	float visibility = TESR_SandstormData.z;
	float heightFalloff = TESR_SandstormData.w;
	float time = TESR_SandstormWind.w;
	float2 wind = TESR_SandstormWind.xy;
	float speed = TESR_SandstormWind.z;
	float billowing = TESR_SandstormShape.x;
	float noiseScale = TESR_SandstormShape.y;

	float3 dir = normalize(toWorld(uv));

	float fullDensity = max(TESR_SandstormData.y, 1e-6f);
	float2 texel = TESR_ReciprocalResolution.xy * 2.0f;
	float distance;
	[branch] if (TESR_SandstormQuality.z > 0.5f) {
		float shown = 0.0f;
		[unroll] for (int y = -1; y <= 1; y++)
			[unroll] for (int x = -1; x <= 1; x++)
				shown += (2 - abs(x)) * (2 - abs(y)) * exp(-ViewDistance(uv + float2(x, y) * texel, visibility) * fullDensity);
		distance = min(-log(max(shown / 16.0f, 1e-9f)) / fullDensity, visibility * 6.0f);
	}
	else distance = ViewDistance(uv, visibility);

	float3 camera = TESR_CameraPosition.xyz;

	float start = 0.0f;
	float surfaceDetail = 1.0f;
	[branch] if (TESR_SandstormWater.x > 0.0f) {
		start = dir.z > 1e-3f ? TESR_SandstormWater.y / dir.z : 1e9f;
		[branch] if (start < 1e8f && abs(distance - start) <= max(64.0f, start * 0.05f)) {
			distance = start + visibility * 6.0f;
			float2 spread = TESR_ReciprocalResolution.xy * 3.0f;
			const float3 toLuma = float3(0.2126f, 0.7152f, 0.0722f);
			float around = dot(linearize(tex2Dlod(TESR_RenderedBuffer, float4(uv + float2(spread.x, 0.0f), 0.0f, 0.0f))).rgb, toLuma)
				+ dot(linearize(tex2Dlod(TESR_RenderedBuffer, float4(uv - float2(spread.x, 0.0f), 0.0f, 0.0f))).rgb, toLuma)
				+ dot(linearize(tex2Dlod(TESR_RenderedBuffer, float4(uv + float2(0.0f, spread.y), 0.0f, 0.0f))).rgb, toLuma)
				+ dot(linearize(tex2Dlod(TESR_RenderedBuffer, float4(uv - float2(0.0f, spread.y), 0.0f, 0.0f))).rgb, toLuma);
			float difference = dot(color.rgb, toLuma) / max(around * 0.25f, 1e-4f) - 1.0f;
			surfaceDetail = 1.0f + 0.8f * difference * (1.0f - smoothstep(0.12f, 0.3f, abs(difference)));
		}
		[branch] if (start >= distance) return delinearize(color);
		distance -= start;
		camera += dir * start;
	}

	float daytime = smoothstep(0.1f, 0.6f, TESR_SunAmount.x);
	float3 ambient = (linearize(TESR_FogColor.rgb) * 0.6f + linearize(TESR_SunAmbient.rgb) * 0.4f) * TESR_PBRData.w;
	float3 sunLight = linearize(TESR_SunColor.rgb) * TESR_PBRData.z * daytime;
	float sunPhase = phase(dot(dir, normalize(TESR_SunPosition.xyz)), 0.55f) * 4.0f * 3.14159265f;
	float3 dustAmbient = TESR_SandstormColor.rgb * ambient * TESR_SandstormColor.w;
	float3 dustSun = TESR_SandstormColor.rgb * sunLight * lerp(0.35f, sunPhase, saturate(TESR_SandstormShape.w * 0.5f)) * max(TESR_SandstormShape.w, 0.25f)
		* TESR_SandstormColor.w;
	float3 dustLight = dustAmbient + dustSun;

	float3 drift = float3(wind * speed * time, -speed * 0.05f * time);
	float jitter = frac(52.9829189f * frac(dot(uv / TESR_ReciprocalResolution.xy, float2(0.06711056f, 0.00583715f))) + time * 7.123f);

	float marchEnd = min(distance, visibility * 1.5f);
	float transmittance = 1.0f;
	float3 scattered = 0.0f;
	float nearCut = TESR_SandstormEmissive.y;
	float nearTransmittance = 1.0f;
	float3 nearScattered = 0.0f;
	const float Steps = TESR_SandstormQuality.x;
	[loop] for (int i = 0; i < MaxSteps; i++) {
		[branch] if (i >= Steps) continue;
		float t0 = marchEnd * pow((i + jitter) / Steps, 2.0f);
		float t1 = marchEnd * pow(min(i + 1 + jitter, Steps) / Steps, 2.0f);
		float3 p = camera + dir * (0.5f * (t0 + t1));
		float height = exp(-max(p.z - camera.z + 200.0f, 0.0f) / heightFalloff);
		float clouds = fbm((p - drift) / noiseScale);
		float local = density * height * lerp(1.0f, saturate(clouds * 2.2f - 0.35f) * 1.8f, billowing) * (t1 - t0);
		float middle = 0.5f * (t0 + t1);
		[branch] if (start + middle < 500.0f) local *= lerp(lerp(0.35f, 1.0f, InTheWindCoarse(dir * (start + middle), wind)), 1.0f, smoothstep(350.0f, 500.0f, start + middle));
		float absorbed = 1.0f - exp(-local);
		float3 light = dustAmbient + dustSun * SunVisibility(dir * (start + middle));
		scattered += transmittance * absorbed * light * (0.8f + 0.4f * clouds);
		transmittance *= 1.0f - absorbed;
		float nearShare = saturate((nearCut - t0) / max(t1 - t0, 1e-3f));
		float nearAbsorbed = 1.0f - exp(-local * nearShare);
		nearScattered += nearTransmittance * nearAbsorbed * light * (0.8f + 0.4f * clouds);
		nearTransmittance *= 1.0f - nearAbsorbed;
	}

	float endHeight = exp(-max(dir.z * marchEnd + 200.0f, 0.0f) / heightFalloff);
	float haze = 1.0f - exp(-max(distance - marchEnd, 0.0f) * density * lerp(endHeight, 1.0f, 0.5f));
	scattered += transmittance * haze * dustLight;
	transmittance *= 1.0f - haze;
	float3 dustScattered = scattered;

	float3 streaks = 0.0f;
	float2 across = float2(-wind.y, wind.x);
	[branch] if (TESR_SandstormShape.z > 0.0f)
	[unroll] for (int s = 0; s < 3 && start <= 0.0f; s++) {
		float t = 50.0f + 120.0f * s + 30.0f * jitter;
		if (t < distance) {
			float3 p = camera + dir * t - float3(wind * speed * time * (2.5f + s), 0.0f);
			float3 q = float3(dot(p.xy, wind) / 240.0f, dot(p.xy, across) / 8.0f, p.z / 6.0f);
			float sheltered;
			[branch] if (TESR_SandstormQuality.w > 0.5f) sheltered = InTheWind(dir * t, wind);
			else sheltered = InTheWindCoarse(dir * t, wind);
			float visible = saturate((distance - t) / 60.0f) * sheltered;
			float grain = smoothstep(0.74f, 0.96f, valueNoise(q + s * 31.7f)) * (1.0f - s * 0.25f) * visible;
			[branch] if (grain > 0.0f) streaks += grain * (dustAmbient + dustSun * SunVisibility(dir * t));
		}
	}
	scattered += streaks * TESR_SandstormShape.z * intensity * 0.25f;

	[branch] if (TESR_SandstormLights.x > 0.0f && start <= 0.0f) {
		float3 glow = 0.0f;
		[loop] for (int l = 0; l < 12; l++) {
			glow += PointGlow(TESR_ShadowLightPosition[l], TESR_LightColor[l], camera, dir, distance, density);
			glow += PointGlow(TESR_LightPosition[l], TESR_LightColor[12 + l], camera, dir, distance, density);
		}
		scattered += glow * density * TESR_SandstormColor.rgb * TESR_SandstormLights.x * 0.01f;
	}

	float3 result = color.rgb * transmittance + scattered;

	float rawDepth = readDepth(uv);
	bool skyBehind = rawDepth / farZ >= 0.9961f;
	float emissive = 0.0f;
	[branch] if (TESR_SandstormEmissive.x > 0.0f) {
		float dustLuma = max(dot(dustLight, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f) * TESR_SandstormEmissive.x * (skyBehind ? 2.0f : 1.0f);
		emissive = saturate((dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f)) - dustLuma) / dustLuma);
		float3 nearResult = color.rgb * nearTransmittance + nearScattered + (scattered - dustScattered);
		result = lerp(result, nearResult, emissive);
	}
	result *= surfaceDetail;
	return delinearize(float4(result, color.a));
}


#ifndef SANDFAST_STREAK_SAMPLES
#define SANDFAST_STREAK_SAMPLES 4
#endif

static const float2 GridSize = TESR_SandstormGridLayout.xy;
static const float2 GridAtlasTexel = TESR_SandstormGridLayout.zw;
static const float2 LowTexel = TESR_SandstormLowLayout.xy;
static const float2 QuarterTexel = TESR_SandstormLowLayout.zw;

float GridEnd() { return TESR_SandstormData.z * 1.5f; }
float GridDistance(float s) { s /= 64.0f; return GridEnd() * s * s; }

float2 GridUV(float z, float2 local) {
	float2 tile = float2(fmod(z, 8.0f), floor(z / 8.0f));
	return (tile * GridSize + clamp(local, 0.5f, GridSize - 0.5f)) * GridAtlasTexel;
}

float OpenAboveAt(float4 ortho) {
	ortho.xyz /= ortho.w;
	float top = tex2Dlod(TESR_OrthoMapBuffer, float4(ortho.x * 0.5f + 0.5f, ortho.y * -0.5f + 0.5f, 0.0f, 0.0f)).r;
	bool outside = abs(ortho.x) > 1.0f || abs(ortho.y) > 1.0f || ortho.z < 0.0f || ortho.z > 1.0f;
	return (outside || top > ortho.z - 0.0001f) ? 1.0f : 0.0f;
}

float InTheWindFast(float3 relative, float2 wind, bool fine) {
	[branch] if (TESR_SandstormCover.x < 0.5f) return 1.0f;
	float4 base = mul(float4(relative, 1.0f), TESR_ShadowCameraToLightTransformOrtho);
	float4 step = mul(float4(-wind, 0.2f, 0.0f), TESR_ShadowCameraToLightTransformOrtho);
	float reached = 1.0f;
	[branch] if (fine) {
		[unroll] for (int k = 0; k < 8; k++) reached *= OpenAboveAt(base + step * LeeReach[k]);
	}
	else {
		[unroll] for (int k = 1; k < 8; k += 2) reached *= OpenAboveAt(base + step * LeeReach[k]);
	}
	return reached;
}

void DustLighting(float3 dir, out float3 dustAmbient, out float3 dustSun) {
	float daytime = smoothstep(0.1f, 0.6f, TESR_SunAmount.x);
	float3 ambient = (linearize(TESR_FogColor.rgb) * 0.6f + linearize(TESR_SunAmbient.rgb) * 0.4f) * TESR_PBRData.w;
	float3 sunLight = linearize(TESR_SunColor.rgb) * TESR_PBRData.z * daytime;
	float sunPhase = phase(dot(dir, normalize(TESR_SunPosition.xyz)), 0.55f) * 4.0f * 3.14159265f;
	dustAmbient = TESR_SandstormColor.rgb * ambient * TESR_SandstormColor.w;
	dustSun = TESR_SandstormColor.rgb * sunLight * lerp(0.35f, sunPhase, saturate(TESR_SandstormShape.w * 0.5f)) * max(TESR_SandstormShape.w, 0.25f)
		* TESR_SandstormColor.w;
}

float4 GridDustPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 tile = floor(vpos / GridSize);
	float2 cell = vpos - tile * GridSize;
	float z = tile.y * 8.0f + tile.x;
	float t0 = GridDistance(z), t1 = GridDistance(z + 1.0f);
	float3 dir = normalize(toWorld((cell + 0.5f) / GridSize));

	float intensity = TESR_SandstormData.x;
	float density = TESR_SandstormData.y * intensity;
	float heightFalloff = TESR_SandstormData.w;
	float time = TESR_SandstormWind.w;
	float2 wind = TESR_SandstormWind.xy;
	float speed = TESR_SandstormWind.z;
	float3 camera = TESR_CameraPosition.xyz;
	float3 dustAmbient, dustSun;
	DustLighting(dir, dustAmbient, dustSun);

	float3 drift = float3(wind * speed * time, -speed * 0.05f * time);
	float middle = 0.5f * (t0 + t1);
	float3 p = camera + dir * middle;
	float height = exp(-max(p.z - camera.z + 200.0f, 0.0f) / heightFalloff);
	float clouds = fbm((p - drift) / TESR_SandstormShape.y);
	float local = density * height * lerp(1.0f, saturate(clouds * 2.2f - 0.35f) * 1.8f, TESR_SandstormShape.x) * (t1 - t0);
	float2 spread = 0.25f / GridSize;
	float2 centre = (cell + 0.5f) / GridSize;
	float3 sub[4] = { normalize(toWorld(centre + float2(-spread.x, -spread.y))) * lerp(t0, t1, 0.125f),
	                  normalize(toWorld(centre + float2(spread.x, -spread.y))) * lerp(t0, t1, 0.625f),
	                  normalize(toWorld(centre + float2(-spread.x, spread.y))) * lerp(t0, t1, 0.875f),
	                  normalize(toWorld(centre + float2(spread.x, spread.y))) * lerp(t0, t1, 0.375f) };
	[branch] if (middle < 500.0f) {
		float open = 0.25f * (InTheWindFast(sub[0], wind, false) + InTheWindFast(sub[1], wind, false) + InTheWindFast(sub[2], wind, false) + InTheWindFast(sub[3], wind, false));
		local *= lerp(lerp(0.35f, 1.0f, open), 1.0f, smoothstep(350.0f, 500.0f, middle));
	}
	float absorbed = 1.0f - exp(-local);
	float sun = 0.25f * (SunVisibility(sub[0]) + SunVisibility(sub[1]) + SunVisibility(sub[2]) + SunVisibility(sub[3]));
	float3 light = dustAmbient + dustSun * sun;
	return float4(absorbed * light * (0.8f + 0.4f * clouds), local);
}

float4 Over(float4 front, float4 back) { return float4(front.rgb + exp(-front.a) * back.rgb, front.a + back.a); }

float4 GridRowPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 tile = floor(vpos / GridSize);
	float2 cell = vpos - tile * GridSize + 0.5f;
	float4 sum = 0.0f;
	[unroll] for (int c = 0; c < 8; c++)
		if (c <= tile.x) sum = Over(sum, tex2Dlod(TESR_SandstormGrid, float4(GridUV(tile.y * 8.0f + c, cell), 0.0f, 0.0f)));
	return sum;
}

float4 GridCarryPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 tile = floor(vpos / GridSize);
	float2 cell = vpos - tile * GridSize + 0.5f;
	float4 sum = 0.0f;
	[unroll] for (int r = 0; r < 7; r++)
		if (r < tile.y) sum = Over(sum, tex2Dlod(TESR_SandstormGridTemp, float4(GridUV(r * 8.0f + 7.0f, cell), 0.0f, 0.0f)));
	return Over(sum, tex2Dlod(TESR_SandstormGridTemp, float4(GridUV(tile.y * 8.0f + tile.x, cell), 0.0f, 0.0f)));
}

float4 DustAt(float2 uv, float d) {
	float s = sqrt(saturate(d / GridEnd())) * 64.0f;
	float z = min(floor(s), 63.0f);
	float2 local = uv * GridSize;
	float4 farSide = tex2Dlod(TESR_SandstormGrid, float4(GridUV(z, local), 0.0f, 0.0f));
	float4 nearSide = z > 0.5f ? tex2Dlod(TESR_SandstormGrid, float4(GridUV(z - 1.0f, local), 0.0f, 0.0f)) : 0.0f;
	return lerp(nearSide, farSide, saturate(s - z));
}

float3 PointGlowFast(float4 light, float4 color, float3 camera, float3 dir, float distance, float density) {
	[branch] if (light.w <= 0.0f || color.w <= 0.0f) return 0.0f;
	float3 toLight = light.xyz - camera;
	float along = dot(toLight, dir);
	float across2 = max(dot(toLight, toLight) - along * along, 0.0f) + light.w * light.w * 0.01f;
	float across = sqrt(across2);
	float swept = atan2(distance * across, across2 - (distance - along) * along) / across;
	float through = exp(-density * clamp(along, 0.0f, distance));
	return linearize(color.rgb) * color.w * light.w * light.w * swept * through;
}

float4 StreaksPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5f) * LowTexel;
	float3 dir = normalize(toWorld(uv));
	float time = TESR_SandstormWind.w;
	float2 wind = TESR_SandstormWind.xy;
	float speed = TESR_SandstormWind.z;
	float3 camera = TESR_CameraPosition.xyz;

	float3 grains = 0.0f;
	float sunlit = 0.0f;
	[branch] if (TESR_SandstormShape.z > 0.0f) {
		float2 group = vpos - 2.0f * floor(vpos * 0.5f);
		float jitter = frac((group.x + 2.0f * group.y + 0.5f) / (4.0f * SANDFAST_STREAK_SAMPLES) + time * 7.123f);
		float2 across = float2(-wind.y, wind.x);
		const bool fine = TESR_SandstormQuality.w > 0.5f;
		[unroll] for (int s = 0; s < 3; s++) {
			float grain = 0.0f;
			[unroll] for (int m = 0; m < SANDFAST_STREAK_SAMPLES; m++) {
				float t = 50.0f + 120.0f * s + 30.0f * frac(jitter + (float)m / SANDFAST_STREAK_SAMPLES);
				float3 p = camera + dir * t - float3(wind * speed * time * (2.5f + s), 0.0f);
				float3 q = float3(dot(p.xy, wind) / 240.0f, dot(p.xy, across) / 8.0f, p.z / 6.0f);
				grain += smoothstep(0.74f, 0.96f, valueNoise(q + s * 31.7f));
			}
			grain *= (1.0f - s * 0.25f) / SANDFAST_STREAK_SAMPLES;
			float tMiddle = 65.0f + 120.0f * s;
			[branch] if (grain > 0.0f) {
				grain *= InTheWindFast(dir * tMiddle, wind, fine);
				[branch] if (grain > 0.0f) sunlit += grain * SunVisibility(dir * tMiddle);
			}
			grains[s] = grain;
		}
	}
	float total = grains.x + grains.y + grains.z;
	return float4(grains, total > 0.0f ? sunlit / total : 1.0f);
}

float4 GlowPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5f) * QuarterTexel - 0.5f * QuarterTexel + 1.5f * TESR_ReciprocalResolution.xy;
	float depth = readDepthLod(uv);
	float visibility = TESR_SandstormData.z;
	float distance = depth / farZ >= 0.9961f ? visibility * 6.0f : length(toWorld(uv) * depth);
	float3 dir = normalize(toWorld(uv));
	float density = TESR_SandstormData.y * TESR_SandstormData.x;
	float3 camera = TESR_CameraPosition.xyz;
	float3 glow = 0.0f;
	[branch] if (TESR_SandstormLights.x > 0.0f) {
		[loop] for (int l = 0; l < 12; l++) {
			glow += PointGlowFast(TESR_ShadowLightPosition[l], TESR_LightColor[l], camera, dir, distance, density);
			glow += PointGlowFast(TESR_LightPosition[l], TESR_LightColor[12 + l], camera, dir, distance, density);
		}
		glow *= density * TESR_SandstormColor.rgb * TESR_SandstormLights.x * 0.01f;
	}
	return float4(glow, depth / farZ);
}

float3 GlowAt(float2 uv, float depth01) {
	float2 position = uv / QuarterTexel - 0.5f;
	float2 base = floor(position);
	float2 fraction = position - base;
	float tolerance = max(depth01 * 0.04f, 0.00002f);
	float3 glow = 0.0f;
	float weights = 1.0e-5f;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float4 tap = tex2Dlod(TESR_SandstormGlow, float4((base + float2(x, y) + 0.5f) * QuarterTexel, 0.0f, 0.0f));
			float weight = (x ? fraction.x : 1.0f - fraction.x) * (y ? fraction.y : 1.0f - fraction.y);
			weight /= 1.0f + abs(tap.a - depth01) / tolerance;
			glow += tap.rgb * weight;
			weights += weight;
		}
	}
	return glow / weights;
}

float4 CompositePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = linearize(tex2D(TESR_RenderedBuffer, uv));

	float intensity = TESR_SandstormData.x;
	float density = TESR_SandstormData.y * intensity;
	float visibility = TESR_SandstormData.z;
	float heightFalloff = TESR_SandstormData.w;
	float time = TESR_SandstormWind.w;
	float3 dir = normalize(toWorld(uv));

	float fullDensity = max(TESR_SandstormData.y, 1e-6f);
	float2 texel = TESR_ReciprocalResolution.xy * 2.0f;
	float distance;
	[branch] if (TESR_SandstormQuality.z > 0.5f) {
		float shown = 0.0f;
		[unroll] for (int y = -1; y <= 1; y++)
			[unroll] for (int x = -1; x <= 1; x++)
				shown += (2 - abs(x)) * (2 - abs(y)) * exp(-ViewDistance(uv + float2(x, y) * texel, visibility) * fullDensity);
		distance = min(-log(max(shown / 16.0f, 1e-9f)) / fullDensity, visibility * 6.0f);
	}
	else distance = ViewDistance(uv, visibility);

	float3 dustAmbient, dustSun;
	DustLighting(dir, dustAmbient, dustSun);
	float3 dustLight = dustAmbient + dustSun;

	float marchEnd = min(distance, visibility * 1.5f);
	float4 dust = DustAt(uv, marchEnd);
	float transmittance = exp(-dust.a);
	float3 scattered = dust.rgb;

	float endHeight = exp(-max(dir.z * marchEnd + 200.0f, 0.0f) / heightFalloff);
	float haze = 1.0f - exp(-max(distance - marchEnd, 0.0f) * density * lerp(endHeight, 1.0f, 0.5f));
	scattered += transmittance * haze * dustLight;
	transmittance *= 1.0f - haze;
	float3 dustScattered = scattered;

	[branch] if (TESR_SandstormShape.z > 0.0f) {
		float jitter = frac(52.9829189f * frac(dot(uv / TESR_ReciprocalResolution.xy, float2(0.06711056f, 0.00583715f))) + time * 7.123f);
		float4 grains = 0.25f * (tex2Dlod(TESR_SandstormStreaks, float4(uv + LowTexel * float2(-0.5f, -0.5f), 0.0f, 0.0f))
			+ tex2Dlod(TESR_SandstormStreaks, float4(uv + LowTexel * float2(0.5f, -0.5f), 0.0f, 0.0f))
			+ tex2Dlod(TESR_SandstormStreaks, float4(uv + LowTexel * float2(-0.5f, 0.5f), 0.0f, 0.0f))
			+ tex2Dlod(TESR_SandstormStreaks, float4(uv + LowTexel * float2(0.5f, 0.5f), 0.0f, 0.0f)));
		float3 visible = saturate((distance - (float3(50.0f, 170.0f, 290.0f) + 30.0f * jitter)) / 60.0f);
		scattered += dot(grains.rgb, visible) * (dustAmbient + dustSun * grains.a) * TESR_SandstormShape.z * intensity * 0.25f;
	}

	float rawDepth = readDepth(uv);
	[branch] if (TESR_SandstormLights.x > 0.0f) scattered += GlowAt(uv, rawDepth / farZ);

	float3 result = color.rgb * transmittance + scattered;

	bool skyBehind = rawDepth / farZ >= 0.9961f;
	[branch] if (TESR_SandstormEmissive.x > 0.0f) {
		float dustLuma = max(dot(dustLight, float3(0.2126f, 0.7152f, 0.0722f)), 1e-4f) * TESR_SandstormEmissive.x * (skyBehind ? 2.0f : 1.0f);
		float emissive = saturate((dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f)) - dustLuma) / dustLuma);
		[branch] if (emissive > 0.0f) {
			float4 near = DustAt(uv, min(TESR_SandstormEmissive.y, marchEnd));
			float3 nearResult = color.rgb * exp(-near.a) + near.rgb + (scattered - dustScattered);
			result = lerp(result, nearResult, emissive);
		}
	}
	return delinearize(float4(result, color.a));
}

technique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SandstormPS();
	}
}

technique GridDust
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GridDustPS();
		Texture[4] = <NoTexture>;
	}
}

technique GridRow
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GridRowPS();
		Texture[5] = <NoTexture>;
	}
}

technique GridCarry
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GridCarryPS();
		Texture[4] = <NoTexture>;
	}
}

technique Streaks
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 StreaksPS();
		Texture[6] = <NoTexture>;
	}
}

technique Composite
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CompositePS();
	}
}

technique Glow
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 GlowPS();
		Texture[7] = <NoTexture>;
	}
}
