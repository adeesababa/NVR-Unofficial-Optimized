float4 TESR_AmbientLightData;
float4 TESR_AmbientLightData2;
float4 TESR_ReciprocalResolution;
float4 NVR_ALLayout;
float4 NVR_ALFlags;
float4 NVR_ALInterior;
float4 NVR_ALScene;
float4 NVR_ALTemporal;
float4 NVR_ALEdges;
float4x4 NVR_ALReproject;
float4 TESR_WaterSettings;
float4 TESR_ShadowData;
float4 TESR_ShadowFade;
float4 TESR_SkyColor;
float4 TESR_SunAmbient;
float4 TESR_SunDirection;
float4 TESR_AmbientOcclusionSky5;
float4 TESR_AmbientOcclusionData;

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BlueNoiseSampler : register(s3) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_PointShadowBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D NVR_ALPrep : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALBuffer : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALOcclusionHistory : register(s11) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

static const float Radius = max(TESR_AmbientLightData.x, 1.0f);
static const float OcclusionStrength = TESR_AmbientLightData.y;
static const float BounceStrength = TESR_AmbientLightData.z;
static const float DrawDistance = max(TESR_AmbientLightData.w, 100.0f);
static const float DarkestAO = saturate(TESR_AmbientLightData2.x);
static const float BrightFade = TESR_AmbientLightData2.y;
static const float BlurStep = max(TESR_AmbientLightData2.z, 0.5f);
static const float DebugView = TESR_AmbientLightData2.w;
static const int SLICES = 2;
static const int STEPS = 4;
static const float BounceScale = 2.5f;
static const float NearFade = 40.0f;
static const float MaxScreenRadius = 0.08f;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };

VSOUT FrameVS(VSIN IN) {
	VSOUT OUT = (VSOUT)0.0f;
	OUT.vertPos = IN.vertPos;
	OUT.UVCoord = IN.UVCoord;
	return OUT;
}

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"

static const int endFade = 8000;
#include "Includes/SkyTake.hlsl"

sampler2D NVR_ALViewModel : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
bool IsViewModel(float2 uv) {
	float d = tex2Dlod(NVR_ALViewModel, float4(uv, 0.0f, 0.0f)).x;
	return (TESR_DepthConstants.z == 0 && d < 0.9f) || (TESR_DepthConstants.z > 0 && d > 0.01f);
}

float4 NVR_SunFarFlags;
sampler2D NVR_SunFarAL : register(s13) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
float3 SunShadowApply(float3 color, float2 uv) {
	color *= lerp(1.0f, tex2Dlod(NVR_SunFarAL, float4(uv, 0.0f, 0.0f)).rgb, NVR_SunFarFlags.x);
	[branch] if (TESR_WaterSettings.z == 1) {
		float3 worldPos = TESR_CameraPosition.xyz + toWorld(uv) * readDepthLod(uv);
		if (worldPos.z < (TESR_WaterSettings.x + 2) && worldPos.z > (TESR_WaterSettings.x - 2) && dot(GetWorldNormalLod(uv), float3(0, 0, -1)) > 0.999)
			return color;
	}
	float darkness = max(0.0, 1 - TESR_ShadowData.y);
	float2 shadow = tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0, 0)).rg;
	shadow.r = lerp(TESR_ShadowFade.x, 1.0f, shadow.r);
	float ambient = lerp(1, luma(TESR_SunAmbient), darkness * TESR_ShadowFade.z);
	shadow.r = lerp(0, ambient, shadow.r);
	shadow.r += shadow.g;
	shadow.r = saturate(lerp(darkness, 1.0, shadow.r));
	float3 linearColor = pows(color, 2.2);
	float3 colorShadow = luma(linearColor) * shadow.r * pows(TESR_SkyColor.rgb, 2.2);
	colorShadow = lerp(colorShadow, linearColor * shadow.r, saturate(shadow.r + 0.5));
	return pows(max(0.0, colorShadow), 1.0 / 2.2);
}

float3 PositionFromLinear(float2 uv, float linear01) {
	float z = projectedDepthFromLinear(linear01);
	float4 viewSpace = mul(float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, z, 1.0f), TESR_InvProjectionTransform);
	return viewSpace.xyz / viewSpace.w;
}

float3 PositionFromViewZ(float2 uv, float z) {
	return float3((uv.x * 2.0f - 1.0f) / TESR_ProjectionTransform[0][0], (1.0f - uv.y * 2.0f) / TESR_ProjectionTransform[1][1], 1.0f) * z;
}

float Pack2(float2 v) { v = floor(saturate(v) * 4095.0f + 0.5f); return v.x * 4096.0f + v.y; }
float2 Unpack2(float p) { float high = floor(p / 4096.0f); return float2(high, p - high * 4096.0f) / 4095.0f; }
float2 OctEncode(float3 n) {
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	float2 s = n.xy >= 0.0f ? 1.0f : -1.0f;
	float2 e = n.z >= 0.0f ? n.xy : (1.0f - abs(n.yx)) * s;
	return e * 0.5f + 0.5f;
}
float3 OctDecode(float2 e) {
	e = e * 2.0f - 1.0f;
	float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
	float t = saturate(-n.z);
	n.xy += n.xy >= 0.0f ? -t : t;
	return normalize(n);
}

float4 Prepare(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float viewZ = PositionFromLinear(uv, tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x).z;
	if (IsViewModel(uv)) viewZ = 1.0e6f;
	float3 normal = normalize(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2 - 1);
	return float4(viewZ, Pack2(OctEncode(normal)), 0.0f, 0.0f);
}

