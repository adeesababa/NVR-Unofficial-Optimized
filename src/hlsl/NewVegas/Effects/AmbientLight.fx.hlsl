// UNOFFICIAL 2026-10-14: ambient occlusion and bounce light in one pass ([Shaders.AmbientLight], src/effects/AmbientLight.cpp).
// While it is on it replaces the AmbientOcclusion and BounceLight effects (ShaderManager skips them).
//
// Both come from the same reads: for each half-resolution pixel, two directions across the screen (turned per pixel by blue
// noise), four reads each way along each. Along every direction the highest the surroundings rise above the pixel's surface (its
// "horizon") gives how much of the sky above the pixel they hide: ambient occlusion, worked out as in "Practical Real-Time
// Strategies for Accurate Indirect Occlusion" (Jimenez et al. 2016, GTAO), cosine-weighted, so creases darken and open ground
// does not. Each read also sends the light it shows on screen to the pixel, as far as it rises above the pixel's surface and the
// two face each other: the bounce light (first after "Horizon-based indirect lighting", Mayaux 2018, counting only the part of
// the view each read newly covered; UNOFFICIAL 2026-10-14 every read counts smoothly instead, see Estimate: far less shimmer).
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
float4 TESR_SunDirection;

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BlueNoiseSampler : register(s3) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_PointShadowBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
// Bound by AmbientLight.cpp per pass (not TESR_ samplers: those bind by their order in the file). After the TESR_ ones.
sampler2D NVR_ALPrep : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALBuffer : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_ALHistory : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
// UNOFFICIAL 2026-10-14: the occlusion kept from frame to frame too (r visibility, g view depth): with the sample pattern turning every
// frame the occlusion alone showed a new grain each frame (user: "a sort of noise filter over everything").
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
static const float BounceScale = 2.5f;       // UNOFFICIAL 2026-10-14: the smooth bounce's level, matched to the old one's average in game (2.0 read ~20% lower)
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

// UNOFFICIAL 2026-10-14 (user: an orange tip on the first-person gun): the weapon and arms' own depth, bound by hand (an NVR_
// sampler: a TESR_ one would bind by its place in the file). Those pixels get nothing from this effect and stand in for no
// surface round about (they would darken the ground behind the gun).
sampler2D NVR_ALViewModel : register(s15) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
bool IsViewModel(float2 uv) {
	float d = tex2Dlod(NVR_ALViewModel, float4(uv, 0.0f, 0.0f)).x;
	return (TESR_DepthConstants.z == 0 && d < 0.9f) || (TESR_DepthConstants.z > 0 && d > 0.01f);
}

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
	if (IsViewModel(uv)) viewZ = 1.0e6f;	// the weapon: as if open sky behind (nothing done, hides nothing)
	float3 normal = normalize(tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0f, 0.0f)).xyz * 2 - 1);
	// UNOFFICIAL 2026-10-14: the colour averaged over the 4x4 full-resolution pixels round the texel (four bilinear reads), not one
	// spot: the bounce gathered from single pixels jumped between a bright blade tip and the dark gap beside it (grass shimmer).
	float2 px = TESR_ReciprocalResolution.xy;
	float3 source = 0.25f * (tex2Dlod(TESR_SourceBuffer, float4(uv + px * float2(-1.0f, -1.0f), 0.0f, 0.0f)).rgb +
		tex2Dlod(TESR_SourceBuffer, float4(uv + px * float2(1.0f, -1.0f), 0.0f, 0.0f)).rgb +
		tex2Dlod(TESR_SourceBuffer, float4(uv + px * float2(-1.0f, 1.0f), 0.0f, 0.0f)).rgb +
		tex2Dlod(TESR_SourceBuffer, float4(uv + px * float2(1.0f, 1.0f), 0.0f, 0.0f)).rgb);
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
				// Bounce: what the read shows, as far as it rises above the pixel's own surface (within reach) and the two
				// surfaces face each other. UNOFFICIAL 2026-10-14: every read counts smoothly; it used to count only the part
				// of the view a read newly covered above the horizon so far, so on grass and rough ground a pixel's light came
				// from one or two reads picked by chance, and jumped from pixel to pixel and frame to frame (user: shimmer and
				// orange speckles).
				float raised = saturate(sampleCos - lowCos);
				float facing = saturate(dot(normal, dir)) * saturate(dot(OctDecode(Unpack2(texel.y)), -dir));
				float2 rg = Unpack2(texel.z);
				bounce += float3(rg * rg, texel.w) * (raised * facing);
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
	// Bounce: the average over the reads, x BounceScale so a pixel enclosed by surfaces showing light L receives about L.
	return float4(bounce * (BounceScale * fade / (SLICES * STEPS * 2)), saturate(visibility));
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

