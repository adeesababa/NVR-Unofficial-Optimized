float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SunAmount;
float4 TESR_SkyColor;
float4 TESR_SkyLowColor;
float4 TESR_HorizonColor;
float4 TESR_CloudsShape;
float4 TESR_CloudsNoise;
float4 TESR_CloudsLight;
float4 TESR_CloudsWind;
float4 TESR_CloudsMarch;
float4 TESR_CloudsLayout;
float4 TESR_CloudsBlocks;
float4 TESR_CloudsLook;
float4x4 TESR_CloudsPrevViewProj;
float4 TESR_CloudsDetail;
float4 TESR_CloudsExtra;
float4x4 TESR_CloudsReflectInvVP;
float4x4 TESR_CloudsHistoryViewProj;
float4 TESR_CloudsReflectData;
float4 TESR_CloudsMotion;
float4 TESR_CloudsTint;
float4 TESR_WaterSettings;
float4 TESR_CloudsTower;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler3D TESR_CloudShape : register(s2) < string ResourceName = "Effects\CloudShape.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler3D TESR_CloudDetail : register(s3) < string ResourceName = "Effects\CloudDetail.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_CloudWeather : register(s4) < string ResourceName = "Effects\CloudWeather.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BlueNoiseSampler : register(s5) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_CloudCurrent : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_CloudHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler3D TESR_NoiseVolume : register(s8) < string ResourceName = "Effects\NoiseVolume.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s9) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_CloudShadow : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
#ifndef PHYSICAL_SKY_ON
    #define PHYSICAL_SKY_ON 0
#endif
#if PHYSICAL_SKY_ON
float4 TESR_PhysSky;
float4 TESR_PhysSkySun;
sampler2D TESR_SkyViewLUT : register(s11) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
#endif
sampler2D NVR_CloudLayerProbe : register(s12) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
texture NoTexture;

static const float Base = TESR_CloudsShape.x;
static const float Thickness = max(TESR_CloudsShape.y, 10.0);
static const float Coverage = saturate(TESR_CloudsShape.z);
static const float Density = TESR_CloudsShape.w;
static const float ShapeTile = max(TESR_CloudsNoise.x, 1.0);
static const float DetailTile = max(TESR_CloudsNoise.y, 1.0);
static const float Erosion = saturate(TESR_CloudsNoise.z);
static const float WeatherTile = max(TESR_CloudsNoise.w, 1.0);
static const float SunStrength = TESR_CloudsLight.x;
static const float AmbientStrength = TESR_CloudsLight.y;
static const float Forward = TESR_CloudsLight.z;
static const float RainDark = saturate(TESR_CloudsLight.w);
static const float Steps = max(TESR_CloudsMarch.x, 4.0);
static const float MaxDistance = TESR_CloudsMarch.y;
static const float HazeDistance = max(TESR_CloudsMarch.w, 1.0);
static const float2 LowTexel = TESR_CloudsLayout.xy;
static const float Sharpness = max(TESR_CloudsMotion.w, 0.0);
static const float Cumulus = saturate(TESR_CloudsDetail.w) * (1.0 - smoothstep(0.6, 0.9, saturate(TESR_CloudsShape.z)));

static const float TowerShare = TESR_CloudsTower.x > 0.0 ? clamp(TESR_CloudsTower.x, 0.05, 1.0) : 1.0;
static const float InvTowerShare = 1.0 / TowerShare;
static const float TowerGrow = InvTowerShare - 1.0;
static const float Towering = saturate(TESR_CloudsTower.y);
static const float Anvil = saturate(TESR_CloudsTower.z);
static const float HeapGrow = min(TowerGrow, 1.5);
static const float StormSpacing = 1.5 * WeatherTile;
static const float StormRadius = 0.55 * Thickness;
static const float BaseThickness = Thickness * TowerShare;