float4 SmoothNormal(VSOUT IN, uniform bool alongY) : COLOR0 {
	float2 uv = IN.UVCoord;
	float viewZ; float3 normal;
	if (alongY) { float4 c = tex2Dlod(NVR_ALBuffer, float4(uv, 0.0f, 0.0f)); normal = c.xyz; viewZ = c.w; }
	else { float4 c = tex2Dlod(NVR_ALPrep, float4(uv, 0.0f, 0.0f)); viewZ = c.x; normal = OctDecode(Unpack2(c.y)); }
	[branch] if (viewZ < DrawDistance && NVR_ALEdges.x > 0.5f) {
		float3 origin = PositionFromViewZ(uv, viewZ);
		float2 axis = alongY ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f);
		float spacing = NVR_ALEdges.x / 3.0f;
		float3 sum = normal;
		[unroll] for (int k = -3; k <= 3; k++) {
			if (k == 0) continue;
			float dist = abs((float)k) * spacing;
			float2 tapUV = uv + axis * (k * spacing) * NVR_ALLayout.zw;
			float onScreen = step(0.0f, tapUV.x) * step(tapUV.x, 1.0f) * step(0.0f, tapUV.y) * step(tapUV.y, 1.0f);
			tapUV = saturate(tapUV);
			float tapZ; float3 tapNormal;
			if (alongY) { float4 t = tex2Dlod(NVR_ALBuffer, float4(tapUV, 0.0f, 0.0f)); tapNormal = t.xyz; tapZ = t.w; }
			else { float4 t = tex2Dlod(NVR_ALPrep, float4(tapUV, 0.0f, 0.0f)); tapZ = t.x; tapNormal = OctDecode(Unpack2(t.y)); }
			float tolerance = max(viewZ * 0.001f * dist, 0.5f);
			float plane = abs(dot(PositionFromViewZ(tapUV, tapZ) - origin, normal));
			float weight = saturate(1.0f - plane / tolerance) * smoothstep(0.5f, 0.8f, dot(tapNormal, normal)) * onScreen;
			sum += tapNormal * weight;
		}
		normal = normalize(sum);
	}
	return alongY ? float4(viewZ, Pack2(OctEncode(normal)), 0.0f, 0.0f) : float4(normal, viewZ);
}

float SliceArc(float horizonCos0, float horizonCos1, float n, float cosNormal, float sinN) {
	float h0 = n + clamp(-acos(clamp(horizonCos1, -1.0f, 1.0f)) - n, -1.5707963f, 1.5707963f);
	float h1 = n + clamp(acos(clamp(horizonCos0, -1.0f, 1.0f)) - n, -1.5707963f, 1.5707963f);
	return (cosNormal + 2.0f * h0 * sinN - cos(2.0f * h0 - n)) * 0.25f + (cosNormal + 2.0f * h1 * sinN - cos(2.0f * h1 - n)) * 0.25f;
}

float4 Estimate(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 center = tex2Dlod(NVR_ALPrep, float4(uv, 0.0f, 0.0f));
	float3 origin = PositionFromViewZ(uv, center.x);
	float fade = smoothstep(NearFade, NearFade * 2.0f, origin.z) * (1.0f - smoothstep(DrawDistance * 0.7f, DrawDistance, origin.z));
	[branch] if (fade <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 1.0f);

	float3 normal = OctDecode(Unpack2(center.y));
	float3 viewVec = normalize(-origin);
	float3 noise = tex2Dlod(TESR_BlueNoiseSampler, float4(uv / (256.0f * NVR_ALLayout.zw), 0.0f, 0.0f)).xyz;
	noise.xy = frac(noise.xy + (NVR_ALTemporal.x > 0.0f ? NVR_ALTemporal.z : 0.0f) * float2(0.618034f, 0.754878f));
	float2 screenRadius = Radius * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) / (2.0f * origin.z);
	screenRadius *= min(1.0f, MaxScreenRadius / screenRadius.x);
	float falloffStart = 0.4f * Radius;

	[branch] if (DebugView > 2.5f && DebugView < 3.5f) return float4(normal * 0.5f + 0.5f, 1.0f);
	[branch] if (DebugView > 3.5f && DebugView < 4.5f) return float4(origin.z / 2000.0f, frac(origin.z / 100.0f), 0.0f, 1.0f);
	[branch] if (DebugView > 4.5f && DebugView < 5.5f) return float4(screenRadius * 10.0f, Radius / 1000.0f, 1.0f);
	float visibility = 0.0f, weightSum = 0.0f;
	float3 bounce = 0.0f;
	float firstCos = 0.0f, firstLow = 0.0f;
	[unroll] for (int slice = 0; slice < SLICES; slice++) {
		float phi = (slice + noise.x) * (3.14159265f / SLICES);
		float2 omega = float2(cos(phi), -sin(phi));
		float3 direction = float3(cos(phi), sin(phi), 0.0f);
		float3 orthoDirection = direction - dot(direction, viewVec) * viewVec;
		float3 axis = normalize(cross(orthoDirection, viewVec));
		float3 projectedNormal = normal - axis * dot(normal, axis);
		float projectedLength = length(projectedNormal) + 1e-5f;
		float signNormal = dot(orthoDirection, projectedNormal) >= 0.0f ? 1.0f : -1.0f;
		float cosNormal = saturate(dot(projectedNormal, viewVec) / projectedLength);
		float n = signNormal * acos(cosNormal);
		float lowCos0 = cos(n + 1.5707963f - NVR_ALEdges.y), lowCos1 = cos(n - 1.5707963f + NVR_ALEdges.y);
		float horizonCos0 = lowCos0, horizonCos1 = lowCos1;
		[unroll] for (int k = 0; k < STEPS; k++) {
			float s = (k + frac(noise.y + k * 0.618034f)) / STEPS;
			s *= s;
			float2 offset = omega * screenRadius * max(s, 2.0f * NVR_ALLayout.z);
			[unroll] for (int side = 0; side < 2; side++) {
				float2 sampleUV = uv + (side == 0 ? offset : -offset);
				float onScreen = step(0.0f, sampleUV.x) * step(sampleUV.x, 1.0f) * step(0.0f, sampleUV.y) * step(sampleUV.y, 1.0f);
				sampleUV = (floor(saturate(sampleUV) / NVR_ALLayout.zw) + 0.5f) * NVR_ALLayout.zw;
				float4 texel = tex2Dlod(NVR_ALPrep, float4(sampleUV, 0.0f, 0.0f));
				float3 delta = PositionFromViewZ(sampleUV, texel.x) - origin;
				float dist = length(delta) + 1e-4f;
				float3 dir = delta / dist;
				float weight = saturate((Radius - dist) / (Radius - falloffStart)) * onScreen * step(NearFade, origin.z + delta.z);
				float lowCos = side == 0 ? lowCos0 : lowCos1;
				float sampleCos = lerp(lowCos, dot(dir, viewVec), weight);
				if (side == 0) horizonCos0 = max(horizonCos0, sampleCos); else horizonCos1 = max(horizonCos1, sampleCos);
				if (slice == 0 && side == 0 && k == 1) { firstCos = dot(dir, viewVec); firstLow = weight; }
			}
		}
		float sinN = sin(n);
		visibility += projectedLength * SliceArc(horizonCos0, horizonCos1, n, cosNormal, sinN);
		weightSum += projectedLength * SliceArc(lowCos0, lowCos1, n, cosNormal, sinN);
	}
	[branch] if (DebugView > 5.5f) return float4(visibility / max(weightSum, 1e-4f), firstCos * 0.5f + 0.5f, firstLow, 1.0f);
	visibility = pow(saturate(visibility / max(weightSum, 1e-4f)), 1.5f * OcclusionStrength);
	visibility = lerp(1.0f, visibility, fade);
	return float4(bounce * (BounceScale * fade / (SLICES * STEPS * 2)), saturate(visibility));
}