// Pass 5 (UNOFFICIAL): this frame's blurred bounce (NVR_ALBuffer) blended into last frame's (NVR_ALHistory); see the top. The
// occlusion the same way into its own history (second target).
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
	float4 o = float4(tex2Dlod(NVR_ALOcclusionHistory, float4((base + 0.5f) * NVR_ALLayout.zw, 0.0f, 0.0f)).r,
		tex2Dlod(NVR_ALOcclusionHistory, float4((base + float2(1.5f, 0.5f)) * NVR_ALLayout.zw, 0.0f, 0.0f)).r,
		tex2Dlod(NVR_ALOcclusionHistory, float4((base + float2(0.5f, 1.5f)) * NVR_ALLayout.zw, 0.0f, 0.0f)).r,
		tex2Dlod(NVR_ALOcclusionHistory, float4((base + 1.5f) * NVR_ALLayout.zw, 0.0f, 0.0f)).r);
	float occlusionHistory = dot(o, weight) / max(found, 1.0e-4f);
	// Held within what the bounce round the spot shows now (its four neighbours), a little wider: a blade that swayed away, or
	// light that changed, does not linger.
	float4 low = current, high = current;
	[unroll] for (int i = 0; i < 4; i++) {
		float2 offset = (i < 2 ? float2(i * 2 - 1, 0.0f) : float2(0.0f, i * 2 - 5)) * NVR_ALLayout.zw;
		float4 n = tex2Dlod(NVR_ALBuffer, float4(uv + offset, 0.0f, 0.0f));
		low = min(low, n);
		high = max(high, n);
	}
	float4 margin = (high - low) * 0.25f;
	history = clamp(history, low.rgb - margin.rgb, high.rgb + margin.rgb);
	occlusionHistory = clamp(occlusionHistory, low.a - margin.a, high.a + margin.a);
	float inside = all(last.xy > 0.0f) && all(last.xy < 1.0f) && last.z > NearFade;
	float moved = length((last.xy - uv) / NVR_ALLayout.zw);
	float keep = NVR_ALTemporal.x / (1.0f + moved / NVR_ALTemporal.y) * saturate(found * 2.0f) * inside;
	OUT.bounce = float4(lerp(current.rgb, history, keep), viewZ);
	OUT.occlusion = float4(lerp(current.a, occlusionHistory, keep), viewZ, 0.0f, 1.0f);
	return OUT;
}

// ---------------------------------------------------------------------------------------------------------------------
// UNOFFICIAL 2026-10-14 (user: the screen-space bounce is noisy and comes and goes as the camera turns): the sun's bounce
// light from a 3D light grid round the player ([Shaders.AmbientLight.Main] GridBounce), after CryEngine 3's light propagation
// volumes (Kaplanyan 2009). It does not depend on what the screen shows, so it neither shimmers nor pops when turning.
//  - ShadowManager draws a small depth map of the world from the sun round the grid (NVR_GridSunDepth, every few frames
//    and whenever the grid moves): every surface the sun lights there, on screen or not.
//  - Pass GridInject: each grid cell (a texel of a 2D atlas: 32 x 32 cells per tile, 4 x 4 tiles for 16 layers) gathers
//    the sunlit surfaces in it from that map; each sends its light out round its normal (a cosine lobe, as order-1
//    spherical harmonics: one number for all directions and three for the direction; the light's colour is the sun's x
//    a ground colour, the same everywhere for now).
//  - Pass GridPropagate (GridSteps times): each cell takes the light its six neighbours send its way, so it spreads a cell
//    a step; the total over the steps is the bounce light (NVR_GridAccum).
//  - Apply: each pixel reads the grid where it is (between the eight nearest cell centres), turns it into the light
//    falling on its surface, and adds it as the screen bounce was added.
// The grid moves with the player in whole cells, so it does not crawl; it has no occlusion, so light can cross a thin wall.
float4 NVR_GridOrigin;          // xyz: the grid's corner relative to the camera now, w: cell size (world units)
float4 NVR_GridData;            // x: strength (0: no grid), y: share of a neighbour's light passed on per step, z: sun map texel size (world units), w: 1 the grid carries the surfaces' colours
float4 NVR_GridLight;           // rgb: the bounced light's colour (sun x ground colour, linear), w: injection scale
float4x4 NVR_GridSunToGrid;     // sun depth map clip space (x, y, depth) -> position relative to the grid's corner
float4x4 NVR_GridToSun;         // position relative to the grid's corner -> sun depth map clip space
sampler2D NVR_GridWave : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridAccum : register(s9) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D NVR_GridSunDepth : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
// UNOFFICIAL 2026-10-14 colour: the sunlit surfaces' colours (ShadowGridColor.pso), and per cell the colour carried with the light
// (rgb: colour x light, a: light; the colour is rgb / a), for each step and over all steps.
sampler2D NVR_GridSunColor : register(s12) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridWaveColor : register(s13) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_GridAccumColor : register(s14) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
// UNOFFICIAL 2026-10-14 blocking: per cell the surfaces in it (xyz: their average normal, w: how much of a cell face they cover),
// from the sun map at injection. Read through stage 5 during the grid passes (every sampler register is taken; NVR_ALPrep is not
// used by them), so it is NVR_ALPrep under another name.
#define NVR_GridGeometry NVR_ALPrep
float4 NVR_GridStep;            // x: 1 on the first spreading step (the running total starts at 0), y: blocking strength
static const float3 GridGroundColor = float3(0.55f, 0.42f, 0.30f);	// without colours: a desert ground (linear)