static const float EarthRadius = 6371000.0;
static const float MetresPerUnit = 1.0 / 70.0;
static const float Extinction = 0.04;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };
VSOUT FrameVS(VSIN IN) { VSOUT OUT = (VSOUT)0.0f; OUT.vertPos = IN.vertPos; OUT.UVCoord = IN.UVCoord; return OUT; }

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#if PHYSICAL_SKY_ON
    #include "Includes/Atmosphere.hlsl"
#endif

float Remap(float v, float lo, float hi, float newLo, float newHi) { return newLo + (v - lo) / max(hi - lo, 1e-5) * (newHi - newLo); }

float ShellDistance(float dz, float above) {
	return EarthRadius * (-dz + sqrt(dz * dz + 2.0 * above / EarthRadius));
}

float HeightProfile(float h, float hb, float type) {
	float top = lerp(0.3, 1.0, type);
	float bottom = lerp(0.08, 0.12, type);
	return smoothstep(0.0, bottom, hb) * (1.0 - smoothstep(top * 0.6, top, h));
}

float CloudDensity(float3 p, float h, float lod, bool cheap, bool towers = false) {
	if (h <= 0.0 || h >= 1.0) return 0.0;
	float4 weather = tex2Dlod(TESR_CloudWeather, float4(p.xy / WeatherTile, 0.0, 0.0));
	float local = saturate(Remap(weather.r, 1.0 - Coverage, 1.0, 0.0, 1.0));
	float type = saturate(weather.g + TESR_CloudsWind.w);
	float tower = 0.0, hb = h, storm = 0.0;
	if (towers) {
	tower = saturate(local * type * 4.0 - 0.6) * Towering;
	hb = h * InvTowerShare;
	[branch] if (hb * Towering > 0.0) {
		float2 cell = floor(p.xy / StormSpacing);
		float4 r = tex3Dlod(TESR_NoiseVolume, float4((cell + 0.5) / 64.0, 0.5 / 64.0, 0.0));
		float exists = saturate((0.75 * Towering - r.a) * 8.0);
		float2 centre = (cell + 0.3 + 0.4 * r.gb) * StormSpacing;
		float radius = StormRadius * (0.75 + 0.5 * r.r);
		float flare = smoothstep(0.72, 0.97, h);
		flare *= flare * Anvil;
		float2 shear = (r.bg - 0.5) * (2.0 * radius * flare);
		float width = radius * lerp(1.0 - 0.1 * h, 2.2, flare);
		float d = length(p.xy - centre - shear) / width;
		[branch] if (d < 1.6) {
			float4 n = tex3Dlod(TESR_NoiseVolume, float4(p / (2.0 * width), 0.0));
			d += (0.5 - n.b) * 0.6 + (0.5 - n.g) * 0.4;
		}
		storm = saturate((1.0 - d) * 3.0) * exists * smoothstep(0.0, 0.03, hb);
	}
	}
	if (local <= 0.0 && storm <= 0.0) return 0.0;
	if (towers) {
		h = hb / (1.0 + tower * HeapGrow);
		if (h >= 1.0 && storm <= 0.0) return 0.0;
		type += tower * (1.0 - type);
	}
	float3 s = p / ShapeTile;
	s += (tex3Dlod(TESR_NoiseVolume, float4(s * 0.5, lod)).rga - 0.5) * (0.15 * TESR_CloudsLook.z * lerp(1.0, 0.4, Cumulus));
	float shape = tex3Dlod(TESR_CloudShape, float4(s, lod + TESR_CloudsLook.y)).r;
	float base = saturate(Remap(shape * HeightProfile(h, hb, type), 1.0 - local, 1.0, 0.0, 1.0)) * local;
	float stormCloud = towers ? saturate((storm - 0.5 * (1.0 - shape)) * 2.0) : 0.0;
	{
		float top = lerp(0.45, 1.0, type);
		float rise = saturate(h / top);
		float threshold = lerp(1.0 - local, 1.0, 0.9 * rise * (1.0 - 0.5 * tower));
		float core = saturate(Remap(shape, threshold, 1.0, 0.0, 1.0));
		core *= smoothstep(0.0, 0.03, hb) * (1.0 - smoothstep(0.85, 1.0, rise));
		core = saturate(Remap(core, 0.1, 1.0, 0.0, 1.0)) * local;
		base = lerp(base, core, Cumulus);
	}
	if (towers) base = max(base, stormCloud);
	if (base <= 0.0) return 0.0;
	if (!cheap) {
		float3 q = (p + float3(0.0, 0.0, TESR_CloudsWind.z)) / DetailTile;
		float4 warp = tex3Dlod(TESR_NoiseVolume, float4(q * 0.125, lod));
		q += (warp.rga - 0.5) * TESR_CloudsLook.z * lerp(1.0, 0.5, Cumulus);
		float detail = lerp(tex3Dlod(TESR_CloudDetail, float4(q, lod)).r, warp.g, TESR_CloudsLook.w);
		[branch] if (TESR_CloudsDetail.x > 0.0) {
			float3 f = (p + float3(0.0, 0.0, 1.7 * TESR_CloudsWind.z)) / max(TESR_CloudsDetail.y, 1.0) + (warp.gar - 0.5) * TESR_CloudsLook.z;
			float fine = tex3Dlod(TESR_CloudDetail, float4(f, lod)).r;
			detail = lerp(detail, detail * 0.65 + fine * 0.35, TESR_CloudsDetail.x);
		}
		float wisps = lerp(detail, 1.0 - detail, saturate(hb * 6.0));
		float erosion = Erosion;
		wisps = lerp(wisps, 1.0 - detail, Cumulus);
		erosion *= lerp(1.0, lerp(0.35, 1.0, smoothstep(0.0, 0.2, hb)), Cumulus);
		base = saturate(Remap(base, wisps * erosion, 1.0, 0.0, 1.0));
	}
	base = saturate(base * max(TESR_CloudsLook.x, 1.0) * (1.0 + 0.5 * Cumulus));
	return base * Density * lerp(0.6, 1.0, weather.b);
}