float4 Blur(VSOUT IN, uniform float2 axis) : COLOR0 {
	float depth = readDepthLod(IN.UVCoord);
	float tolerance = max(depth * 0.03f, 2.0f);
	float looseTolerance = max(depth * 0.25f, 16.0f);
	static const float weights[4] = { 0.30f, 0.23f, 0.11f, 0.05f };
	float4 center = tex2Dlod(NVR_ALBuffer, float4(IN.UVCoord, 0.0f, 0.0f));
	[branch] if (DebugView > 2.5f && DebugView < 6.5f) return center;
	float3 bounce = center.rgb * weights[0];
	float visibility = center.a * weights[0];
	float2 total = weights[0];
	[unroll] for (int i = 1; i < 4; i++) {
		[unroll] for (int side = -1; side <= 1; side += 2) {
			float2 tapUV = IN.UVCoord + axis * NVR_ALLayout.zw * (i * side * BlurStep);
			float dz = abs(readDepthLod(tapUV) - depth);
			float2 w = weights[i] * saturate(1.0f - dz / float2(tolerance, looseTolerance));
			float4 tap = tex2Dlod(NVR_ALBuffer, float4(tapUV, 0.0f, 0.0f));
			visibility += tap.a * w.x;
			bounce += tap.rgb * w.y;
			total += w;
		}
	}
	return float4(bounce / total.y, visibility / total.x);
}

