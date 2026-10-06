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
//
// UNOFFICIAL additions, each switchable ([Shaders.BounceLight.Main] OffScreenLight, MultiBounce, Temporal, HalfSamples);
// with all of them off BounceLight.cpp runs the first technique, exactly as before. The other techniques have the same
// five passes, buffers and result (so Specular's SpecularBounce combine takes them unchanged):
// - BounceLightPlus: Prepare and Gather with the off-screen light and the extra bounces (below).
// - BounceLightTemporal (8 samples a frame) and BounceLightTemporalHalf (4): the same, plus last frame's bounce reused.
//   The sample pattern turns from frame to frame, and each frame's gather is blended into last frame's at the same spot
//   (found by reprojection; not where the depth there does not match: something moved in front, or came into view).
//   NVR_BounceHistory is last frame's gather then; this frame's goes to the other history buffer.
// Off-screen light: a sample that leaves the screen (or lands on the first-person weapon) used to add nothing, which
//   darkened the bounce toward the screen edges and round the weapon. Its surface is unknown: it is now taken to face
//   the pixel as the seen samples' surfaces do on average, and to send light between the ambient light's (the weather's
//   outdoors, the cell's indoors) and the seen samples' average.
// Extra bounces: in Prepare, the bounce light each surface showed last frame (what Combine added to it) joins the light
//   it sends on, so light keeps bouncing, one more time each frame.

float4 TESR_BounceLightData; // x: strength, y: radius (world units), z: draw distance, w: blur radius (pixels)
float4 TESR_ReciprocalResolution;
float4 NVR_BounceLayout; // zw: 1 / half-resolution buffer size
float4 NVR_BounceTemporal;    // x: share of last frame's gather kept (0: none), y: extra bounces (0: none), z: frame number
float4 NVR_BounceAmbient;     // rgb: the ambient light an unknown surface sends (linear), w: off-screen light (0: none)
float4x4 NVR_BounceReproject; // this frame's view space -> last frame's clip space

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D TESR_BlueNoiseSampler : register(s4) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D NVR_BounceBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_BouncePrep : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
// Last frame's low-resolution result (rgb = bounce, a = view depth): its final result in Prepare (extra bounces), its
// gather output in the temporal Gather. Bound by BounceLight.cpp per pass. After the TESR_ samplers.
sampler2D NVR_BounceHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

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

// ---- UNOFFICIAL: off-screen light, extra bounces, last frame's bounce reused (BounceLightPlus, BounceLightTemporal[Half]) ----

// Where a point (this frame's view space) was last frame: xy = screen texture coordinate, z = its view depth then.
float3 LastFrame(float3 position)
{
	float4 clip = mul(float4(position, 1.0f), NVR_BounceReproject);
	float w = max(clip.w, 0.001f);
	return float3(clip.x / w * 0.5f + 0.5f, 0.5f - clip.y / w * 0.5f, clip.w);
}

// How far the depths of the low-resolution pixels round a point may stray from its own and still be the same surface:
// a few percent, and more where the surface is seen edge on (there, neighbouring pixels lie far apart in depth).
float DepthTolerance(float3 position, float3 normal)
{
	float facing = max(abs(dot(normal, normalize(position))), 0.05f);
	float texel = 2.0f * NVR_BounceLayout.z / TESR_ProjectionTransform[0][0]; // one low-resolution pixel's width at depth 1
	return position.z * (0.03f + 2.0f * texel * sqrt(1.0f - facing * facing) / facing) + 4.0f;
}

// Last frame's result at a point (from LastFrame) in NVR_BounceHistory: the four low-resolution pixels round it, bilinearly,
// each one only if its depth matches. Returns how much of it matched (0: nothing usable, off screen or another surface).
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

