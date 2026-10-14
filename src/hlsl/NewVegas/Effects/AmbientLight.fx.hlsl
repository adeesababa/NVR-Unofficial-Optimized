// UNOFFICIAL 2026-10-14: ambient occlusion and bounce light in one pass ([Shaders.AmbientLight], src/effects/AmbientLight.cpp).
// While it is on it replaces the AmbientOcclusion and BounceLight effects (ShaderManager skips them).
//
// Both come from the same reads: for each half-resolution pixel, two directions across the screen (turned per pixel by blue
// noise), four reads each way along each. Along every direction the highest the surroundings rise above the pixel's surface (its
// "horizon") gives how much of the sky above the pixel they hide: ambient occlusion, worked out as in "Practical Real-Time
// Strategies for Accurate Indirect Occlusion" (Jimenez et al. 2016, GTAO), cosine-weighted, so creases darken and open ground
// does not. Each read that raises the horizon also sends the light it shows on screen to the pixel, in proportion to how much
// more of the view above the pixel it covers and to how much the two face each other: the bounce light (after "Horizon-based
// indirect lighting", Mayaux 2018; Therrien et al. 2023 do the same with bitmasks, which shader model 3 has no integers for).
//
// Pass 1 Prepare (half): depth, normal and colour of each half-resolution pixel packed into one texel (as BounceLight's), so
//        each read of the estimate is one small texel.
// Pass 2 Estimate (half): rgb = bounce light, a = visibility (1 open, 0 fully hidden).
// Pass 3-4 Blur (half): depth-aware, horizontal then vertical, all four channels.
// Pass 5 Accumulate (half, UNOFFICIAL 2026-10-14, user: "shimmers really hard above 1 bounce strength", worst on grass): the
//        bounce light blended into last frame's at the same spot (found by reprojection, kept only where the depth there
//        matches, less of it the further the spot moved, and held within the range of the bounce round it now so swaying grass
//        does not smear). With it the estimate's noise turns every frame, so the frames average out. rgb = bounce, a = view depth
//        (next frame's match). Off ([Shaders.AmbientLight.Main] Temporal) or after a break: this frame's bounce as it is.
// Pass 6 Apply (full): depth-aware upsample; the scene darkened by the occlusion (less on bright surfaces, as the old AO) and the
//        bounce added, tinted by the surface's own hue (as BounceLight: there is no albedo buffer).
//
// UNOFFICIAL 2026-10-14 cheaper: outdoors, the sun-shadow apply (ShadowsExteriors.fx.hlsl, a full-resolution pass of its own while
// this effect is on) is done here instead (NVR_ALFlags.x): in Apply before the occlusion, and in Prepare on the colour the bounce
// light is gathered from, so both see the scene as they did after that pass. The blurs and the apply take their depths from the
// depth buffer (4 bytes a read) instead of the 16-byte prepared texel.

float4 TESR_AmbientLightData;   // x: radius (world units), y: occlusion strength, z: bounce strength, w: draw distance
float4 TESR_AmbientLightData2;  // x: darkest the occlusion may make a pixel (0-1), y: brightness above which it fades, z: blur radius (half-res texels per tap), w: debug view (1 occlusion, 2 bounce)
float4 TESR_ReciprocalResolution;
float4 NVR_ALLayout;            // xy: half-resolution size / full size, zw: 1 / half-resolution size
float4 NVR_ALFlags;             // x: apply the sun shadows here (ShaderManager skips their own pass)
float4 NVR_ALTemporal;          // x: share of last frame's bounce kept (0 none), y: moved pixels that halve it, z: frame number (0-1023)
float4x4 NVR_ALReproject;       // this frame's view space -> last frame's clip space (BounceLightMath.h)
float4 TESR_WaterSettings;      // x: water height, z: camera underwater
float4 TESR_ShadowData;         // y: darkness
float4 TESR_ShadowFade;         // x: fading at sunrise/sunset, z: point light shadows
float4 TESR_SkyColor;
float4 TESR_SunAmbient;

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BlueNoiseSampler : register(s3) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_PointShadowBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
// Bound by AmbientLight.cpp per pass (not TESR_ samplers: those bind by their order in the file). After the TESR_ ones.
sampler2D NVR_ALPrep : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALBuffer : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

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
static const float NearFade = 40.0f;          // view depth (world units) below which nothing is done: the weapon, the camera's own body
static const float MaxScreenRadius = 0.08f;   // largest radius on screen, as a share of the screen width (keeps the reads in cache)

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

