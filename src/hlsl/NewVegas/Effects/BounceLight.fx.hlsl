float4 TESR_BounceLightData;
float4 TESR_ReciprocalResolution;
float4 NVR_BounceLayout;
float4 NVR_BounceTemporal;
float4 NVR_BounceAmbient;
float4x4 NVR_BounceReproject;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D TESR_BlueNoiseSampler : register(s4) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D NVR_BounceBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_BouncePrep : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_BounceHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

static const float Strength = TESR_BounceLightData.x;
static const float Radius = max(TESR_BounceLightData.y, 1.0f);
static const float DrawDistance = TESR_BounceLightData.z;
static const float BlurRadius = TESR_BounceLightData.w;
static const int SAMPLES = 8;
static const float NearFade = 40.0f;
static const float MaxScreenRadius = 0.06f;

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

VSOUT HalfVS(VSIN IN)
{
	VSOUT OUT = FrameVS(IN);
	OUT.UVCoord += 0.5f * (NVR_BounceLayout.zw - TESR_ReciprocalResolution.xy);
	return OUT;
}

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Blur.hlsl"

float3 NormalLod(float2 uv)
{
	return normalize(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2 - 1);
}

float3 PositionFromLinear(float2 uv, float linear01)
{
	float z = projectedDepthFromLinear(linear01);
	float4 viewSpace = mul(float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, z, 1.0f), TESR_InvProjectionTransform);
	return viewSpace.xyz / viewSpace.w;
}

float Pack2(float2 v)
{
	v = floor(saturate(v) * 4095.0f + 0.5f);
	return v.x * 4096.0f + v.y;
}

float2 Unpack2(float p)
{
	float high = floor(p / 4096.0f);
	return float2(high, p - high * 4096.0f) / 4095.0f;
}

float2 OctEncode(float3 n)
{
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	float2 s = n.xy >= 0.0f ? 1.0f : -1.0f;
	float2 e = n.z >= 0.0f ? n.xy : (1.0f - abs(n.yx)) * s;
	return e * 0.5f + 0.5f;
}

float3 OctDecode(float2 e)
{
	e = e * 2.0f - 1.0f;
	float3 n = float3(e, 1.0f - abs(e.x) - abs(e.y));
	float t = saturate(-n.z);
	n.xy += n.xy >= 0.0f ? -t : t;
	return normalize(n);
}

float4 Prepare(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float linear01 = tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x;
	float3 normal = NormalLod(uv);
	float3 light = min(linearize(tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0f, 0.0f)).rgb), 1.0f);
	return float4(linear01, Pack2(OctEncode(normal)), Pack2(sqrt(light.rg)), light.b);
}

float4 Gather(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 center = tex2Dlod(NVR_BouncePrep, float4(uv, 0.0f, 0.0f));
	float3 origin = PositionFromLinear(uv, center.x);
	float fade = smoothstep(NearFade, NearFade * 2.0f, origin.z) * (1.0f - smoothstep(DrawDistance * 0.7f, DrawDistance, origin.z));
	[branch] if (fade <= 0.0f) return float4(0.0f, 0.0f, 0.0f, origin.z);

	float3 normal = OctDecode(Unpack2(center.y));
	float3 noise = tex2Dlod(TESR_BlueNoiseSampler, float4(uv / (256.0f * NVR_BounceLayout.zw), 0.0f, 0.0f)).xyz;
	float3 up = abs(normal.z) < 0.99f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
	float3 tangent = normalize(cross(up, normal));
	float3 bitangent = cross(normal, tangent);
	float radius = min(Radius, MaxScreenRadius * 2.0f * origin.z / TESR_ProjectionTransform[0][0]);

	float3 indirect = 0.0f;
	[unroll]
	for (int i = 0; i < SAMPLES; i++) {
		float angle = (i + noise.x) * (6.2831853f / SAMPLES);
		float r = radius * (0.15f + 0.85f * frac(noise.y + i * 0.618034f));
		float3 p = origin + (tangent * cos(angle) + bitangent * sin(angle)) * r + normal * (r * 0.25f);
		float2 sampleUV = projectPosition(p).xy;
		float onScreen = step(0.0f, sampleUV.x) * step(sampleUV.x, 1.0f) * step(0.0f, sampleUV.y) * step(sampleUV.y, 1.0f);
		sampleUV = (floor(saturate(sampleUV) / NVR_BounceLayout.zw) + 0.5f) * NVR_BounceLayout.zw;

		float4 texel = tex2Dlod(NVR_BouncePrep, float4(sampleUV, 0.0f, 0.0f));
		float3 s = PositionFromLinear(sampleUV, texel.x);
		float3 toSample = s - origin;
		float dist = length(toSample) + 0.001f;
		float3 dir = toSample / dist;
		float nearAim = saturate(1.0f - length(s - p) / radius);
		float weight = saturate(dot(normal, dir)) * saturate(dot(OctDecode(Unpack2(texel.y)), -dir)) * nearAim * onScreen * step(NearFade, s.z);
		float2 rg = Unpack2(texel.z);
		indirect += float3(rg * rg, texel.w) * weight;
	}
	return float4(indirect * (2.0f / SAMPLES) * fade, origin.z);
}