struct AccumulateOut { float4 bounce : COLOR0; float4 occlusion : COLOR1; };
AccumulateOut Accumulate(VSOUT IN) {
	AccumulateOut OUT;
	float2 uv = IN.UVCoord;
	float4 current = tex2Dlod(NVR_ALBuffer, float4(uv, 0.0f, 0.0f));
	float viewZ = tex2Dlod(NVR_ALPrep, float4(uv, 0.0f, 0.0f)).x;
	OUT.bounce = float4(current.rgb, viewZ);
	OUT.occlusion = float4(current.a, viewZ, 0.0f, 1.0f);
	[branch] if (!(NVR_ALTemporal.x > 0.0f) || viewZ < NearFade) return OUT;
	float3 origin = PositionFromViewZ(uv, viewZ);
	float4 clip = mul(float4(origin, 1.0f), NVR_ALReproject);
	float w = max(clip.w, 0.001f);
	float3 last = float3(clip.x / w * 0.5f + 0.5f, 0.5f - clip.y / w * 0.5f, clip.w);
	float2 position = last.xy / NVR_ALLayout.zw - 0.5f;
	float2 base = floor(position);
	float2 f = position - base;
	float2 t00 = tex2Dlod(NVR_ALOcclusionHistory, float4((base + 0.5f) * NVR_ALLayout.zw, 0.0f, 0.0f)).rg;
	float2 t10 = tex2Dlod(NVR_ALOcclusionHistory, float4((base + float2(1.5f, 0.5f)) * NVR_ALLayout.zw, 0.0f, 0.0f)).rg;
	float2 t01 = tex2Dlod(NVR_ALOcclusionHistory, float4((base + float2(0.5f, 1.5f)) * NVR_ALLayout.zw, 0.0f, 0.0f)).rg;
	float2 t11 = tex2Dlod(NVR_ALOcclusionHistory, float4((base + 1.5f) * NVR_ALLayout.zw, 0.0f, 0.0f)).rg;
	float4 weight = float4((1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y);
	weight *= abs(float4(t00.g, t10.g, t01.g, t11.g) - last.z) < max(last.z * 0.05f, 4.0f);
	float found = dot(weight, 1.0f);
	float occlusionHistory = dot(float4(t00.r, t10.r, t01.r, t11.r), weight) / max(found, 1.0e-4f);
	float low = current.a, high = current.a;
	[unroll] for (int i = 0; i < 4; i++) {
		float2 offset = (i < 2 ? float2(i * 2 - 1, 0.0f) : float2(0.0f, i * 2 - 5)) * NVR_ALLayout.zw;
		float n = tex2Dlod(NVR_ALBuffer, float4(uv + offset, 0.0f, 0.0f)).a;
		low = min(low, n);
		high = max(high, n);
	}
	float margin = (high - low) * 0.25f;
	occlusionHistory = clamp(occlusionHistory, low - margin, high + margin);
	float inside = all(last.xy > 0.0f) && all(last.xy < 1.0f) && last.z > NearFade;
	float moved = length((last.xy - uv) / NVR_ALLayout.zw);
	float keep = NVR_ALTemporal.x / (1.0f + moved / NVR_ALTemporal.y) * saturate(found * 2.0f) * inside;
	OUT.occlusion = float4(lerp(current.a, occlusionHistory, keep), viewZ, 0.0f, 1.0f);
	return OUT;
}

float4 NVR_GridOrigin;
float4 NVR_GridOrigin1;
float4 NVR_GridData;
float4 NVR_GridLight;
float4x4 NVR_GridSunToGrid;
float4x4 NVR_GridToSun;
sampler2D NVR_GridWave : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridAccum : register(s9) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D NVR_GridSunDepth : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridSunColor : register(s12) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridWaveColor : register(s13) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridAccumColor : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
#define NVR_GridGeometry NVR_ALPrep
float4 NVR_GridStep;
static const float3 GridGroundColor = float3(0.55f, 0.42f, 0.30f);

static const float GridX = 32.0f, GridY = 32.0f, GridZ = 16.0f;
static const float AtlasSize = 128.0f;
static const float2 AtlasDims = float2(256.0f, 128.0f);
static const float SunMapSize = 256.0f;

float4 CosineLobe(float3 n) { return float4(0.886227f, 1.023328f * n); }
float4 ShBasis(float3 d) { return float4(0.282095f, 0.488603f * d); }

float2 CellUV(float3 c, float level = 0.0f) {
	float tile = c.z;
	float2 t = float2(fmod(tile, 4.0f), floor(tile / 4.0f));
	return (t * 32.0f + c.xy + 0.5f + float2(level * AtlasSize, 0.0f)) / AtlasDims;
}

float3 AtlasCell(float2 uv) {
	float2 texel = floor(uv * AtlasDims);
	texel.x -= texel.x >= AtlasSize ? AtlasSize : 0.0f;
	float2 t = floor(texel / 32.0f);
	return float3(texel - t * 32.0f, t.y * 4.0f + t.x);
}

float3 SunMapPosition(float2 uv, float depth) {
	float4 p = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f), NVR_GridSunToGrid);
	return p.xyz / p.w;
}

struct GridOut { float4 wave : COLOR0; float4 accum : COLOR1; float4 waveColor : COLOR2; float4 accumColor : COLOR3; };

#define NVR_GridScreenOld NVR_ALBuffer
#define NVR_GridScreenColorOld NVR_ALHistory
float4 NVR_GridShift;
float4 NVR_GridScreen;

float4 NVR_GridWalls;
#define NVR_GridWallsOld NVR_GridWave
#define NVR_GridWallMemory NVR_GridWaveColor

#define GridLampsMax 32
float4 NVR_GridLamps[GridLampsMax];
float4 NVR_GridLampColors[GridLampsMax];
float4 NVR_GridLampInfo;
static const float GridLampReflectance = 0.4f;

struct ScreenOut { float4 sh : COLOR0; float4 colour : COLOR1; float4 walls : COLOR2; };

ScreenOut ScreenInject(VSOUT IN) {
	ScreenOut OUT;
	[branch] if (IN.UVCoord.x * AtlasDims.x >= AtlasSize) { OUT.sh = 0.0f; OUT.colour = 0.0f; OUT.walls = 0.0f; return OUT; }
	float3 cell = AtlasCell(IN.UVCoord);
	float size = NVR_GridOrigin.w;
	float3 oldCell = cell + NVR_GridShift.xyz;
	bool had = NVR_GridShift.w > 0.5f && all(oldCell >= 0.0f) && oldCell.x < GridX && oldCell.y < GridY && oldCell.z < GridZ;
	OUT.sh = had ? tex2Dlod(NVR_GridScreenOld, float4(CellUV(oldCell), 0.0f, 0.0f)) : 0.0f;
	OUT.colour = had ? tex2Dlod(NVR_GridScreenColorOld, float4(CellUV(oldCell), 0.0f, 0.0f)) : 0.0f;
	OUT.walls = had && NVR_GridWalls.z > 0.5f ? tex2Dlod(NVR_GridWallsOld, float4(CellUV(oldCell), 0.0f, 0.0f)) : 0.0f;
	float3 centre = NVR_GridOrigin.xyz + (cell + 0.5f) * size;
	float3 view = mul(float4(centre, 0.0f), TESR_ViewTransform).xyz;
	[branch] if (view.z < 0.5f * size) return OUT;
	float3 screen = projectPosition(view);
	[branch] if (any(screen.xy < 0.0f) || any(screen.xy > 1.0f)) return OUT;
	float2 extent = (0.6f * size) * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) / (2.0f * view.z);
	float3 low = centre - 0.5f * size;
	float4 sh = 0.0f, colour = 0.0f;
	float3 axes = 0.0f;
	float hits = 0.0f, empty = 0.0f;
	[loop] for (int j = 0; j < 4; j++) {
		[loop] for (int i = 0; i < 4; i++) {
			float2 uv = screen.xy + ((float2(i, j) + 0.5f) / 4.0f * 2.0f - 1.0f) * extent;
			if (any(uv < 0.0f) || any(uv > 1.0f)) continue;
			float depth = readDepthLod(uv);
			float3 p = toWorld(uv) * depth;
			float3 inCell = p - low;
			[branch] if (all(inCell >= 0.0f) && all(inCell < size)) {
				float3 light = min(pows(tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0f, 0.0f)).rgb, 2.2f), 4.0f);
				float l = luma(light);
				float3 n = normalize(GetWorldNormalLod(uv));
				sh += CosineLobe(n) * l;
				colour += float4(light, l);
				axes += abs(n);
				hits += 1.0f;
			}
			else if (depth > view.z + size) empty += 1.0f;
		}
	}
	[branch] if (hits + empty < 4.0f) return OUT;
	float share = 1.44f / 16.0f * NVR_GridScreen.x;
	OUT.sh = lerp(OUT.sh, sh * share, 0.5f);
	OUT.colour = lerp(OUT.colour, colour * share, 0.5f);
	OUT.walls = NVR_GridWalls.x > 0.5f ? lerp(OUT.walls, float4(axes * (1.44f / 16.0f), hits / 16.0f), 0.5f) : 0.0f;
	return OUT;
}