float Phase(float cosT, float g) {
	return (1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosT, 1e-4), 1.5);
}

float4 MarchRay(float3 dir, float jitter, float stepScale, bool fixedExit = false, bool towers = false) {
	float steps = max(Steps * stepScale, 4.0);
	if (towers) steps = max(64.0 * stepScale, 4.0);
	float3 camera = TESR_CameraPosition.xyz * MetresPerUnit;
	float baseAbove = max(Base - camera.z, 50.0);
	float tStart = ShellDistance(dir.z, baseAbove);
	float tEnd = ShellDistance(dir.z, baseAbove + Thickness);
	if (dir.z < -0.15 || tStart > MaxDistance) return float4(0.0, 0.0, 0.0, 1.0);
	tEnd = min(tEnd, tStart + 4.0 * Thickness + 8000.0);
	float dt = (tEnd - tStart) / steps;

	float3 light = normalize(TESR_SunDirection.xyz);
	float day = saturate(TESR_SunAmount.x);
	float cosT = dot(dir, light);
	float3 sun = linearize(TESR_SunColor.rgb) * SunStrength * lerp(0.15, 1.0, day) * (1.0 - 0.6 * RainDark);
	float3 skyTop = linearize(TESR_SkyColor.rgb);
	float3 skyLow = linearize(TESR_HorizonColor.rgb);
	float3 ambientTop = skyTop * AmbientStrength * (1.0 - 0.4 * RainDark);
	float3 ambientBottom = lerp(skyLow, skyTop, 0.5) * AmbientStrength * 0.5 * (1.0 - 0.6 * RainDark);
#if PHYSICAL_SKY_ON
	[branch] if (TESR_PhysSky.x > 0.0) {
		const float share = TESR_PhysSky.x, tint = TESR_PhysSky.z, tableScale = 0.001 * TESR_PhysSky.y;
		float3 through = exp(-AtmSunDepth(AtmGround + Base + 0.5 * Thickness, TESR_PhysSkySun.z, TESR_PhysSky.w));
		float3 physSun = luma(sun) * through / max(luma(through), 1e-4);
		sun = lerp(sun, lerp(physSun, sun, tint), share);
		float3 zenith = tex2Dlod(TESR_SkyViewLUT, float4(0.5, 1.0, 0.0, 0.0)).rgb * tableScale;
		float3 low = (tex2Dlod(TESR_SkyViewLUT, float4(0.0, 0.35, 0.0, 0.0)).rgb + tex2Dlod(TESR_SkyViewLUT, float4(0.5, 0.35, 0.0, 0.0)).rgb +
			tex2Dlod(TESR_SkyViewLUT, float4(1.0, 0.35, 0.0, 0.0)).rgb) * (tableScale / 3.0);
		zenith *= luma(skyTop) / max(luma(zenith), 1e-6);
		low *= luma(skyLow) / max(luma(low), 1e-6);
		float3 physTop = lerp(zenith, skyTop, tint);
		float3 physLow = lerp(low, skyLow, tint);
		ambientTop = lerp(ambientTop, physTop * AmbientStrength * (1.0 - 0.4 * RainDark), share);
		ambientBottom = lerp(ambientBottom, lerp(physLow, physTop, 0.5) * AmbientStrength * 0.5 * (1.0 - 0.6 * RainDark), share);
	}
#endif
	float lightStep = (towers ? BaseThickness : Thickness) / 12.0;
	float lightRise = max(light.z, 0.1);

	float3 wind = float3(TESR_CloudsWind.xy, 0.0);
	float3 scattered = 0.0;
	float transmittance = 1.0;
	float depthSum = 0.0, depthWeight = 0.0;
	[loop] for (float i = 0.0; i < 64.0 && (!fixedExit || !(i >= steps || transmittance < 0.01)); i += 1.0) {
		if (!fixedExit && (i >= steps || transmittance < 0.01)) break;
		float t = tStart + (i + jitter) * dt;
		float altitude = camera.z + t * dir.z + t * t / (2.0 * EarthRadius);
		float h = (altitude - Base) / Thickness;
		float3 p = float3(camera.xy + dir.xy * t, altitude) + wind;
		float lod = saturate(t / 40000.0) * 2.0 * (1.0 - 0.5 * saturate(Sharpness));
		float density = CloudDensity(p, h, lod, false, towers);
		[branch] if (density > 0.0) {
			float opticalDepth = 0.0;
			float reach = 0.0;
			[branch] if (towers) {
				float stepLength = lightStep * 0.5;
				[loop] for (float j = 0.0; j < 6.0; j += 1.0) {
					reach += stepLength;
					float3 q = p + light * reach;
					float hq = h + reach * lightRise / Thickness;
					opticalDepth += CloudDensity(q, hq, lod + 1.0, j > 2.5, true) * stepLength;
					stepLength *= 2.0;
				}
			}
			else {
				[unroll] for (int j = 0; j < 6; j++) {
					float stepLength = lightStep * exp2((float)j) * 0.5;
					reach += stepLength;
					float3 q = p + light * reach;
					float hq = h + reach * lightRise / Thickness;
					opticalDepth += CloudDensity(q, hq, lod + 1.0, j > 2) * stepLength;
				}
			}
			if (towers) opticalDepth *= 1.0 - 0.8 * Towering * saturate((h * InvTowerShare - 0.5) / 1.5);
			float3 sunLight = 0.0;
			float a = 1.0, b = 1.0, c = 1.0;
			[unroll] for (int k = 0; k < 3; k++) {
				float g = Forward * c;
				sunLight += b * exp(-Extinction * opticalDepth * a) * lerp(Phase(cosT, g), Phase(cosT, -0.3 * c), 0.5);
				a *= 0.25; b *= 0.5; c *= 0.5;
			}
			float powder = 1.0 - exp(-2.0 * Extinction * density * 40.0);
			float hb = towers ? h * InvTowerShare : h;
			float3 ambient = lerp(ambientBottom, ambientTop, saturate(hb));
			if (towers) ambient = lerp(ambient, luma(ambient) * 1.15, 0.7 * Towering * saturate((hb - 0.5) / 1.5));
			float lowLight = lerp(1.0, lerp(0.75, 1.0, smoothstep(0.0, 0.2, hb)), Cumulus);
			float3 radiance = (sun * sunLight * lerp(0.5, 1.0, powder) + ambient) * lowLight;
			float sigma = Extinction * density;
			float stepTransmittance = exp(-sigma * dt);
			scattered += transmittance * radiance * (1.0 - stepTransmittance);
			depthSum += t * transmittance * (1.0 - stepTransmittance);
			depthWeight += transmittance * (1.0 - stepTransmittance);
			transmittance *= stepTransmittance;
		}
	}
	scattered *= lerp(1.0, TESR_CloudsTint.rgb, TESR_CloudsTint.w);
	float distance = depthWeight > 0.0 ? depthSum / depthWeight : tStart;
	if (towers) {
		float hitAltitude = camera.z + distance * dir.z + distance * distance / (2.0 * EarthRadius);
		distance *= 1.0 - 0.75 * Towering * saturate(3.0 * (hitAltitude - Base - 0.5 * BaseThickness) / max(Thickness - BaseThickness, 1.0));
	}
	float haze = exp(-(distance * distance) / (HazeDistance * HazeDistance)) * saturate((MaxDistance - distance) / (0.4 * MaxDistance));
	scattered *= haze;
	transmittance = lerp(1.0, transmittance, haze);
	return float4(scattered, transmittance);
}