static const float GridX = 32.0f, GridY = 32.0f, GridZ = 16.0f;
static const float AtlasSize = 128.0f;   // 4 x 4 tiles of 32 x 32
static const float SunMapSize = 256.0f;

// Order-1 spherical harmonics: a clamped cosine lobe round n, and a function's value in direction d.
float4 CosineLobe(float3 n) { return float4(0.886227f, 1.023328f * n); }
float4 ShBasis(float3 d) { return float4(0.282095f, 0.488603f * d); }

// The atlas texel (centre, uv) of cell (x, y, z).
float2 CellUV(float3 c) {
	float tile = c.z;
	float2 t = float2(fmod(tile, 4.0f), floor(tile / 4.0f));
	return (t * 32.0f + c.xy + 0.5f) / AtlasSize;
}

// The cell an atlas texel holds.
float3 AtlasCell(float2 uv) {
	float2 texel = floor(uv * AtlasSize);
	float2 t = floor(texel / 32.0f);
	return float3(texel - t * 32.0f, t.y * 4.0f + t.x);
}

// A point of the sun's depth map (uv, depth) relative to the grid's corner.
float3 SunMapPosition(float2 uv, float depth) {
	float4 p = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f), NVR_GridSunToGrid);
	return p.xyz / p.w;
}

struct GridOut { float4 wave : COLOR0; float4 accum : COLOR1; float4 waveColor : COLOR2; float4 accumColor : COLOR3; };

GridOut GridInject(VSOUT IN) {
	float3 cell = AtlasCell(IN.UVCoord);
	float size = NVR_GridOrigin.w;
	float3 low = cell * size;
	float4 centre = mul(float4(low + 0.5f * size, 1.0f), NVR_GridToSun);
	float2 centreUV = float2(centre.x * 0.5f + 0.5f, 0.5f - centre.y * 0.5f);
	// A cell seen from the sun covers up to about 1.75 cells across: 6 x 6 reads over that.
	float span = 1.75f * size / NVR_GridData.z;
	float stepTexels = span / 6.0f;
	float4 sh = 0.0f;
	float4 colour = 0.0f;	// the surfaces' colours (linear) added up, and how many
	float3 normals = 0.0f;	// their normals added up (blocking)
	[loop] for (int j = 0; j < 6; j++) {
		[loop] for (int i = 0; i < 6; i++) {
			float2 uv = centreUV + (float2(i, j) - 2.5f) * stepTexels / SunMapSize;
			float depth = tex2Dlod(NVR_GridSunDepth, float4(uv, 0.0f, 0.0f)).r;
			if (depth >= 0.9999f) continue;	// nothing drawn there
			float3 p = SunMapPosition(uv, depth);
			float3 inCell = p - low;
			if (any(inCell < 0.0f) || any(inCell >= size)) continue;
			// The surface's normal from its neighbours in the map (facing the sun).
			float2 px = 1.0f / SunMapSize;
			float3 px1 = SunMapPosition(uv + float2(px.x, 0.0f), tex2Dlod(NVR_GridSunDepth, float4(uv + float2(px.x, 0.0f), 0.0f, 0.0f)).r);
			float3 py1 = SunMapPosition(uv + float2(0.0f, px.y), tex2Dlod(NVR_GridSunDepth, float4(uv + float2(0.0f, px.y), 0.0f, 0.0f)).r);
			float3 n = cross(px1 - p, py1 - p);
			float len = length(n);
			n = len > 1e-4f ? n / len : float3(0.0f, 0.0f, 1.0f);
			n *= dot(n, TESR_SunDirection.xyz) < 0.0f ? -1.0f : 1.0f;
			sh += CosineLobe(n);
			normals += n;
			colour += float4(NVR_GridData.w > 0.5f ? pow(tex2Dlod(NVR_GridSunColor, float4(uv, 0.0f, 0.0f)).rgb, 2.2f) : GridGroundColor, 1.0f);
		}
	}
	// Each read stands for stepTexels^2 map texels of sunlit surface; as a share of a cell's face.
	float faceShare = (stepTexels * NVR_GridData.z) * (stepTexels * NVR_GridData.z) / (size * size);
	sh *= NVR_GridLight.w * faceShare;
	GridOut OUT;
	OUT.wave = sh;
	// The second target holds the cell's surfaces for blocking (not the running total: that starts at 0 on the first step,
	// NVR_GridStep.x; injection is kept out of it, else a surface's own cell lit the surface with what it sends out).
	float normalLength = length(normals);
	OUT.accum = float4(normalLength > 1e-4f ? normals / normalLength : float3(0.0f, 0.0f, 1.0f), colour.a * faceShare);
	// The colour as colour x light (the light: the lobe's all-directions part), so steps can add it up weighted by light.
	OUT.waveColor = float4(colour.rgb / max(colour.a, 1.0f), 1.0f) * sh.x;
	OUT.accumColor = 0.0f;
	return OUT;
}