GridOut GridInject(VSOUT IN) {
	float3 cell = AtlasCell(IN.UVCoord);
	bool fine = IN.UVCoord.x * AtlasDims.x >= AtlasSize;
	float size = fine ? NVR_GridOrigin1.w : NVR_GridOrigin.w;
	float3 low = (fine ? NVR_GridOrigin1.xyz - NVR_GridOrigin.xyz : 0.0f) + cell * size;
	float4 sh = 0.0f;
	float4 colour = 0.0f;
	float3 normals = 0.0f;
	float count = 0.0f;
	float faceShare = 0.0f;
	[branch] if (NVR_GridStep.z > 0.5f) {
		float4 centre = mul(float4(low + 0.5f * size, 1.0f), NVR_GridToSun);
		float2 centreUV = float2(centre.x * 0.5f + 0.5f, 0.5f - centre.y * 0.5f);
		float span = 1.75f * size / NVR_GridData.z;
		float stepTexels = span / 6.0f;
		[loop] for (int j = 0; j < 6; j++) {
			[loop] for (int i = 0; i < 6; i++) {
				float2 uv = centreUV + (float2(i, j) - 2.5f) * stepTexels / SunMapSize;
				float depth = tex2Dlod(NVR_GridSunDepth, float4(uv, 0.0f, 0.0f)).r;
				if (depth >= 0.9999f) continue;
				float3 p = SunMapPosition(uv, depth);
				float3 inCell = p - low;
				if (any(inCell < 0.0f) || any(inCell >= size)) continue;
				float2 px = 1.0f / SunMapSize;
				float3 px1 = SunMapPosition(uv + float2(px.x, 0.0f), tex2Dlod(NVR_GridSunDepth, float4(uv + float2(px.x, 0.0f), 0.0f, 0.0f)).r);
				float3 py1 = SunMapPosition(uv + float2(0.0f, px.y), tex2Dlod(NVR_GridSunDepth, float4(uv + float2(0.0f, px.y), 0.0f, 0.0f)).r);
				float3 n = cross(px1 - p, py1 - p);
				float len = length(n);
				n = len > 1e-4f ? n / len : float3(0.0f, 0.0f, 1.0f);
				n *= dot(n, TESR_SunDirection.xyz) < 0.0f ? -1.0f : 1.0f;
				float3 albedo = NVR_GridData.w > 0.5f ? pow(tex2Dlod(NVR_GridSunColor, float4(uv, 0.0f, 0.0f)).rgb, 2.2f) : GridGroundColor;
				float3 light = albedo * NVR_GridLight.rgb;
				float l = luma(light);
				sh += CosineLobe(n) * l;
				colour += float4(light, l);
				normals += n;
				count += 1.0f;
			}
		}
		faceShare = (stepTexels * NVR_GridData.z) * (stepTexels * NVR_GridData.z) / (size * size);
		sh *= NVR_GridLight.w * faceShare;
		colour *= faceShare;
	}
	[branch] if (NVR_GridStep.w > 0.5f) {
		sh += tex2Dlod(NVR_GridScreenOld, float4(IN.UVCoord, 0.0f, 0.0f));
		colour += tex2Dlod(NVR_GridScreenColorOld, float4(IN.UVCoord, 0.0f, 0.0f));
	}
	GridOut OUT;
	OUT.wave = sh;
	float normalLength = length(normals);
	float cover = count * faceShare;
	OUT.accum = float4(cover * abs(normalLength > 1e-4f ? normals / normalLength : float3(0.0f, 0.0f, 1.0f)), cover);
	[branch] if (NVR_GridWalls.y > 0.5f) OUT.accum = max(OUT.accum, tex2Dlod(NVR_GridWallMemory, float4(IN.UVCoord, 0.0f, 0.0f)));
	[branch] if (NVR_GridLampInfo.x > 0.5f && any(OUT.accum.xyz > 0.0f)) {
		float3 centre = low + 0.5f * size;
		[loop] for (int k = 0; k < GridLampsMax; k++) {
			if (k >= (int)NVR_GridLampInfo.x) break;
			float4 lamp = NVR_GridLamps[k];
			float3 v = (lamp.xyz - NVR_GridOrigin.xyz) - centre;
			float d = length(v);
			[branch] if (d >= lamp.w + 0.5f * size) continue;
			float3 dir = d > 1e-3f ? v / d : float3(0.0f, 0.0f, 1.0f);
			float fall = saturate(1.0f - max(d - 0.5f * size, 0.0f) / lamp.w);
			fall *= fall;
			float through = 1.0f;
			[branch] if (NVR_GridWalls.y > 0.5f && NVR_GridLampInfo.z > 0.0f && d > size) {
				[unroll] for (int s = 1; s <= 3; s++) {
					float3 c = floor((centre + v * (s / 4.0f)) / NVR_GridOrigin.w);
					if (any(c < 0.0f) || c.x >= GridX || c.y >= GridY || c.z >= GridZ) continue;
					float3 w = tex2Dlod(NVR_GridWallMemory, float4(CellUV(c), 0.0f, 0.0f)).xyz;
					through *= 1.0f - saturate(dot(w, abs(dir)) * NVR_GridLampInfo.z);
				}
			}
			float facing = min(dot(OUT.accum.xyz, abs(dir)), 1.0f);
			float3 light = NVR_GridLampColors[k].rgb * (fall * through * facing * GridLampReflectance * NVR_GridLampInfo.y);
			float l = luma(light);
			sh += CosineLobe(dir) * l;
			colour += float4(light, l);
		}
		OUT.wave = sh;
		OUT.waveColor = colour;
	}
	OUT.waveColor = colour;
	OUT.accumColor = 0.0f;
	return OUT;
}