float4 MarchPS(VSOUT IN, float2 vpos : VPOS, uniform bool fixedExit, uniform bool towers) : COLOR0 {
	float2 pixel = vpos * 2.0 + TESR_CloudsBlocks.zw;
	float2 uv = (pixel + 0.5) * LowTexel;
	float jitter = frac(tex2Dlod(TESR_BlueNoiseSampler, float4(pixel / 256.0, 0, 0)).r + TESR_CloudsMarch.z * 0.61803398875);
	return MarchRay(normalize(toWorld(uv)), jitter, 1.0, fixedExit, towers);
}

float4 CatmullRom(float2 uv) {
	float2 size = 1.0 / LowTexel;
	float2 position = uv * size;
	float2 centre = floor(position - 0.5) + 0.5;
	float2 f = position - centre;
	float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
	float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
	float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
	float2 w3 = f * f * (-0.5 + 0.5 * f);
	float2 w12 = w1 + w2;
	float2 offset12 = w2 / w12;
	float2 t0 = (centre - 1.0) * LowTexel, t3 = (centre + 2.0) * LowTexel, t12 = (centre + offset12) * LowTexel;
	float4 result = 0.0;
	result += tex2Dlod(TESR_CloudHistory, float4(t0.x, t0.y, 0, 0)) * w0.x * w0.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t12.x, t0.y, 0, 0)) * w12.x * w0.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t3.x, t0.y, 0, 0)) * w3.x * w0.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t0.x, t12.y, 0, 0)) * w0.x * w12.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t12.x, t12.y, 0, 0)) * w12.x * w12.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t3.x, t12.y, 0, 0)) * w3.x * w12.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t0.x, t3.y, 0, 0)) * w0.x * w3.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t12.x, t3.y, 0, 0)) * w12.x * w3.y;
	result += tex2Dlod(TESR_CloudHistory, float4(t3.x, t3.y, 0, 0)) * w3.x * w3.y;
	return float4(max(result.rgb, 0.0), saturate(result.a));
}