GridOut GridPropagate(VSOUT IN) {
	float3 cell = AtlasCell(IN.UVCoord);
	float4 sh = 0.0f;
	float4 colour = 0.0f;
	float4 blocker = tex2Dlod(NVR_GridGeometry, float4(IN.UVCoord, 0.0f, 0.0f));
	[unroll] for (int k = 0; k < 6; k++) {
		float3 axis = k == 0 ? float3(1, 0, 0) : k == 1 ? float3(-1, 0, 0) : k == 2 ? float3(0, 1, 0) : k == 3 ? float3(0, -1, 0) : k == 4 ? float3(0, 0, 1) : float3(0, 0, -1);
		float3 neighbour = cell - axis;	// the neighbour on the far side; its light travels along +axis to this cell
		if (any(neighbour < 0.0f) || neighbour.x >= GridX || neighbour.y >= GridY || neighbour.z >= GridZ) continue;
		float4 from = tex2Dlod(NVR_GridWave, float4(CellUV(neighbour), 0.0f, 0.0f));
		float sent = max(dot(from, ShBasis(axis)), 0.0f);
		// Blocking: the surfaces in this cell stop the light crossing them, as far as they face across its path (a floor does
		// not stop light running along it).
		sent *= 1.0f - saturate(blocker.w * abs(dot(blocker.xyz, axis)) * NVR_GridStep.y);
		sh += CosineLobe(axis) * (sent * NVR_GridData.y);
		// The neighbour's colour travels with the light it sends.
		float4 fromColour = tex2Dlod(NVR_GridWaveColor, float4(CellUV(neighbour), 0.0f, 0.0f));
		colour += float4(fromColour.rgb / max(fromColour.a, 1e-5f), 1.0f) * (sent * NVR_GridData.y);
	}
	GridOut OUT;
	OUT.wave = sh;
	OUT.accum = (NVR_GridStep.x > 0.5f ? 0.0f : tex2Dlod(NVR_GridAccum, float4(IN.UVCoord, 0.0f, 0.0f))) + sh;	// the total so far (read at the texel centre) plus this step
	OUT.waveColor = colour;
	OUT.accumColor = tex2Dlod(NVR_GridAccumColor, float4(IN.UVCoord, 0.0f, 0.0f)) + colour;
	return OUT;
}