float4 Blur(VSOUT IN, uniform float2 axis) : COLOR0
{
	float4 center = tex2Dlod(NVR_BounceBuffer, float4(IN.UVCoord, 0.0f, 0.0f));
	float tolerance = max(center.a * 0.03f, 2.0f);
	float3 sum = center.rgb * 0.114725602f;
	float weights = 0.114725602f;
	[unroll]
	for (int i = 0; i < cKernelSize; i++) {
		float2 uv = IN.UVCoord + BlurOffsets[i] * axis * BlurRadius * (NVR_BounceLayout.zw / TESR_ReciprocalResolution.xy);
		float4 tap = tex2Dlod(NVR_BounceBuffer, float4(uv, 0.0f, 0.0f));
		float weight = BlurWeights[i] * (abs(tap.a - center.a) <= tolerance);
		sum += tap.rgb * weight;
		weights += weight;
	}
	return float4(sum / weights, center.a);
}

float4 Combine(VSOUT IN) : COLOR0
{
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	float depth = readDepth(IN.UVCoord);
	[branch] if (depth < NearFade || depth > DrawDistance) return color;

	float2 position = IN.UVCoord / NVR_BounceLayout.zw - 0.5f;
	float2 base = floor(position);
	float2 fraction = position - base;
	float tolerance = max(depth * 0.03f, 2.0f);
	float3 sum = 0.0f;
	float weights = 1.0e-5f;
	[unroll] for (int y = 0; y < 2; y++) {
		[unroll] for (int x = 0; x < 2; x++) {
			float4 tap = tex2Dlod(NVR_BounceBuffer, float4((base + float2(x, y) + 0.5f) * NVR_BounceLayout.zw, 0.0f, 0.0f));
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1.0f + abs(tap.a - depth) / tolerance;
			sum += tap.rgb * weight;
			weights += weight;
		}
	}
	float3 indirect = sum / weights;

	float3 lin = linearize(color.rgb);
	float peak = max(max(lin.r, lin.g), max(lin.b, 0.0001f));
	float3 albedo = lin / peak * 0.6f;
	lin += Strength * indirect * albedo;
	return delinearize(float4(lin, color.a));
}

float3 LastFrame(float3 position)
{
	float4 clip = mul(float4(position, 1.0f), NVR_BounceReproject);
	float w = max(clip.w, 0.001f);
	return float3(clip.x / w * 0.5f + 0.5f, 0.5f - clip.y / w * 0.5f, clip.w);
}

float DepthTolerance(float3 position, float3 normal)
{
	float facing = max(abs(dot(normal, normalize(position))), 0.05f);
	float texel = 2.0f * NVR_BounceLayout.z / TESR_ProjectionTransform[0][0];
	return position.z * (0.03f + 2.0f * texel * sqrt(1.0f - facing * facing) / facing) + 4.0f;
}

float HistoryAt(float3 last, float tolerance, out float3 history)
{
	float2 position = last.xy / NVR_BounceLayout.zw - 0.5f;
	float2 base = floor(position);
	float2 f = position - base;
	float4 t00 = tex2Dlod(NVR_BounceHistory, float4((base + 0.5f) * NVR_BounceLayout.zw, 0.0f, 0.0f));
	float4 t10 = tex2Dlod(NVR_BounceHistory, float4((base + float2(1.5f, 0.5f)) * NVR_BounceLayout.zw, 0.0f, 0.0f));
	float4 t01 = tex2Dlod(NVR_BounceHistory, float4((base + float2(0.5f, 1.5f)) * NVR_BounceLayout.zw, 0.0f, 0.0f));
	float4 t11 = tex2Dlod(NVR_BounceHistory, float4((base + 1.5f) * NVR_BounceLayout.zw, 0.0f, 0.0f));
	float4 w = float4((1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y);
	w *= abs(float4(t00.a, t10.a, t01.a, t11.a) - last.z) < tolerance;
	float sum = dot(w, 1.0f);
	history = (t00.rgb * w.x + t10.rgb * w.y + t01.rgb * w.z + t11.rgb * w.w) / max(sum, 1.0e-4f);
	float inside = all(last.xy > 0.0f) && all(last.xy < 1.0f) && last.z > NearFade;
	return inside ? sum : 0.0f;
}

float4 PreparePlus(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float linear01 = tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x;
	float3 normal = NormalLod(uv);
	float3 scene = linearize(tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0f, 0.0f)).rgb);
	float3 light = min(scene, 1.0f);
	[branch] if (NVR_BounceTemporal.y > 0.0f) {
		float3 position = PositionFromLinear(uv, linear01);
		float3 last;
		float found = HistoryAt(LastFrame(position), DepthTolerance(position, normal), last);
		[branch] if (found > 0.0f) {
			float peak = max(max(scene.r, scene.g), max(scene.b, 0.0001f));
			light = min(light + (NVR_BounceTemporal.y * saturate(found * 2.0f) * Strength * 0.6f / peak) * last * scene, 1.0f);
		}
	}
	return float4(linear01, Pack2(OctEncode(normal)), Pack2(sqrt(light.rg)), light.b);
}