float4 SharpHistory(float2 uv) {
	float2 size = 1.0 / LowTexel;
	float2 corner = (floor(uv * size - 0.5) + 0.5) * LowTexel;
	float4 a = tex2Dlod(TESR_CloudHistory, float4(corner, 0, 0));
	float4 b = tex2Dlod(TESR_CloudHistory, float4(corner + float2(LowTexel.x, 0.0), 0, 0));
	float4 c = tex2Dlod(TESR_CloudHistory, float4(corner + float2(0.0, LowTexel.y), 0, 0));
	float4 d = tex2Dlod(TESR_CloudHistory, float4(corner + LowTexel, 0, 0));
	return clamp(CatmullRom(uv), min(min(a, b), min(c, d)), max(max(a, b), max(c, d)));
}

float4 TemporalPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * LowTexel;
	float2 block = floor(vpos * 0.5);
	float2 blockUV = (block + 0.5) * TESR_CloudsBlocks.xy;
	float4 current = tex2Dlod(TESR_CloudCurrent, float4(blockUV, 0, 0));
	float4 lo = current, hi = current;
	[unroll] for (int y = -1; y <= 1; y++) {
		[unroll] for (int x = -1; x <= 1; x++) {
			float4 n = tex2Dlod(TESR_CloudCurrent, float4(blockUV + float2(x, y) * TESR_CloudsBlocks.xy, 0, 0));
			lo = min(lo, n); hi = max(hi, n);
		}
	}
	bool fresh = all(vpos - block * 2.0 == TESR_CloudsBlocks.zw);
	float3 dir = normalize(toWorld(uv));
	float3 previousDir = dir;
	[branch] if (Sharpness > 0.0) {
		float3 camera = TESR_CameraPosition.xyz * MetresPerUnit;
		float reach = ShellDistance(dir.z, max(Base + 0.4 * BaseThickness - camera.z, 50.0));
		previousDir = dir * reach + float3(TESR_CloudsMotion.xy, 0.0);
	}
	float4 clip = mul(float4(previousDir, 0.0), TESR_CloudsPrevViewProj);
	float weight = TESR_CloudsLayout.z;
	[branch] if (clip.w <= 0.0 || weight <= 0.0) return current;
	float2 previous = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
	if (any(previous < 0.0) || any(previous > 1.0)) return current;
	float4 history = clamp(Sharpness > 0.0 ? SharpHistory(previous) : tex2Dlod(TESR_CloudHistory, float4(previous, 0, 0)), lo, hi);
	return fresh ? lerp(current, history, weight) : history;
}