GridOut GridPropagate(VSOUT IN) {
	float3 cell = AtlasCell(IN.UVCoord);
	float level = IN.UVCoord.x * AtlasDims.x >= AtlasSize ? 1.0f : 0.0f;
	float4 sh = 0.0f;
	float4 colour = 0.0f;
	float4 blocker = tex2Dlod(NVR_GridGeometry, float4(IN.UVCoord, 0.0f, 0.0f));
	[unroll] for (int k = 0; k < 6; k++) {
		float3 axis = k == 0 ? float3(1, 0, 0) : k == 1 ? float3(-1, 0, 0) : k == 2 ? float3(0, 1, 0) : k == 3 ? float3(0, -1, 0) : k == 4 ? float3(0, 0, 1) : float3(0, 0, -1);
		float3 neighbour = cell - axis;
		if (any(neighbour < 0.0f) || neighbour.x >= GridX || neighbour.y >= GridY || neighbour.z >= GridZ) continue;
		float4 from = tex2Dlod(NVR_GridWave, float4(CellUV(neighbour, level), 0.0f, 0.0f));
		float sent = max(dot(from, ShBasis(axis)), 0.0f);
		sent *= 1.0f - saturate(dot(blocker.xyz, abs(axis)) * NVR_GridStep.y);
		sh += CosineLobe(axis) * (sent * NVR_GridData.y);
		float4 fromColour = tex2Dlod(NVR_GridWaveColor, float4(CellUV(neighbour, level), 0.0f, 0.0f));
		colour += float4(fromColour.rgb / max(fromColour.a, 1e-5f), 1.0f) * (sent * NVR_GridData.y);
	}
	GridOut OUT;
	OUT.wave = sh;
	OUT.accum = (NVR_GridStep.x > 0.5f ? 0.0f : tex2Dlod(NVR_GridAccum, float4(IN.UVCoord, 0.0f, 0.0f))) + sh;
	OUT.waveColor = colour;
	OUT.accumColor = tex2Dlod(NVR_GridAccumColor, float4(IN.UVCoord, 0.0f, 0.0f)) + colour;
	return OUT;
}

float4 GridLevel(float3 cameraRelative, float3 normal, float4 origin, float level, float edgeCells, out float4 colourLight, out float fade) {
	float size = origin.w;
	float3 g = (cameraRelative - origin.xyz + normal * (0.5f * size)) / size - 0.5f;
	float3 dims = float3(GridX, GridY, GridZ);
	float3 edge = min(g + 0.5f, dims - 0.5f - g);
	fade = saturate(min(min(edge.x, edge.y), edge.z) / edgeCells);
	colourLight = 0.0f;
	[branch] if (fade <= 0.0f) return 0.0f;
	g = clamp(g, 0.0f, dims - 1.0f);
	float z0 = floor(g.z);
	float z1 = min(z0 + 1.0f, GridZ - 1.0f);
	float fz = g.z - z0;
	float4 a = tex2Dlod(NVR_GridAccum, float4(CellUV(float3(g.xy, z0), level), 0.0f, 0.0f));
	float4 b = tex2Dlod(NVR_GridAccum, float4(CellUV(float3(g.xy, z1), level), 0.0f, 0.0f));
	[branch] if (NVR_GridData.w > 0.5f) {
		float4 ca = tex2Dlod(NVR_GridAccumColor, float4(CellUV(float3(g.xy, z0), level), 0.0f, 0.0f));
		float4 cb = tex2Dlod(NVR_GridAccumColor, float4(CellUV(float3(g.xy, z1), level), 0.0f, 0.0f));
		colourLight = lerp(ca, cb, fz);
	}
	return lerp(a, b, fz);
}

float3 GridBounce(float3 cameraRelative, float3 normal) {
	float4 c, cFine;
	float fade, fadeFine;
	float4 sh = GridLevel(cameraRelative, normal, NVR_GridOrigin, 0.0f, 2.0f, c, fade);
	[branch] if (NVR_GridOrigin1.w > 0.0f) {
		float4 shFine = GridLevel(cameraRelative, normal, NVR_GridOrigin1, 1.0f, 4.0f, cFine, fadeFine);
		sh = lerp(sh, shFine, fadeFine);
		c = lerp(c, cFine, fadeFine);
		fade = max(fade, fadeFine);
	}
	[branch] if (fade <= 0.0f) return 0.0f;
	float3 colour = 1.0f;
	[branch] if (NVR_GridData.w > 0.5f) {
		colour = c.a > 1e-5f ? lerp(1.0f, c.rgb / c.a, NVR_ALScene.w) : 1.0f;
	}
	float irradiance = max(dot(sh, CosineLobe(-normal)), 0.0f) * 1.5f;
	return irradiance * colour * fade;
}