// ShadowsExteriors.fx.hlsl Shadow() on a gamma-encoded colour (the same maths as VolumetricFog.fx.hlsl CompositeSunShadow).
float3 SunShadowApply(float3 color, float2 uv) {
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

// View-space position (x right, y up, z forward, world units) from a screen position and the linear depth (view z / far z).
float3 PositionFromLinear(float2 uv, float linear01) {
	float z = projectedDepthFromLinear(linear01);
	float4 viewSpace = mul(float4(uv.x * 2 - 1, (1 - uv.y) * 2 - 1, z, 1.0f), TESR_InvProjectionTransform);
	return viewSpace.xyz / viewSpace.w;
}

// View-space position from a screen position and its view depth: two multiply-adds (the projection is symmetric).
float3 PositionFromViewZ(float2 uv, float z) {
	return float3((uv.x * 2.0f - 1.0f) / TESR_ProjectionTransform[0][0], (1.0f - uv.y * 2.0f) / TESR_ProjectionTransform[1][1], 1.0f) * z;
}

// Two values in [0, 1] as 12-bit integers in one 32-bit float, and a unit vector as two numbers in [0, 1] (as BounceLight's).
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

// Pass 1: x = view depth (world units), y = normal, z = colour red and green (square-root encoded), w = colour blue (linear light, capped
// so small bright glints do not spray light around).
float4 Prepare(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float viewZ = PositionFromLinear(uv, tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x).z;
	float3 normal = normalize(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2 - 1);
	float3 source = tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0f, 0.0f)).rgb;
	[branch] if (NVR_ALFlags.x > 0.5f) source = SunShadowApply(source, uv);
	float3 light = min(linearize(source), 1.0f);
	return float4(viewZ, Pack2(OctEncode(normal)), Pack2(sqrt(light.rg)), light.b);
}

// GTAO's cosine-weighted visible arc of one slice between its two horizons (cosines), each kept within the hemisphere round the
// normal.
float SliceArc(float horizonCos0, float horizonCos1, float n, float cosNormal, float sinN) {
	float h0 = n + clamp(-acos(clamp(horizonCos1, -1.0f, 1.0f)) - n, -1.5707963f, 1.5707963f);
	float h1 = n + clamp(acos(clamp(horizonCos0, -1.0f, 1.0f)) - n, -1.5707963f, 1.5707963f);
	return (cosNormal + 2.0f * h0 * sinN - cos(2.0f * h0 - n)) * 0.25f + (cosNormal + 2.0f * h1 * sinN - cos(2.0f * h1 - n)) * 0.25f;
}