static const float ShadowStrength = TESR_CloudsExtra.x;
static const float ShadowRange = max(TESR_CloudsExtra.y, 1000.0);
static const float ShadowMapSize = max(TESR_CloudsDetail.z, 256.0);

float2 ShadowCentre() {
	float texel = ShadowRange / ShadowMapSize;
	return floor(TESR_CameraPosition.xy * MetresPerUnit / texel) * texel;
}

float3 ShadowLight() {
	float3 light = normalize(TESR_SunDirection.xyz);
	light.z = max(light.z, 0.25);
	return normalize(light);
}

float4 ShadowMapPS(VSOUT IN, float2 vpos : VPOS, uniform bool towers) : COLOR0 {
	float2 at = ShadowCentre() + ((vpos + 0.5) / ShadowMapSize - 0.5) * ShadowRange;
	float3 light = ShadowLight();
	float length = Thickness / light.z;
	float stepLength = length / 8.0;
	float3 wind = float3(TESR_CloudsWind.xy, 0.0);
	float opticalDepth = 0.0;
	[branch] if (towers) {
		[loop] for (float i = 0.5; i < 8.0; i += 1.0) {
			float3 p = float3(at, Base) + light * (i * stepLength);
			opticalDepth += CloudDensity(p + wind, (p.z - Base) / Thickness, 1.0, false, true) * stepLength;
		}
	}
	else {
		[unroll] for (int i = 0; i < 8; i++) {
			float s = (i + 0.5) * stepLength;
			float3 p = float3(at, Base) + light * s;
			opticalDepth += CloudDensity(p + wind, (p.z - Base) / Thickness, 1.0, false) * stepLength;
		}
	}
	return float4(exp(-Extinction * opticalDepth), 0.0, 0.0, 1.0);
}