float4 GatherPlus(VSOUT IN, uniform int samples, uniform bool temporal, uniform float motionPixels) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 center = tex2Dlod(NVR_BouncePrep, float4(uv, 0.0f, 0.0f));
	float3 origin = PositionFromLinear(uv, center.x);
	float fade = smoothstep(NearFade, NearFade * 2.0f, origin.z) * (1.0f - smoothstep(DrawDistance * 0.7f, DrawDistance, origin.z));
	[branch] if (fade <= 0.0f) return float4(0.0f, 0.0f, 0.0f, origin.z);

	float3 normal = OctDecode(Unpack2(center.y));
	float3 noise = tex2Dlod(TESR_BlueNoiseSampler, float4(uv / (256.0f * NVR_BounceLayout.zw), 0.0f, 0.0f)).xyz;
	if (temporal) noise.xy = frac(noise.xy + NVR_BounceTemporal.z * float2(0.618034f, 0.754878f));
	float3 up = abs(normal.z) < 0.99f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
	float3 tangent = normalize(cross(up, normal));
	float3 bitangent = cross(normal, tangent);
	float radius = min(Radius, MaxScreenRadius * 2.0f * origin.z / TESR_ProjectionTransform[0][0]);

	float3 indirect = 0.0f;
	float seenWeight = 0.0f, seen = 0.0f;
	[unroll]
	for (int i = 0; i < samples; i++) {
		float angle = (i + noise.x) * (6.2831853f / samples);
		float r = radius * (0.15f + 0.85f * frac(noise.y + i * 0.618034f));
		float3 p = origin + (tangent * cos(angle) + bitangent * sin(angle)) * r + normal * (r * 0.25f);
		float2 sampleUV = projectPosition(p).xy;
		float onScreen = step(0.0f, sampleUV.x) * step(sampleUV.x, 1.0f) * step(0.0f, sampleUV.y) * step(sampleUV.y, 1.0f);
		sampleUV = (floor(saturate(sampleUV) / NVR_BounceLayout.zw) + 0.5f) * NVR_BounceLayout.zw;

		float4 texel = tex2Dlod(NVR_BouncePrep, float4(sampleUV, 0.0f, 0.0f));
		float3 s = PositionFromLinear(sampleUV, texel.x);
		float3 toSample = s - origin;
		float dist = length(toSample) + 0.001f;
		float3 dir = toSample / dist;
		float nearAim = saturate(1.0f - length(s - p) / radius);
		float visible = onScreen * step(NearFade, s.z);
		float weight = saturate(dot(normal, dir)) * saturate(dot(OctDecode(Unpack2(texel.y)), -dir)) * nearAim * visible;
		float2 rg = Unpack2(texel.z);
		indirect += float3(rg * rg, texel.w) * weight;
		seenWeight += weight;
		seen += visible;
	}
	float3 seenLight = indirect / max(seenWeight, 1.0e-4f);
	float unknownWeight = (samples - seen) * seenWeight / max(seen, 1.0f);
	indirect += lerp(NVR_BounceAmbient.rgb, seenLight, seen / samples) * (unknownWeight * NVR_BounceAmbient.w);
	float3 result = indirect * (2.0f / samples) * fade;
	if (temporal) {
		float3 last;
		float3 where = LastFrame(origin);
		float found = HistoryAt(where, DepthTolerance(origin, normal), last);
		float moved = length((where.xy - uv) / NVR_BounceLayout.zw);
		result = lerp(result, last, NVR_BounceTemporal.x / (1.0f + moved / motionPixels) * saturate(found * 2.0f));
	}
	return float4(result, origin.z);
}

technique
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Prepare(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Gather(); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}

technique BounceLightPlus
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 PreparePlus(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 GatherPlus(8, false, 1.0f); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}

technique BounceLightTemporal
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 PreparePlus(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 GatherPlus(8, true, 4.0f); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}

technique BounceLightTemporalHalf
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 PreparePlus(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 GatherPlus(4, true, 8.0f); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}