static const float MaxReflectance = 0.6f;
float3 SurfaceColour(float3 lin) {
	float3 ambient = max(NVR_ALScene.rgb, 0.25f * luma(NVR_ALScene.rgb) + 1.0e-4f);
	float3 raw = max(lin, 0.0f) / ambient;
	float level = luma(raw);
	float3 hue = raw / max(level, 1.0e-4f);
	float trust = saturate(MaxReflectance / max(level, 1.0e-4f));
	return min(level, MaxReflectance) * min(lerp(1.0f, hue, trust), 3.0f);
}

#define NVR_ALStats NVR_ALBuffer
#define NVR_ALBalance NVR_ALHistory
static const float2 StatsSize = float2(16.0f, 9.0f);

float3 StatsAt(float2 uv) {
	float3 lin = linearize(tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0f, 0.0f)).rgb);
	float depth = readDepthLod(uv);
	[branch] if (depth < NearFade || depth > DrawDistance || IsViewModel(uv)) return float3(luma(lin), 0.0f, 0.0f);
	float ao = lerp(DarkestAO, 1.0f, tex2Dlod(NVR_ALOcclusionHistory, float4(uv, 0.0f, 0.0f)).r);
	ao = lerp(ao, 1.0f, saturate((luma(lin) - BrightFade) * 3.0f));
	float3 albedo = SurfaceColour(lin);
	float3 normal = normalize(GetWorldNormalLod(uv));
	float3 bounce = GridBounce(toWorld(uv) * depth, normal) * (NVR_GridData.x * ao) * albedo;
	float3 taken = (1.0f - ao) * min(lin, albedo * NVR_ALScene.rgb);
	return float3(luma(lin), luma(taken), luma(bounce));
}

float4 StatsSample(VSOUT IN) : COLOR0 {
	float2 cell = floor(IN.UVCoord * StatsSize);
	float3 sum = 0.0f;
	[unroll] for (int j = 0; j < 2; j++) {
		[unroll] for (int i = 0; i < 2; i++) sum += StatsAt((cell * 2.0f + float2(i, j) + 0.5f) / (StatsSize * 2.0f));
	}
	return float4(sum, 1.0f);
}

float4 StatsReduce(VSOUT IN) : COLOR0 {
	float3 total = 0.0f;
	[loop] for (int j = 0; j < 9; j++) {
		[loop] for (int i = 0; i < 16; i++) total += tex2Dlod(NVR_ALStats, float4((float2(i, j) + 0.5f) / StatsSize, 0.0f, 0.0f)).xyz;
	}
	float scale = saturate(NVR_ALInterior.z * total.y / max(total.z, 1.0e-6f));
	float last = tex2Dlod(NVR_ALBalance, float4(0.5f, 0.5f, 0.0f, 0.0f)).x;
	scale = NVR_ALInterior.y > 0.5f ? lerp(scale, last, NVR_ALInterior.w) : scale;
	return float4(scale, total.yzx / (StatsSize.x * StatsSize.y * 4.0f));
}

sampler2D NVR_ALSkyVis : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
float2 SkyFactorAt(float2 uv, float depth) {
	[branch] if (!(depth < endFade)) return 1.0f;
	float2 position = uv / NVR_ALLayout.zw - 0.5f;
	float2 base = floor(position);
	float2 fraction = position - base;
	float2 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float2 tapUV = clamp((base + float2(x, y) + 0.5f) * NVR_ALLayout.zw, 0.5f * NVR_ALLayout.zw, 1.0f - 0.5f * NVR_ALLayout.zw);
			float3 tap = tex2Dlod(NVR_ALSkyVis, float4(tapUV, 0.0f, 0.0f)).rgb;
			float weight = (x ? fraction.x : 1.0f - fraction.x) * (y ? fraction.y : 1.0f - fraction.y);
			weight /= 1.0f + abs(tap.y - depth) / max(TESR_AmbientOcclusionData.z, 0.001f);
			sum += tap.xz * weight;
			weights += weight;
		}
	}
	return saturate(sum / max(weights, 1.0e-6f));
}