float CloudShadowAt(float3 world) {
	float3 light = ShadowLight();
	float2 q = world.xy + light.xy * ((Base - world.z) / light.z);
	float2 suv = (q - ShadowCentre()) / ShadowRange + 0.5;
	float2 edge = min(suv, 1.0 - suv);
	float inside = saturate(min(edge.x, edge.y) * 10.0);
	return lerp(1.0, tex2Dlod(TESR_CloudShadow, float4(suv, 0, 0)).r, inside);
}

float4 CompositePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);
	[branch] if (depth < farZ * 0.996) {
		[branch] if (ShadowStrength <= 0.0) return color;
		float3 eye = toWorld(uv);
		float3 world = (TESR_CameraPosition.xyz + eye * depth) * MetresPerUnit;
		float3 normal = mul((float3x3)TESR_ViewTransform, normalize(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0, 0)).xyz * 2.0 - 1.0));
		float height = TESR_CameraPosition.z + eye.z * depth;
		bool water = normal.z > 0.7 && abs(height - TESR_WaterSettings.x) < max(2.0, depth * 0.002);
		[branch] if (ShadowStrength > 0.0 && !water) {
			float3 sunDir = normalize(TESR_SunDirection.xyz);
			float day = saturate(TESR_SunAmount.x);
			float facing = smoothstep(0.0, 0.25, dot(normal, sunDir));
			float strength = ShadowStrength * lerp(0.3, 1.0, day) * smoothstep(0.08, 0.35, sunDir.z) * facing;
			float shadow = CloudShadowAt(world);
			[branch] if (TESR_CloudsExtra.w > 0.0) return float4(shadow.xxx * lerp(0.4, 1.0, facing), 1.0);
			float factor = lerp(1.0, shadow, strength);
			[branch] if (factor >= 1.0) return color;
			return float4(delinearize(max(linearize(max(color.rgb, 0.0)) * factor, 0.0)), color.a);
		}
		return color;
	}
	float4 clouds;
	[branch] if (LowTexel.x < TESR_ReciprocalResolution.x * 0.99) {
		float2 q = 0.25 * TESR_ReciprocalResolution.xy;
		clouds = 0.25 * (tex2Dlod(TESR_CloudHistory, float4(uv + float2(-q.x, -q.y), 0, 0)) + tex2Dlod(TESR_CloudHistory, float4(uv + float2(q.x, -q.y), 0, 0)) +
			tex2Dlod(TESR_CloudHistory, float4(uv + float2(-q.x, q.y), 0, 0)) + tex2Dlod(TESR_CloudHistory, float4(uv + q, 0, 0)));
	}
	else clouds = CatmullRom(uv);
	[branch] if (Sharpness > 0.0) {
		float4 n0 = tex2Dlod(TESR_CloudHistory, float4(uv + float2(LowTexel.x, 0.0), 0, 0));
		float4 n1 = tex2Dlod(TESR_CloudHistory, float4(uv - float2(LowTexel.x, 0.0), 0, 0));
		float4 n2 = tex2Dlod(TESR_CloudHistory, float4(uv + float2(0.0, LowTexel.y), 0, 0));
		float4 n3 = tex2Dlod(TESR_CloudHistory, float4(uv - float2(0.0, LowTexel.y), 0, 0));
		float4 lo = min(min(min(n0, n1), min(n2, n3)), clouds), hi = max(max(max(n0, n1), max(n2, n3)), clouds);
		float4 sharpened = clouds + (clouds - 0.25 * (n0 + n1 + n2 + n3)) * (1.5 * Sharpness);
		clouds = clamp(sharpened, lo, hi);
	}
	float3 lin = linearize(max(color.rgb, 0.0)) * clouds.a + clouds.rgb * TESR_CloudsLayout.w;
	return float4(delinearize(max(lin, 0.0)), color.a);
}

