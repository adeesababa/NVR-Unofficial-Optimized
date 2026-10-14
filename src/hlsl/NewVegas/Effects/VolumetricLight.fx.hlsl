// UNOFFICIAL: volumetric sun light -- shafts of sunlight through the air, where trees, buildings and rocks shade it.
// Written for this build; the idea comes from Physically-Based-Reloaded's Volumetric Light (mcstfuerson's shader, ported
// by Conor), but no code is shared: theirs marches its own shadow lookup at half resolution with a history pass; this
// one reuses Includes/SunCascades.hlsl (the lookup Specular already uses).
//
// A march with few steps bands (each step is a hard edge in the light). The march breaks the bands up into noise by
// starting each pixel's steps at a different point, and the passes after it take the noise out again:
//   1 March    (half resolution): from the eye towards each block's surface, at most MaxDistance, Steps samples of the
//              sun's shadow, each pixel's start shifted by interleaved gradient noise (a new pattern every frame when
//              Temporal is on). r: the share of the view the lit air scatters towards the eye (single scattering,
//              1 - exp falloff), g: the same if all the air were lit, b: depth / farZ (for the depth-aware passes).
//              Written to TESR_VolumetricLightTemp, or to TESR_VolumetricLightMarch when Temporal is on.
//   3 Temporal (half resolution, only when Temporal is on): March -> Temp. Blends in last frame's result at the same
//              spot (TESR_VolumetricLightHistory, reprojected by depth), held to the range of this frame's neighbours so
//              a moving shadow does not leave a trail. The effect then copies Temp into History for the next frame.
//   2 Blur X, 4 Blur Y (half resolution): Temp -> March -> TESR_VolumetricLightBuffer, a depth-aware Gaussian each way
//              (BlurRadius texels), which keeps the shafts sharp at the edges of buildings while smoothing the noise.
//   0 Composite (full resolution, in the frame chain): depth-aware upsample, then the lit air adds sunlight (shaped by
//              the forward-scattering phase, brightest looking towards the sun) and the shaded air darkens what is behind
//              it a little -- the game's fog lights all the air alike, so without that the shafts would only ever brighten.
//
// The frame here is gamma encoded HDR (VolumetricFog linearises it the same way): the light is added in linear light.

float4 TESR_ReciprocalResolution;
float4 TESR_SunColor;
float4 TESR_ShadowFade;              // read by Includes/Shadows.hlsl (point lights; unused here)
float4 TESR_VolumetricLightData;     // x: strength, y: shade darkening, z: air density per game unit, w: march distance
float4 TESR_VolumetricLightData2;    // x: steps, y: phase asymmetry g, z: fade (shadows on, rain), w: noise frame offset
float4 TESR_VolumetricLightData3;    // x: history weight (0 = none this frame), y: blur radius in half-resolution texels
float4 TESR_VolumetricLightLayout;   // xy: one half-resolution texel in screen UV, zw: unused
float4x4 TESR_VolumetricLightReproject; // camera-relative world position -> last frame's clip space

// Declared in register order: the effect binds TESR_ samplers by declaration order (a mismatch silently swaps them).
sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_ShadowAtlas : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightMarch : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightTemp : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_VolumetricLightHistory : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
texture NoTexture;   // never set: unbinds a technique's own render target from its sampler

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

// Interleaved gradient noise (Jimenez 2014) in [0, 1): neighbouring pixels get well spread values with no visible
// pattern, so the march's steps land at different depths side by side and the bands turn into fine noise. Shifted by
// a new amount every frame when Temporal is on, so the history averages different start points at each pixel.
float GradientNoise(float2 pixel) {
	pixel += NoiseFrame * 5.588238;
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// The sun's visibility at one point of the march: one atlas read from the first cascade that holds the point (no blend
// between cascades, no normal offset: the march averages many points and is blurred). Beyond the LOD cascade (the
// shadow distance) the air counts as lit. EVSM4 reads only its positive warp. The LOD cascade matters: at the default
// 6000-unit shadow distance the first three end near 1750 units, so without it every building farther than that let
// the light straight through. GetSunCascadeLight, which blends cascades with a full three-format
// lookup each, made the march loop take minutes to compile.
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
	[branch] if (TESR_ShadowFormatData.x == 3.0) return ShadowPCF2x2(TESR_ShadowAtlas, lightSpace.xy * 0.5 + quadrant, lightSpace.z - 0.0002, TESR_ShadowBlur.x);	// UNOFFICIAL PCF
	float4 moments = tex2Dlod(TESR_ShadowAtlas, float4(lightSpace.xy * 0.5 + quadrant, 0.0, 0.0));
	float bias = TESR_ShadowFormatData.x == 0.0 ? 0.00001 : 0.01;
	if (TESR_ShadowFormatData.x == 0.0) return GetLightAmountValueVSM(moments.xy, lightSpace.z, bias, bleed);
	if (TESR_ShadowFormatData.x == 1.0) return GetLightAmountValueEVSM2(moments.xy, lightSpace.z, bias, bleed, TESR_ShadowFormatData.y);
	float2 exponents = GetEVSMExponents(40.0, 5.0, TESR_ShadowFormatData.y);
	float warped = WarpDepth(lightSpace.z, exponents).x;
	float depthScale = bias * exponents.x * warped;
	return ChebyshevUpperBound(moments.xz, warped, depthScale * depthScale, bleed);
}

