// UNOFFICIAL, optional (off by default): one bounce of indirect light, gathered from the screen.
// Written from scratch for this build, after the general idea of screen-space indirect lighting.
//
// Passes 1-4 run at half (QuarterResolution: quarter) resolution into the effect's own buffers (BounceLight.cpp).
// Pass 1 (Prepare): depth, normal and colour of each low-resolution pixel packed into one texel (see Prepare).
// Pass 2 (Gather, rgb = bounced light, a = view depth of the pixel it belongs to): for each pixel, 8 points on a disc around it in its tangent plane (lifted slightly off the
// surface), each projected to the screen. The surface found there sends the light it shows on screen back toward
// the pixel, weighted by how much each faces the other and by how close it is to the point aimed at. Rotated per
// pixel by blue noise. The disc is kept within a small part of the screen: close up, a full-size disc would read
// pixels scattered over the whole screen, which is very slow (texture cache misses) and pulls in unrelated colour.
// Passes 3-4: depth-aware blur, horizontal then vertical, to smooth out the noise.
// Pass 5 (Combine, full resolution): depth-aware upsample, then the bounced light added to the scene, tinted by
// the receiving surface's own hue (there is no albedo buffer, so the hue of the pixel stands in for it).
//
// Screen-space limits: only light from things visible on screen bounces, and it fades at the screen edges.
// The first-person weapon and anything touching the camera neither receive nor send bounce light.

float4 TESR_BounceLightData; // x: strength, y: radius (world units), z: draw distance, w: blur radius (pixels)
float4 TESR_ReciprocalResolution;
float4 NVR_BounceLayout; // zw: 1 / half-resolution buffer size

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D TESR_BlueNoiseSampler : register(s4) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D NVR_BounceBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_BouncePrep : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

static const float Strength = TESR_BounceLightData.x;
static const float Radius = max(TESR_BounceLightData.y, 1.0f);
static const float DrawDistance = TESR_BounceLightData.z;
static const float BlurRadius = TESR_BounceLightData.w;
static const int SAMPLES = 8;
static const float NearFade = 40.0f;      // view depth (world units) below which nothing bounces: the weapon, the camera's own body
static const float MaxScreenRadius = 0.06f; // largest disc on screen, as a fraction of the screen width

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

// Low-resolution passes: the CPU sets a half- or quarter-size viewport over a target of that size; move the texture
// coordinate to the centre of the low-resolution texel.
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

// View-space position from a screen position and CombineDepth's linear depth (view z / far z).
float3 PositionFromLinear(float2 uv, float linear01)
{
	float z = projectedDepthFromLinear(linear01);
	float4 viewSpace = mul(float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, z, 1.0f), TESR_InvProjectionTransform);
	return viewSpace.xyz / viewSpace.w;
}

// Two values in [0, 1] as 12-bit integers in one 32-bit float (exact: the result stays below 2^24).
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

// A unit vector folded onto an octahedron and unfolded into a square: two numbers in [0, 1].
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

// Pass 1 (Prepare): everything the gather needs about one low-resolution pixel, packed into a single texel:
// x = linear depth, y = normal, z = colour red and green (square-root encoded), w = colour blue. The gather then
// reads one small, compact buffer per sample instead of three full-resolution ones (depth, normals, scene), which
// is what made it slow: its samples land far apart, so nearly every read was a texture cache miss.
float4 Prepare(VSOUT IN) : COLOR0
{
	float2 uv = IN.UVCoord;
	float linear01 = tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x;
	float3 normal = NormalLod(uv);
	// Capped so small bright glints (specular highlights, lamps) do not spray light around.
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
	// A disc of r world units at depth z spans r * P11 / (2 z) of the screen width.
	float radius = min(Radius, MaxScreenRadius * 2.0f * origin.z / TESR_ProjectionTransform[0][0]);

	float3 indirect = 0.0f;
	[unroll]
	for (int i = 0; i < SAMPLES; i++) {
		float angle = (i + noise.x) * (6.2831853f / SAMPLES);
		float r = radius * (0.15f + 0.85f * frac(noise.y + i * 0.618034f));
		float3 p = origin + (tangent * cos(angle) + bitangent * sin(angle)) * r + normal * (r * 0.25f);
		float2 sampleUV = projectPosition(p).xy;
		float onScreen = step(0.0f, sampleUV.x) * step(sampleUV.x, 1.0f) * step(0.0f, sampleUV.y) * step(sampleUV.y, 1.0f);
		// Snapped to the centre of the low-resolution pixel whose depth, normal and colour are read.
		sampleUV = (floor(saturate(sampleUV) / NVR_BounceLayout.zw) + 0.5f) * NVR_BounceLayout.zw;

		float4 texel = tex2Dlod(NVR_BouncePrep, float4(sampleUV, 0.0f, 0.0f));
		float3 s = PositionFromLinear(sampleUV, texel.x);
		float3 toSample = s - origin;
		float dist = length(toSample) + 0.001f;
		float3 dir = toSample / dist;
		// The surface found must lie near the point aimed at. Something far in front of it (a pole, the weapon)
		// is another object that does not face this pixel, and would otherwise leave a coloured halo around it.
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
		// BlurOffsets are in full-resolution pixels; this buffer is half or quarter resolution.
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

	// Depth-aware bilinear upsample: the four nearest half-resolution texels, the ones at another depth
	// (across an object's edge) counted less, so the bounce does not bleed across silhouettes.
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

technique
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Prepare(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Gather(); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}