// Pass 2: the occlusion and the bounce light (see the top of the file).
float4 Estimate(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 center = tex2Dlod(NVR_ALPrep, float4(uv, 0.0f, 0.0f));
	float3 origin = PositionFromViewZ(uv, center.x);
	float fade = smoothstep(NearFade, NearFade * 2.0f, origin.z) * (1.0f - smoothstep(DrawDistance * 0.7f, DrawDistance, origin.z));
	[branch] if (fade <= 0.0f) return float4(0.0f, 0.0f, 0.0f, 1.0f);

	float3 normal = OctDecode(Unpack2(center.y));
	float3 viewVec = normalize(-origin);
	float3 noise = tex2Dlod(TESR_BlueNoiseSampler, float4(uv / (256.0f * NVR_ALLayout.zw), 0.0f, 0.0f)).xyz;
	// UNOFFICIAL: with Temporal, a new pattern every frame (golden-ratio steps keep it blue noise), so the frames average out.
	noise.xy = frac(noise.xy + (NVR_ALTemporal.x > 0.0f ? NVR_ALTemporal.z : 0.0f) * float2(0.618034f, 0.754878f));
	// The radius on screen: r world units at depth z span r * P00 / (2 z) of the screen's width (P11 for the height).
	float2 screenRadius = Radius * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) / (2.0f * origin.z);
	screenRadius *= min(1.0f, MaxScreenRadius / screenRadius.x);
	float falloffStart = 0.4f * Radius;

	// Diagnostics (DebugView 3-6): the pixel's normal, its view depth / 2000, the reach on screen x 10, raw horizons.
	[branch] if (DebugView > 2.5f && DebugView < 3.5f) return float4(normal * 0.5f + 0.5f, 1.0f);
	[branch] if (DebugView > 3.5f && DebugView < 4.5f) return float4(origin.z / 2000.0f, frac(origin.z / 100.0f), 0.0f, 1.0f);
	[branch] if (DebugView > 4.5f && DebugView < 5.5f) return float4(screenRadius * 10.0f, Radius / 1000.0f, 1.0f);
	float visibility = 0.0f, weightSum = 0.0f;
	float3 bounce = 0.0f;
	float firstCos = 0.0f, firstLow = 0.0f;
	[unroll] for (int slice = 0; slice < SLICES; slice++) {
		float phi = (slice + noise.x) * (3.14159265f / SLICES);
		float2 omega = float2(cos(phi), -sin(phi));             // on screen (y down)
		float3 direction = float3(cos(phi), sin(phi), 0.0f);    // the same in view space (y up)
		float3 orthoDirection = direction - dot(direction, viewVec) * viewVec;
		float3 axis = normalize(cross(orthoDirection, viewVec));
		float3 projectedNormal = normal - axis * dot(normal, axis);
		float projectedLength = length(projectedNormal) + 1e-5f;
		float signNormal = dot(orthoDirection, projectedNormal) >= 0.0f ? 1.0f : -1.0f;
		float cosNormal = saturate(dot(projectedNormal, viewVec) / projectedLength);
		float n = signNormal * acos(cosNormal);
		// Start each side's horizon at the pixel's own tangent plane: nothing below it can hide anything.
		float lowCos0 = cos(n + 1.5707963f), lowCos1 = cos(n - 1.5707963f);
		float horizonCos0 = lowCos0, horizonCos1 = lowCos1;
		[unroll] for (int k = 0; k < STEPS; k++) {
			float s = (k + frac(noise.y + k * 0.618034f)) / STEPS;
			s *= s;                                              // more reads close to the pixel
			float2 offset = omega * screenRadius * max(s, 2.0f * NVR_ALLayout.z);
			[unroll] for (int side = 0; side < 2; side++) {
				float2 sampleUV = uv + (side == 0 ? offset : -offset);
				float onScreen = step(0.0f, sampleUV.x) * step(sampleUV.x, 1.0f) * step(0.0f, sampleUV.y) * step(sampleUV.y, 1.0f);
				sampleUV = (floor(saturate(sampleUV) / NVR_ALLayout.zw) + 0.5f) * NVR_ALLayout.zw;   // the centre of the texel read
				float4 texel = tex2Dlod(NVR_ALPrep, float4(sampleUV, 0.0f, 0.0f));
				float3 delta = PositionFromViewZ(sampleUV, texel.x) - origin;
				float dist = length(delta) + 1e-4f;
				float3 dir = delta / dist;
				float weight = saturate((Radius - dist) / (Radius - falloffStart)) * onScreen * step(NearFade, origin.z + delta.z);
				float lowCos = side == 0 ? lowCos0 : lowCos1;
				float sampleCos = lerp(lowCos, dot(dir, viewVec), weight);
				float horizon = side == 0 ? horizonCos0 : horizonCos1;
				// Bounce: the part of the view above the pixel this read newly covers, lit by what the read shows, as far as
				// the two surfaces face each other.
				float covered = max(sampleCos - horizon, 0.0f);
				float facing = saturate(dot(normal, dir)) * saturate(dot(OctDecode(Unpack2(texel.y)), -dir));
				float2 rg = Unpack2(texel.z);
				bounce += float3(rg * rg, texel.w) * (covered * facing);
				if (side == 0) horizonCos0 = max(horizonCos0, sampleCos); else horizonCos1 = max(horizonCos1, sampleCos);
				if (slice == 0 && side == 0 && k == 1) { firstCos = dot(dir, viewVec); firstLow = weight; }
			}
		}
		// The slice's visible arc, over what it would be with nothing in the way: one slice's arc is only right on average
		// over all directions (it exceeds 1 for a surface seen at an angle), so a few slices alone need this to keep open
		// ground at exactly 1.
		float sinN = sin(n);
		visibility += projectedLength * SliceArc(horizonCos0, horizonCos1, n, cosNormal, sinN);
		weightSum += projectedLength * SliceArc(lowCos0, lowCos1, n, cosNormal, sinN);
	}
	// Raised to a power (1.5 at strength 1, as XeGTAO does) so creases and contact read as the eye expects.
	[branch] if (DebugView > 5.5f) return float4(visibility / max(weightSum, 1e-4f), firstCos * 0.5f + 0.5f, firstLow, 1.0f);
	visibility = pow(saturate(visibility / max(weightSum, 1e-4f)), 1.5f * OcclusionStrength);
	visibility = lerp(1.0f, visibility, fade);
	// Bounce: x 2 per slice side covered, so a pixel enclosed by surfaces showing light L receives about L.
	return float4(bounce * (2.0f * fade / SLICES), saturate(visibility));
}