float4 Apply(VSOUT IN, uniform bool sky) : COLOR0 {
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	[branch] if (NVR_ALFlags.x > 0.5f) color = float4(SunShadowApply(color.rgb, IN.UVCoord), 1.0f);
	float depth = readDepth(IN.UVCoord);
	float2 skyFactor = 1.0f;
	if (sky) skyFactor = SkyFactorAt(IN.UVCoord, depth);
	[branch] if (depth < NearFade || depth > DrawDistance || IsViewModel(IN.UVCoord)) {
		if (sky) color.rgb = pows(SkyTake(pows(color.rgb, 2.2f), skyFactor.x, skyFactor.y, TESR_AmbientOcclusionSky5.x, TESR_AmbientOcclusionSky5.w), 1.0f / 2.2f);
		return DebugView > 0.5f ? float4(0.0f, 0.0f, 0.0f, 1.0f) : color;
	}

	float2 position = IN.UVCoord / NVR_ALLayout.zw - 0.5f;
	float2 base = floor(position);
	float2 fraction = position - base;
	float tolerance = max(depth * 0.03f, 2.0f);
	float4 sum = 0.0f;
	float weights = 1.0e-5f;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float2 tapUV = (base + float2(x, y) + 0.5f) * NVR_ALLayout.zw;
			float tapDepth = readDepthLod(tapUV);
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1.0f + abs(tapDepth - depth) / tolerance;
			sum.a += tex2Dlod(NVR_ALOcclusionHistory, float4(tapUV, 0.0f, 0.0f)).r * weight;
			weights += weight;
		}
	}
	float4 ambient = sum / weights;

	float3 lin = linearize(color.rgb);
	float ao = lerp(DarkestAO, 1.0f, ambient.a);
	ao = lerp(ao, 1.0f, saturate((luma(lin) - BrightFade) * 3.0f));
	float2 px = TESR_ReciprocalResolution.xy;
	float left = readDepthLod(IN.UVCoord - float2(px.x, 0.0f)), right = readDepthLod(IN.UVCoord + float2(px.x, 0.0f));
	float up = readDepthLod(IN.UVCoord - float2(0.0f, px.y)), down = readDepthLod(IN.UVCoord + float2(0.0f, px.y));
	float bendX = abs(left + right - 2.0f * depth);
	float bendY = abs(up + down - 2.0f * depth);
	[branch] if (NVR_ALFlags.y > 0.5f) {
		float left2 = readDepthLod(IN.UVCoord - float2(2.0f * px.x, 0.0f)), right2 = readDepthLod(IN.UVCoord + float2(2.0f * px.x, 0.0f));
		float up2 = readDepthLod(IN.UVCoord - float2(0.0f, 2.0f * px.y)), down2 = readDepthLod(IN.UVCoord + float2(0.0f, 2.0f * px.y));
		bendX = min(bendX, min(abs(left2 + depth - 2.0f * left), abs(depth + right2 - 2.0f * right)));
		bendY = min(bendY, min(abs(up2 + depth - 2.0f * up), abs(depth + down2 - 2.0f * down)));
	}
	float busy = saturate((bendX + bendY) / depth * 20.0f - 0.1f);
	[branch] if (DebugView > 11.5f) {
		float3 c = floor((toWorld(IN.UVCoord) * depth - NVR_GridOrigin.xyz) / NVR_GridOrigin.w);
		float4 m = tex2Dlod(NVR_GridWave, float4(CellUV(clamp(c, 0.0f, float3(GridX, GridY, GridZ) - 1.0f)), 0.0f, 0.0f));
		return float4(saturate(m.xyz * NVR_GridStep.y), 1.0f);
	}
	[branch] if (DebugView > 10.5f) return float4(busy.xxx, 1.0f);
	ao = lerp(ao, lerp(1.0f, ao, 0.25f), busy);
	float3 albedo = SurfaceColour(lin);
	float3 gridBounce = 0.0f;
	[branch] if (NVR_GridData.x > 0.0f) {
		float3 cameraRelative = toWorld(IN.UVCoord) * depth;
		float3 normal = normalize(GetWorldNormalLod(IN.UVCoord));
		gridBounce = GridBounce(cameraRelative, normal) * (NVR_GridData.x * ao);
	}
	[branch] if (DebugView > 8.5f) return delinearize(float4(DebugView < 9.5f ? gridBounce : albedo, 1.0f));
	[branch] if (NVR_ALInterior.x > 0.5f) gridBounce *= tex2Dlod(NVR_ALBalance, float4(0.5f, 0.5f, 0.0f, 0.0f)).x;
	[branch] if (DebugView > 7.5f) {
		float3 cameraRelative = toWorld(IN.UVCoord) * depth;
		float3 c = floor((cameraRelative - NVR_GridOrigin.xyz) / NVR_GridOrigin.w);
		float4 m = tex2Dlod(NVR_GridWave, float4(CellUV(clamp(c, 0.0f, float3(GridX, GridY, GridZ) - 1.0f)), 0.0f, 0.0f));
		return float4(saturate(m.x * 4.0f), m.x != 0.0f ? 0.5f : 0.0f, saturate(gridBounce.b * 4.0f), 1.0f);
	}
	[branch] if (DebugView > 6.5f) return delinearize(float4(gridBounce * albedo, 1.0f));
	[branch] if (DebugView > 2.5f) return float4(tex2Dlod(NVR_ALBuffer, float4(IN.UVCoord, 0.0f, 0.0f)).rgb, 1.0f);
	[branch] if (DebugView > 0.5f) return delinearize(float4(DebugView < 1.5f ? ao.xxx : gridBounce * albedo, 1.0f));
	[branch] if (NVR_ALInterior.x > 0.5f) lin = lin - (1.0f - ao) * min(lin, albedo * NVR_ALScene.rgb) + gridBounce * albedo;
	else {
		float3 lit = lin * ao;
		if (sky) lit = SkyTake(lit, skyFactor.x, skyFactor.y, TESR_AmbientOcclusionSky5.x, TESR_AmbientOcclusionSky5.w);
		lin = lit + gridBounce * albedo;
	}
	return delinearize(float4(lin, color.a));
}

technique AmbientLight {
	pass Prepare { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Prepare(); }
	pass Estimate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Estimate(); }
	pass BlurX { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Accumulate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Accumulate(); }
	pass StatsSample { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 StatsSample(); }
	pass StatsReduce { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 StatsReduce(); }
	pass Apply { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Apply(false); }

}

technique AmbientLightGrid {
	pass ScreenInject { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 ScreenInject(); }
	pass GridInject { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 GridInject(); }
	pass GridPropagate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 GridPropagate(); }
}

technique AmbientLightSky {
	pass Prepare { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Prepare(); }
	pass Estimate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Estimate(); }
	pass BlurX { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Accumulate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Accumulate(); }
	pass StatsSample { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 StatsSample(); }
	pass StatsReduce { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 StatsReduce(); }
	pass Apply { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Apply(true); }
}

technique AmbientLightSmooth {
	pass SmoothX { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 SmoothNormal(false); }
	pass SmoothY { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 SmoothNormal(true); }
}