// ---- 1: March, half resolution ----
float4 MarchPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * LowTexel;                                  // this block's centre on screen
	float depth = readDepthLod(uv - 0.5 * TESR_ReciprocalResolution.xy);   // a pixel centre in the block, not an edge
	float isSky = depth >= farZ * 0.996 ? 1.0 : 0.0;
	float planar = isSky > 0.0 ? MarchDistance : min(depth, MarchDistance);

	float3 ray = toWorld(uv);                       // the eye vector per unit of planar depth (camera-relative world)
	float sigma = Density * length(ray);            // extinction per unit of planar depth
	float dt = planar / Steps;
	float offset = GradientNoise(vpos);

	float lit = 0.0, all = 0.0;
	[loop] for (float i = 0.0; i < Steps; i += 1.0) {
		float t = (i + offset) * dt;
		float transmittance = exp(-sigma * t);
		lit += MarchShadow(ray * t) * transmittance;
		all += transmittance;
	}
	float scattered = sigma * dt;                   // the share each sample scatters towards the eye
	return float4(lit * scattered, all * scattered, depth / farZ, 1.0);
}

// ---- 3: Temporal, half resolution ----
// Last frame's result where this block's surface was then: the four history texels round it, bilinearly, each only if
// its depth matches. Returns how much of it matched (0: off screen, or something else stood there).
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

	// The range this frame's neighbours on the same surface span: the history is held inside it, so where a shadow
	// moved (or the camera uncovered new ground) the old light cannot linger.
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
	float2 last = float2(clip.x / w * 0.5 + 0.5, 0.5 - clip.y / w * 0.5);
	float2 history;
	float found = HistoryAt(last, clip.w / farZ, history);
	found *= all(last > 0.0) && all(last < 1.0);

	float2 margin = 0.1 * (high - low) + 1.0e-4;
	history = clamp(history, low - margin, high + margin);
	float weight = HistoryWeight * saturate(found);
	return float4(lerp(current.rg, history, weight), current.b, 1.0);
}

// ---- 2 / 4: Blur X / Blur Y, half resolution ----
// A Gaussian over BlurRadius texels each way (sigma half the radius), each tap weighed down by how far its depth is
// from the centre's, so the light does not smear across the edge of a building onto the sky behind it.
float4 BlurAxis(sampler2D source, float2 vpos, float2 axis) {
	float4 centre = tex2Dlod(source, float4((vpos + 0.5) * LowTexel, 0, 0));
	float tolerance = max(centre.b * 0.05, 0.00002);
	float sigma = max(BlurRadius * 0.5, 0.5);
	float falloff = -0.5 / (sigma * sigma);
	float2 sum = centre.rg;
	float weights = 1.0;
	// UNOFFICIAL 2026-10-13: only the taps inside BlurRadius are read (those beyond weighed exactly 0 but were still
	// fetched: with the default radius 4, half of the 16 reads of each blur pass did nothing).
	[loop] for (int i = 1; i <= 8; i++) {
		if (i > BlurRadius) break;
		float gauss = exp(falloff * i * i);
		float4 a = tex2Dlod(source, float4((vpos + 0.5 + axis * i) * LowTexel, 0, 0));
		float4 b = tex2Dlod(source, float4((vpos + 0.5 - axis * i) * LowTexel, 0, 0));
		float wa = gauss / (1.0 + abs(a.b - centre.b) / tolerance);
		float wb = gauss / (1.0 + abs(b.b - centre.b) / tolerance);
		sum += a.rg * wa + b.rg * wb;
		weights += wa + wb;
	}
	return float4(sum / weights, centre.b, 1.0);
}

float4 BlurXPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 { return BlurAxis(TESR_VolumetricLightTemp, vpos, float2(1.0, 0.0)); }
float4 BlurYPS(VSOUT IN, float2 vpos : VPOS) : COLOR0 { return BlurAxis(TESR_VolumetricLightMarch, vpos, float2(0.0, 1.0)); }

// ---- 0: Composite, full resolution ----
float4 CompositePS(VSOUT IN) : COLOR0 {
	float2 uv = IN.UVCoord;
	float4 color = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0, 0));
	float depth = readDepthLod(uv);

	// Depth-aware bilinear upsample from the half-resolution buffer (texel k stands for its centre).
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

	// Henyey-Greenstein phase, times 4 pi (1 for air that scatters evenly), capped so looking straight at the sun
	// cannot blow out.
	float cosTheta = dot(normalize(toWorld(uv)), TESR_SmoothedSunDir.xyz);
	float g = Anisotropy;
	float phase = min((1.0 - g * g) / pow(max(1.0 + g * g - 2.0 * g * cosTheta, 1.0e-4), 1.5), 8.0);

	float3 lin = linearize(max(color.rgb, 0.0));
	float shadedAir = saturate(air.g - air.r);
	lin = lin * (1.0 - Darken * Fade * shadedAir) + linearize(TESR_SunColor.rgb) * (Strength * Fade * phase * air.r);
	return float4(delinearize(max(lin, 0.0)), color.a);
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
		Texture[3] = <NoTexture>;   // writes March (Temporal on) or Temp (off); reads neither
		Texture[5] = <NoTexture>;
	}
}

technique BlurX {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurXPS();
		Texture[3] = <NoTexture>;   // writes March
	}
}

technique Temporal {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 TemporalPS();
		Texture[5] = <NoTexture>;   // writes Temp
	}
}

technique BlurY {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 BlurYPS();
		Texture[4] = <NoTexture>;   // writes Buffer
	}
}