VSOUT ReflectionVS(VSIN IN) {
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = float4(IN.vertPos.xy, TESR_CloudsReflectData.w, 1.0);
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

float4 ReflectionPS(VSOUT IN, float2 vpos : VPOS, uniform bool towers) : COLOR0 {
	float2 ndc = float2((vpos.x + 0.5) / TESR_CloudsReflectData.y * 2.0 - 1.0, 1.0 - (vpos.y + 0.5) / TESR_CloudsReflectData.z * 2.0);
	float4 far = mul(float4(ndc, 0.5, 1.0), TESR_CloudsReflectInvVP);
	float3 dir = normalize(far.xyz / far.w);
	float4 clouds = float4(0.0, 0.0, 0.0, 1.0);
	[branch] if (TESR_CloudsExtra.z > 0.0) return dir.z > 0.0 ? float4(1.0, 0.0, 0.0, 0.0) : float4(0.0, 0.0, 1.0, 0.0);
	float4 clip = mul(float4(dir, 0.0), TESR_CloudsHistoryViewProj);
	float2 at = clip.xy / max(clip.w, 1e-4) * float2(0.5, -0.5) + 0.5;
	float2 edge = min(at, 1.0 - at);
	float seen = clip.w > 0.0 ? saturate(min(edge.x, edge.y) * 20.0) : 0.0;
	[branch] if (seen > 0.0) clouds = tex2Dlod(TESR_CloudHistory, float4(saturate(at), 0, 0));
	[branch] if (seen < 1.0) {
		float jitter = frac(tex2Dlod(TESR_BlueNoiseSampler, float4(vpos / 256.0, 0, 0)).r + TESR_CloudsMarch.z * 0.61803398875);
		clouds = lerp(MarchRay(dir, jitter, 0.5, false, towers), clouds, seen);
	}
	float strength = saturate(TESR_CloudsReflectData.x);
	float transmittance = lerp(1.0, saturate(clouds.a), strength);
	float coverage = 1.0 - pow(transmittance, 0.4545);
	float3 own = clouds.rgb * strength * TESR_CloudsLayout.w / max(1.0 - transmittance, 1e-3);
	return float4(delinearize(max(own, 0.0)) * coverage, 1.0 - coverage);
}

float4 LayerProbePS(VSOUT IN) : COLOR0 {
	float sum = 0.0;
	[loop] for (float y = 0.5; y < 16.0; y += 1.0) {
		[loop] for (float x = 0.5; x < 16.0; x += 1.0) sum += tex2Dlod(NVR_CloudLayerProbe, float4(x / 16.0, y / 16.0, 0.0, TESR_CloudsMotion.z)).a;
	}
	return (sum / 256.0).xxxx;
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
		PixelShader = compile ps_3_0 MarchPS(false, false);
		Texture[6] = <NoTexture>;
	}
}

technique Temporal {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 TemporalPS();
	}
}

technique Reflection {
	pass {
		VertexShader = compile vs_3_0 ReflectionVS();
		PixelShader = compile ps_3_0 ReflectionPS(false);
	}
}

technique ShadowMap {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ShadowMapPS(false);
		Texture[10] = <NoTexture>;
	}
}

technique LayerProbe {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 LayerProbePS();
	}
}

technique MarchFixedExit {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 MarchPS(true, false);
		Texture[6] = <NoTexture>;
	}
}

technique MarchTowers {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 MarchPS(false, true);
		Texture[6] = <NoTexture>;
	}
}

technique MarchTowersFixedExit {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 MarchPS(true, true);
		Texture[6] = <NoTexture>;
	}
}

technique ReflectionTowers {
	pass {
		VertexShader = compile vs_3_0 ReflectionVS();
		PixelShader = compile ps_3_0 ReflectionPS(true);
	}
}

technique ShadowMapTowers {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 ShadowMapPS(true);
		Texture[10] = <NoTexture>;
	}
}