// Passes 3-4: depth-aware blur, 7 taps along axis (half-resolution texels apart x BlurStep). UNOFFICIAL 2026-10-14: the bounce
// (rgb) blurs across a much looser depth step than the occlusion (a). Grass is a field of blades each at its own depth, so the
// tight step kept every grass pixel to itself and its grain was never smoothed; light that bounces is soft anyway, while the
// occlusion keeps its crisp edges.
float4 Blur(VSOUT IN, uniform float2 axis) : COLOR0 {
	float depth = readDepthLod(IN.UVCoord);
	float tolerance = max(depth * 0.03f, 2.0f);
	float looseTolerance = max(depth * 0.25f, 16.0f);
	static const float weights[4] = { 0.30f, 0.23f, 0.11f, 0.05f };
	float4 center = tex2Dlod(NVR_ALBuffer, float4(IN.UVCoord, 0.0f, 0.0f));
	float3 bounce = center.rgb * weights[0];
	float visibility = center.a * weights[0];
	float2 total = weights[0];	// x occlusion, y bounce
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

// Pass 5 (UNOFFICIAL): this frame's blurred bounce (NVR_ALBuffer) blended into last frame's (NVR_ALHistory); see the top.
float4 Accumulate(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 current = tex2Dlod(NVR_ALBuffer, float4(uv, 0.0f, 0.0f));
	float viewZ = tex2Dlod(NVR_ALPrep, float4(uv, 0.0f, 0.0f)).x;
	[branch] if (!(NVR_ALTemporal.x > 0.0f) || viewZ < NearFade) return float4(current.rgb, viewZ);
	float3 origin = PositionFromViewZ(uv, viewZ);
	float4 clip = mul(float4(origin, 1.0f), NVR_ALReproject);
	float w = max(clip.w, 0.001f);
	float3 last = float3(clip.x / w * 0.5f + 0.5f, 0.5f - clip.y / w * 0.5f, clip.w);
	// Last frame's at that point: the four half-resolution pixels round it, bilinearly, each only if its depth matches.
	float2 position = last.xy / NVR_ALLayout.zw - 0.5f;
	float2 base = floor(position);
	float2 f = position - base;
	float4 t00 = tex2Dlod(NVR_ALHistory, float4((base + 0.5f) * NVR_ALLayout.zw, 0.0f, 0.0f));
	float4 t10 = tex2Dlod(NVR_ALHistory, float4((base + float2(1.5f, 0.5f)) * NVR_ALLayout.zw, 0.0f, 0.0f));
	float4 t01 = tex2Dlod(NVR_ALHistory, float4((base + float2(0.5f, 1.5f)) * NVR_ALLayout.zw, 0.0f, 0.0f));
	float4 t11 = tex2Dlod(NVR_ALHistory, float4((base + 1.5f) * NVR_ALLayout.zw, 0.0f, 0.0f));
	float4 weight = float4((1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y), (1.0f - f.x) * f.y, f.x * f.y);
	weight *= abs(float4(t00.a, t10.a, t01.a, t11.a) - last.z) < max(last.z * 0.05f, 4.0f);
	float found = dot(weight, 1.0f);
	float3 history = (t00.rgb * weight.x + t10.rgb * weight.y + t01.rgb * weight.z + t11.rgb * weight.w) / max(found, 1.0e-4f);
	// Held within what the bounce round the spot shows now (its four neighbours), a little wider: a blade that swayed away, or
	// light that changed, does not linger.
	float3 low = current.rgb, high = current.rgb;
	[unroll] for (int i = 0; i < 4; i++) {
		float2 offset = (i < 2 ? float2(i * 2 - 1, 0.0f) : float2(0.0f, i * 2 - 5)) * NVR_ALLayout.zw;
		float3 n = tex2Dlod(NVR_ALBuffer, float4(uv + offset, 0.0f, 0.0f)).rgb;
		low = min(low, n);
		high = max(high, n);
	}
	float3 margin = (high - low) * 0.25f;
	history = clamp(history, low - margin, high + margin);
	float inside = all(last.xy > 0.0f) && all(last.xy < 1.0f) && last.z > NearFade;
	float moved = length((last.xy - uv) / NVR_ALLayout.zw);
	float keep = NVR_ALTemporal.x / (1.0f + moved / NVR_ALTemporal.y) * saturate(found * 2.0f) * inside;
	return float4(lerp(current.rgb, history, keep), viewZ);
}

// Pass 5: upsample and apply.
float4 Apply(VSOUT IN) : COLOR0 {
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	[branch] if (NVR_ALFlags.x > 0.5f) color = float4(SunShadowApply(color.rgb, IN.UVCoord), 1.0f);
	float depth = readDepth(IN.UVCoord);
	[branch] if (depth < NearFade || depth > DrawDistance) return DebugView > 0.5f ? float4(0.0f, 0.0f, 0.0f, 1.0f) : color;

	// The four nearest half-resolution texels, the ones at another depth (across an object's edge) counted less.
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
			// UNOFFICIAL: the bounce from the accumulated buffer (Accumulate), the occlusion from the blurred one.
			sum += float4(tex2Dlod(NVR_ALHistory, float4(tapUV, 0.0f, 0.0f)).rgb, tex2Dlod(NVR_ALBuffer, float4(tapUV, 0.0f, 0.0f)).a) * weight;
			weights += weight;
		}
	}
	float4 ambient = sum / weights;

	float3 lin = linearize(color.rgb);
	float ao = lerp(DarkestAO, 1.0f, ambient.a);
	ao = lerp(ao, 1.0f, saturate((luma(lin) - BrightFade) * 3.0f));   // bright surfaces keep their light, as the old AO
	float peak = max(max(lin.r, lin.g), max(lin.b, 0.0001f));
	float3 albedo = lin / peak * 0.6f;                                  // the surface's hue stands in for its colour (as BounceLight)
	[branch] if (DebugView > 2.5f) return float4(ambient.rgb, 1.0f);	// diagnostics: the estimate's raw values
	[branch] if (DebugView > 0.5f) return delinearize(float4(DebugView < 1.5f ? ao.xxx : BounceStrength * ambient.rgb * albedo, 1.0f));
	lin = lin * ao + BounceStrength * ambient.rgb * albedo;
	return delinearize(float4(lin, color.a));
}

technique AmbientLight {
	pass Prepare { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Prepare(); }
	pass Estimate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Estimate(); }
	pass BlurX { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Blur(float2(1.0f, 0.0f)); }
	pass BlurY { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Blur(float2(0.0f, 1.0f)); }
	pass Accumulate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Accumulate(); }
	pass Apply { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 Apply(); }
}