// The grid's bounce light falling on a surface at a camera-relative position with a world normal (0 outside the grid).
float3 GridBounce(float3 cameraRelative, float3 normal) {
	float size = NVR_GridOrigin.w;
	// Half a cell off the surface along its normal: a surface's own cell holds the light it sends out, not what it receives.
	float3 g = (cameraRelative - NVR_GridOrigin.xyz + normal * (0.5f * size)) / size - 0.5f;
	float3 dims = float3(GridX, GridY, GridZ);
	float3 edge = min(g + 0.5f, dims - 0.5f - g);
	float fade = saturate(min(min(edge.x, edge.y), edge.z) / 2.0f);
	[branch] if (fade <= 0.0f) return 0.0f;
	g = clamp(g, 0.0f, dims - 1.0f);
	float z0 = floor(g.z);
	float z1 = min(z0 + 1.0f, GridZ - 1.0f);
	float fz = g.z - z0;
	// Within a layer the atlas's own bilinear filter (kept inside the tile: g.xy is clamped to the cell centres).
	float4 a = tex2Dlod(NVR_GridAccum, float4(CellUV(float3(g.xy, z0)), 0.0f, 0.0f));
	float4 b = tex2Dlod(NVR_GridAccum, float4(CellUV(float3(g.xy, z1)), 0.0f, 0.0f));
	float4 sh = lerp(a, b, fz);
	// The surfaces' colour the light comes from (weighted by their light), or one ground colour.
	float3 colour = GridGroundColor;
	[branch] if (NVR_GridData.w > 0.5f) {
		float4 ca = tex2Dlod(NVR_GridAccumColor, float4(CellUV(float3(g.xy, z0)), 0.0f, 0.0f));
		float4 cb = tex2Dlod(NVR_GridAccumColor, float4(CellUV(float3(g.xy, z1)), 0.0f, 0.0f));
		float4 c = lerp(ca, cb, fz);
		colour = c.a > 1e-5f ? c.rgb / c.a : GridGroundColor;
	}
	// The light arriving over the hemisphere round the normal; / pi so that a surface ringed by the bounce of ground lit with
	// colour C gets about C x the share of the view the ground fills (test 1: x 1 washed the picture out).
	float irradiance = max(dot(sh, CosineLobe(normal)), 0.0f) * 0.3183f;
	return irradiance * colour * NVR_GridLight.rgb * fade;
}

// Pass 5: upsample and apply.
float4 Apply(VSOUT IN) : COLOR0 {
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	[branch] if (NVR_ALFlags.x > 0.5f) color = float4(SunShadowApply(color.rgb, IN.UVCoord), 1.0f);
	float depth = readDepth(IN.UVCoord);
	[branch] if (depth < NearFade || depth > DrawDistance || IsViewModel(IN.UVCoord)) return DebugView > 0.5f ? float4(0.0f, 0.0f, 0.0f, 1.0f) : color;

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
			sum += float4(tex2Dlod(NVR_ALHistory, float4(tapUV, 0.0f, 0.0f)).rgb, tex2Dlod(NVR_ALOcclusionHistory, float4(tapUV, 0.0f, 0.0f)).r) * weight;
			weights += weight;
		}
	}
	float4 ambient = sum / weights;

	float3 lin = linearize(color.rgb);
	float ao = lerp(DarkestAO, 1.0f, ambient.a);
	ao = lerp(ao, 1.0f, saturate((luma(lin) - BrightFade) * 3.0f));   // bright surfaces keep their light, as the old AO
	float peak = max(max(lin.r, lin.g), max(lin.b, 0.0001f));
	float3 albedo = lin / peak * 0.6f;                                  // the surface's hue stands in for its colour (as BounceLight)
	// UNOFFICIAL 2026-10-14: the light grid's bounce (GridBounce), darkened by the occlusion as light from round about is.
	float3 gridBounce = 0.0f;
	[branch] if (NVR_GridData.x > 0.0f) {
		float3 cameraRelative = toWorld(IN.UVCoord) * depth;
		float3 normal = normalize(GetWorldNormalLod(IN.UVCoord));
		gridBounce = GridBounce(cameraRelative, normal) * (NVR_GridData.x * ao);
	}
	[branch] if (DebugView > 6.5f) return delinearize(float4(gridBounce * albedo, 1.0f));	// 7: the grid's bounce alone
	[branch] if (DebugView > 2.5f) return float4(ambient.rgb, 1.0f);	// diagnostics: the estimate's raw values
	[branch] if (DebugView > 0.5f) return delinearize(float4(DebugView < 1.5f ? ao.xxx : BounceStrength * ambient.rgb * albedo, 1.0f));
	lin = lin * ao + (BounceStrength * ambient.rgb + gridBounce) * albedo;
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

// UNOFFICIAL 2026-10-14: the light grid (see GridInject above), each pass into two targets (the step and the running total).
technique AmbientLightGrid {
	pass GridInject { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 GridInject(); }
	pass GridPropagate { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 GridPropagate(); }
}