// Prepare, plus the extra bounces: the bounce light this surface showed last frame (NVR_BounceHistory: last frame's
// final result, rgb as Combine adds it) is light it sends on as well.
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
			// As Combine adds it: Strength * bounce * the surface's hue.
			float peak = max(max(scene.r, scene.g), max(scene.b, 0.0001f));
			light = min(light + (NVR_BounceTemporal.y * saturate(found * 2.0f) * Strength * 0.6f / peak) * last * scene, 1.0f);
		}
	}
	return float4(linear01, Pack2(OctEncode(normal)), Pack2(sqrt(light.rg)), light.b);
}

// Gather, plus the off-screen light; with temporal, a sample pattern that turns from frame to frame, blended into last
// frame's result.
float4 GatherPlus(VSOUT IN, uniform int samples, uniform bool temporal, uniform float motionPixels) : COLOR0
{
	float2 uv = IN.UVCoord;
	float4 center = tex2Dlod(NVR_BouncePrep, float4(uv, 0.0f, 0.0f));
	float3 origin = PositionFromLinear(uv, center.x);
	float fade = smoothstep(NearFade, NearFade * 2.0f, origin.z) * (1.0f - smoothstep(DrawDistance * 0.7f, DrawDistance, origin.z));
	[branch] if (fade <= 0.0f) return float4(0.0f, 0.0f, 0.0f, origin.z);

	float3 normal = OctDecode(Unpack2(center.y));
	float3 noise = tex2Dlod(TESR_BlueNoiseSampler, float4(uv / (256.0f * NVR_BounceLayout.zw), 0.0f, 0.0f)).xyz;
	// A new pattern every frame (golden-ratio steps keep each frame's pattern blue noise), so the frames average out.
	if (temporal) noise.xy = frac(noise.xy + NVR_BounceTemporal.z * float2(0.618034f, 0.754878f));
	float3 up = abs(normal.z) < 0.99f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
	float3 tangent = normalize(cross(up, normal));
	float3 bitangent = cross(normal, tangent);
	float radius = min(Radius, MaxScreenRadius * 2.0f * origin.z / TESR_ProjectionTransform[0][0]);

	float3 indirect = 0.0f;
	float seenWeight = 0.0f, seen = 0.0f; // the seen samples' weights added up, and how many were seen
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
		float visible = onScreen * step(NearFade, s.z); // a surface on screen, not the weapon in front of it
		float weight = saturate(dot(normal, dir)) * saturate(dot(OctDecode(Unpack2(texel.y)), -dir)) * nearAim * visible;
		float2 rg = Unpack2(texel.z);
		indirect += float3(rg * rg, texel.w) * weight;
		seenWeight += weight;
		seen += visible;
	}
	// The samples whose surface is unknown (off screen, behind the weapon): weighted as the seen ones on average, with
	// light between the ambient's and the seen ones' average, the more of them seen the nearer theirs.
	float3 seenLight = indirect / max(seenWeight, 1.0e-4f);
	float unknownWeight = (samples - seen) * seenWeight / max(seen, 1.0f);
	indirect += lerp(NVR_BounceAmbient.rgb, seenLight, seen / samples) * (unknownWeight * NVR_BounceAmbient.w);
	float3 result = indirect * (2.0f / samples) * fade;
	if (temporal) {
		// Last frame's at the same spot. Less of it is kept the further the spot moved on screen: half as much for one that
		// moved motionPixels low-resolution pixels. What the screen shows round a spot, and so its bounce, changes as the
		// view moves, and a long memory would lag behind.
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

// UNOFFICIAL: OffScreenLight and/or MultiBounce on, Temporal off.
technique BounceLightPlus
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 PreparePlus(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 GatherPlus(8, false, 1.0f); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}

// UNOFFICIAL: Temporal on.
technique BounceLightTemporal
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 PreparePlus(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 GatherPlus(8, true, 4.0f); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}

// UNOFFICIAL: Temporal and HalfSamples on: 4 samples a frame instead of 8.
technique BounceLightTemporalHalf
{
	pass Prepare { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 PreparePlus(); }
	pass Gather { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 GatherPlus(4, true, 8.0f); }
	pass BlurX { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 HalfVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Combine(); }
}
