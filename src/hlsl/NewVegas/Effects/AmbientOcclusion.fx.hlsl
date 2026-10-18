// Ambient Occlusion fullscreen shader for Oblivion/Skyrim Reloaded

#define viewao 0
#define halfres 0
#define kernelSize 5
// Set to 0 to restore the legacy technique with the new DLL (A/B testing).
#define NVR_PACKED_AO 1
float4 NVR_AOLayout; // packed extent xy, inverse low-resolution dimensions zw

float4 TESR_AmbientOcclusionAOData;
float4 TESR_AmbientOcclusionData;
float4 TESR_AmbientOcclusionGTAO;	// UNOFFICIAL 2026-10-16 ([Shaders.AmbientOcclusion.GTAO]; the GTAO techniques at the end): w largest radius (pixels)
float4 TESR_AmbientOcclusionSky;	// UNOFFICIAL 2026-10-17 Skylighting ([Shaders.AmbientOcclusion.Skylighting]): x 1 on (exteriors), y strength, z ray length (units), w depth slack (ortho map units)
float4x4 TESR_ShadowCameraToLightTransformOrtho;	// camera-relative world position -> the top-down ortho map (as WetWorld)
float4 TESR_AmbientOcclusionSky2;	// UNOFFICIAL 2026-10-17 Skylighting v2: x 1 on, y strength, z sky light A, w sun light S (Includes/SkyShare.hlsl)
float4 TESR_AmbientOcclusionSky3;	// x 1 = normal from the depth (0 = the normals buffer), y 1 / OpenThreshold, z share apply, w debug view
float4 TESR_AmbientOcclusionSky4;	// x ray length (units), y soft width (units), z sun mask bound (pass), w factor bound (DedicatedCombine)
float4 TESR_SmoothedSunDir;
float4 TESR_ShadowFade;
float4 TESR_ReciprocalResolution;
float4 TESR_FogData; // x: fog start, y: fog end, z: sun glare, w: fog power
float4 TESR_FogColor;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_SourceBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BlueNoiseSampler : register(s3) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
sampler2D NVR_AOBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
// UNOFFICIAL 2026-10-17 Skylighting: the top-down depth map NVR renders for rain (ShadowManager, MapOrtho), bound by hand to stage 6
// in AmbientOcclusionEffect::Render (an NVR_ name: TESR_ samplers are bound by their order among the TESR_ ones, and stage 5 is the
// hand-bound NVR_AOBuffer).
sampler2D NVR_OrthoMap : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
// UNOFFICIAL 2026-10-17 Skylighting v2 (bound by hand in AmbientOcclusion.cpp): the sky factor for DedicatedCombine (s7) and the sun
// shadow mask TESR_PointShadowBuffer for the sky pass (s8; an NVR_ name for the same reason as above).
sampler2D NVR_SkyVis : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D NVR_SunMask : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

static const float AOsamples = TESR_AmbientOcclusionAOData.x;
static const float AOstrength = TESR_AmbientOcclusionAOData.y;
static const float AOclamp = TESR_AmbientOcclusionAOData.z;
static const float AOrange = TESR_AmbientOcclusionAOData.w;
static const float AOangleBias = TESR_AmbientOcclusionData.x;
static const float AOlumThreshold = TESR_AmbientOcclusionData.y;
static const float blurDrop = TESR_AmbientOcclusionData.z;
static const float blurRadius = TESR_AmbientOcclusionData.w;
static const int startFade = 2000;
static const int endFade = 8000;
static const float2 io = float2(1.0f, 0.0f);
 
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
 
#include "Includes/Depth.hlsl"
#include "Includes/BlurDepth.hlsl"
#include "Includes/Helpers.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/SkyShare.hlsl"


// returns a semi random float3 between 0 and 1 based on the given seed.
// tailored to return a different value for each uv coord of the screen.
float3 random(float2 seed)
{
	return tex2D(TESR_BlueNoiseSampler, (seed/256 + 0.5) / TESR_ReciprocalResolution.xy).xyz;
}

float fogCoeff(float depth){
	return saturate(invlerp(TESR_FogData.x, TESR_FogData.y, depth));
}

float4 SSAOValue(VSOUT IN, uniform float2 OffsetMask)
{
	float2 uv = IN.UVCoord.xy;

#if halfres
	clip ((IN.UVCoord.x < 0.5 && IN.UVCoord.y < 0.5)-1); // discard half the screen to render at half resolution
	uv *= 2;
#endif
	
	// generate the sampling kernel with random points in a hemisphere
	// int kernelSize = clamp(AOsamples, 0, 32);
	float uRadius = abs(AOrange);
	float bias = saturate(AOangleBias);

	float3 origin = reconstructPosition(uv);
	if (origin.z > endFade) return 1.0;

	//reorient our sample kernel along the origin's normal
	float3 normal = GetNormal(uv);

	float angle = -random(uv).x / 2 * PI; // random angle between 0 and 90degrees
	float3 kernelRotation = float3( -sin(angle), cos(angle), 0);
	float3 tangent = normalize(kernelRotation - normal * dot(kernelRotation, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 tbn = float3x3(tangent, bitangent, normal);

	// calculate occlusion by sampling depth of each point from the kernel
	float occlusion = 0.0;
	[unroll]
	for (int i = 0; i < kernelSize; ++i) {
		// generate random samples in a unit sphere (random vector coordinates from -1 to 1);
		float3 rand = random(uv + i * TESR_ReciprocalResolution.x);
		float3 sampleVector = float3 (expand(rand.xy), rand.z) * float3(OffsetMask, 1); // separate kernel
		sampleVector = mul(normalize(sampleVector), tbn);

		//randomize points distance to sphere center, making them more concentrated towards the center
		sampleVector *= random(uv * i/2);
		float scale = 1 + float(i) / float(kernelSize);
		scale = lerp(bias, 1.0f, scale * scale);
		sampleVector *= scale; 

		// get sample positions around origin:
		sampleVector *= dot(normal, sampleVector) < 0.0 ? -1.0 : 1.0; // if our sample vector goes inside the geometry, we flip it
		float3 samplePoint = origin + sampleVector * uRadius;
		
		// compare depth of the projected sample with the value from depthbuffer
		float3 screenSpaceSample = projectPosition (samplePoint);
		float sampleDepth = readDepth(screenSpaceSample.xy);
		float actualDepth = samplePoint.z;

		// range check & accumulate:
		float distance = abs(actualDepth - sampleDepth);
		float rangeCheck = distance < uRadius ? 1.0 : 0.0;
		float influence = (sampleDepth < actualDepth ? 1.0 : 0.0 ) * rangeCheck;

		// stronger strength curve in close vectors (replacing an if statement with a lerp)
		influence *= lerp(1.0 - distance * distance/(uRadius * uRadius), 1.0 - distance /uRadius, i < kernelSize / 4);
		occlusion += influence;
	}
	
	occlusion = 1.0 - occlusion/kernelSize * AOstrength;

	float fogColor = luma(TESR_FogColor.rgb);
	float darkness = clamp(lerp(occlusion, fogColor, fogCoeff(origin.z)), occlusion, 1.0);

	darkness = lerp(darkness, 1.0, saturate(invlerp(startFade, endFade, origin.z)));

	return float2(darkness, 1.0).xxxy;
}

float4 SSAO(VSOUT IN, uniform float2 OffsetMask) : COLOR0
{
	float value = SSAOValue(IN, OffsetMask).r;
	if (OffsetMask.y) value *= tex2D(TESR_RenderedBuffer, IN.UVCoord).r;
	return float4(value, value, value, 1);
}

float4 Expand(VSOUT IN) : COLOR0
{
	float2 coord = IN.UVCoord * 0.5;
	return tex2D(TESR_RenderedBuffer, coord);
}

float4 Combine(VSOUT IN) : COLOR0
{
	float3 source = tex2D(TESR_SourceBuffer, IN.UVCoord).rgb;
	float3 color = pows(source,2.2); // linearise
	float ao = lerp(AOclamp, 1.0, tex2D(TESR_RenderedBuffer, IN.UVCoord).r);

	float luminance = luma(color);
	float lt = luminance - AOlumThreshold;
	luminance = saturate(lt * 3.0);
	ao = lerp(ao, 1.0, luminance);
    #if viewao
		return float4(ao, ao, ao, 1.0f);
	#endif

	// (source^2.2 * ao)^(1/2.2) == source * ao^(1/2.2). Keep the
	// luminance test in linear space, but do the inverse gamma once for AO
	// instead of once per color channel.
	return float4(source * pow(ao, 1.0 / 2.2), 1.0f);
}
 

// perform depth aware 12 taps blur along the direction of the offsetmask
float4 NormalBlurRChannel(VSOUT IN, uniform float2 OffsetMask, uniform float blurRadius,uniform float depthDrop,uniform float endFade) : COLOR0
{
	float WeightSum = 0.114725602f;
	float4 color1 = tex2D(TESR_RenderedBuffer, IN.UVCoord) * WeightSum;
	float3 normal = GetNormal(IN.UVCoord);
	float depth = projectedDepthFromLinear(tex2D(TESR_DepthBuffer, IN.UVCoord).x);
	
    if (invertedDepth) {
        depth = 1 - depth;
    }

    float depth1 = readDepth(IN.UVCoord);
	clip(endFade - depth1);

	// coeff for blurring to increase blur depthDrop on surfaces facing away from the camera
	float normalCoeff = (0.5 + 2 * compress(dot(normal, float3(0, 0, 1))));

    for (int i = 0; i < cKernelSize; i++)
    {
		float2 uvOff = (BlurOffsets[i] * OffsetMask) * blurRadius / max(depth, 1e-6);	// UNOFFICIAL 2026-10-16: 0 at the far plane (as the other blurs guard)
		float4 color2 = tex2D(TESR_RenderedBuffer, IN.UVCoord + uvOff).r;
		float depth2 = readDepth(IN.UVCoord + uvOff);
		float3 normal2 = GetNormal(IN.UVCoord + uvOff);

		float diff = abs(depth1 - depth2);

		int useForBlur = (diff <= depthDrop * normalCoeff);
		color1.r += BlurWeights[i] * color2.r * useForBlur;
		WeightSum += BlurWeights[i] * useForBlur;
    }
	
	color1.r /= WeightSum;
    return float4(color1.rgb, 1);
}


technique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SSAO(io.xy);
	}

	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SSAO(io.yx);
	}

#if halfres
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Expand();
	}
#endif
	
	pass
	{ 
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 NormalBlurRChannel(io.xy, blurRadius, blurDrop, endFade);
	}
	
	pass
	{ 
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 NormalBlurRChannel(io.yx, blurRadius, blurDrop, endFade);
	}
	
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Combine();
	}
}

#if NVR_PACKED_AO
// The CPU sets a half-size viewport, and copies only that rectangle after
// each intermediate pass. No additional render-target allocations are needed.
VSOUT PackedVS(VSIN IN)
{
	VSOUT OUT = FrameVS(IN);
	OUT.UVCoord += 0.5 * (NVR_AOLayout.zw - TESR_ReciprocalResolution.xy);
	return OUT;
}

float2 PackedUV(float2 uv)
{
	// Keep depth and AO paired at a low-resolution texel, including near edges.
	uv = (floor(uv / NVR_AOLayout.zw) + 0.5) * TESR_ReciprocalResolution.xy;
	return clamp(uv, 0.5 * TESR_ReciprocalResolution.xy,
		NVR_AOLayout.xy - 0.5 * TESR_ReciprocalResolution.xy);
}

float2 PackedSSAOValues(float2 uv)
{
	float uRadius = abs(AOrange);
	float bias = saturate(AOangleBias);
	float3 origin = reconstructPosition(uv);
	if (origin.z > endFade) return 1;

	float3 normal = GetNormal(uv);
	float angle = -random(uv).x / 2 * PI;
	float3 kernelRotation = float3(-sin(angle), cos(angle), 0);
	float3 tangent = normalize(kernelRotation - normal * dot(kernelRotation, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 tbn = float3x3(tangent, bitangent, normal);
	float2 occlusion = 0;

	[unroll] for (int i = 0; i < kernelSize; ++i) {
		// The horizontal and vertical kernels use the same random vectors. Evaluate both
		// together so each pair shares its two blue-noise reads and all basis setup.
		float3 rand = random(uv + i * TESR_ReciprocalResolution.x);
		float3 raw = float3(expand(rand.xy), rand.z);
		float3 sampleX = mul(normalize(raw * float3(1, 0, 1)), tbn);
		float3 sampleY = mul(normalize(raw * float3(0, 1, 1)), tbn);
		float3 randomScale = random(uv * i / 2);
		sampleX *= randomScale;
		sampleY *= randomScale;
		float scale = lerp(bias, 1.0f, pows(1 + float(i) / float(kernelSize), 2));
		sampleX *= scale * (dot(normal, sampleX) < 0 ? -1 : 1);
		sampleY *= scale * (dot(normal, sampleY) < 0 ? -1 : 1);

		float3 pointX = origin + sampleX * uRadius;
		float3 pointY = origin + sampleY * uRadius;
		float2 sampleDepth = float2(readDepth(projectPosition(pointX).xy), readDepth(projectPosition(pointY).xy));
		float2 actualDepth = float2(pointX.z, pointY.z);
		float2 distance = abs(actualDepth - sampleDepth);
		float2 influence = (sampleDepth < actualDepth) * (distance < uRadius);
		influence *= lerp(1 - distance * distance / (uRadius * uRadius), 1 - distance / uRadius, i < kernelSize / 4);
		occlusion += influence;
	}

	float2 values = 1 - occlusion / kernelSize * AOstrength;
	float fogColor = luma(TESR_FogColor.rgb);
	values = clamp(lerp(values, fogColor, fogCoeff(origin.z)), values, 1);
	return lerp(values, 1, saturate(invlerp(startFade, endFade, origin.z)));
}

// UNOFFICIAL 2026-10-16 GTAO: ground-truth based ambient occlusion (Jimenez, Wu, Pesce, Jarabo 2016, "Practical Real-Time
// Strategies for Accurate Indirect Occlusion"; written from the paper's equations, Intel's XeGTAO (MIT licence) read as a
// reference). In 2 screen-space slices per pixel, the highest horizon on each side is searched (4 depth reads a side), and the
// cosine-weighted visibility between the two horizons is integrated exactly. Flat open ground stays unoccluded (the SSAO's
// random points dip below a surface and grey it), corners and creases darken by how closed they really are, and things far
// in front (past the radius) fade out instead of leaving a halo. Same radius, strength, fog and fade as the SSAO.
float3 GTAOPosition(float2 uv)
{
	// (On a depth texel's centre: the linear filter would blend a silhouette's two depths into a surface that is not there.)
	return reconstructPosition((floor(uv / TESR_ReciprocalResolution.xy) + 0.5) * TESR_ReciprocalResolution.xy);
}

float GTAOValue(float2 uv)
{
	float3 P = reconstructPosition(uv);
	if (P.z > endFade) return 1.0;
	float3 V = normalize(-P);
	float3 N = normalize(GetNormal(uv));
	N = dot(N, V) < 0.0 ? -N : N;
	float radius = abs(AOrange);
	// The radius on screen (uv per unit of slice direction), capped at w pixels.
	float2 radiusUV = radius / P.z * 0.5 * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]);
	radiusUV *= min(1.0, TESR_AmbientOcclusionGTAO.w * TESR_ReciprocalResolution.x / max(radiusUV.x, 1e-6));
	float3 noise = random(uv);
	// Falloff (as XeGTAO): samples past 0.385 x radius count less, none past the radius.
	float falloffMul = -1.0 / (0.615 * radius);
	float falloffAdd = (1.0 - 0.615) / 0.615 + 1.0;
	float visibility = 0.0;
	[unroll] for (int slice = 0; slice < 2; slice++) {
		float phi = (slice + noise.x) * (PI / 2.0);
		float2 omega = float2(cos(phi), sin(phi));
		float3 direction = float3(omega, 0.0);		// view space: x right, y up
		float2 stepUV = float2(omega.x, -omega.y) * radiusUV;	// (screen v runs down)
		float3 orthoDirection = direction - dot(direction, V) * V;
		float3 axis = normalize(cross(orthoDirection, V));
		float3 projectedN = N - axis * dot(N, axis);
		float projectedLength = length(projectedN);
		float cosN = saturate(dot(projectedN, V) / max(projectedLength, 1e-4));
		float n = (dot(orthoDirection, projectedN) < 0.0 ? -1.0 : 1.0) * acos(cosN);
		float low0 = cos(n + PI / 2.0);		// the tangent plane: the lowest each side's horizon can be
		float low1 = cos(n - PI / 2.0);
		float horizon0 = low0, horizon1 = low1;
		[unroll] for (int j = 0; j < 4; j++) {
			float t = (j + noise.y) / 4.0;
			t *= t;		// more samples near the pixel
			float2 offset = stepUV * t + float2(omega.x, -omega.y) * TESR_ReciprocalResolution.xy * 2.0;	// at least one (half-res) texel out
			float3 s0 = GTAOPosition(uv + offset) - P;
			float3 s1 = GTAOPosition(uv - offset) - P;
			float l0 = length(s0), l1 = length(s1);
			horizon0 = max(horizon0, lerp(low0, dot(s0, V) / max(l0, 1e-4), saturate(l0 * falloffMul + falloffAdd)));
			horizon1 = max(horizon1, lerp(low1, dot(s1, V) / max(l1, 1e-4), saturate(l1 * falloffMul + falloffAdd)));
		}
		float h0 = n + clamp(-acos(horizon1) - n, -PI / 2.0, PI / 2.0);	// against omega
		float h1 = n + clamp(acos(horizon0) - n, -PI / 2.0, PI / 2.0);	// along omega
		float sinN = sin(n);
		visibility += projectedLength * ((cosN + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) + (cosN + 2.0 * h1 * sinN - cos(2.0 * h1 - n))) * 0.25;
	}
	visibility *= 0.5;
	float darkness = saturate(1.0 - (1.0 - saturate(visibility)) * AOstrength);
	float fogColor = luma(TESR_FogColor.rgb);
	darkness = clamp(lerp(darkness, fogColor, fogCoeff(P.z)), darkness, 1.0);
	return lerp(darkness, 1.0, saturate(invlerp(startFade, endFade, P.z)));
}

// UNOFFICIAL 2026-10-17 Skylighting ([Shaders.AmbientOcclusion.Skylighting], exteriors): how much of the sky a point sees, from the
// top-down ortho map (the highest surface over each spot, rendered for rain). A height-field test: straight up and 6 rays on two
// rings (60 and 30 degrees up, turned per pixel by the blue noise; the AO blur smooths it), each probed at a third and at the full
// ray length; a ray is open when, at both probes, nothing in the map lies above it. Open rays count by their cosine to the
// surface normal. Outside the map counts as open sky. Written from the idea of Community Shaders' Skylighting (sky occlusion from
// a precipitation-occlusion map; research\skylighting-map.md), not from its code. Only tex2Dlod: it runs behind a branch.
float SkyOpen(float3 relativeWorld)
{
	float4 o = mul(float4(relativeWorld, 1.0f), TESR_ShadowCameraToLightTransformOrtho);
	o.xyz /= o.w;
	if (abs(o.x) > 1.0f || abs(o.y) > 1.0f || o.z < 0.0f || o.z > 1.0f) return 1.0f;
	float top = tex2Dlod(NVR_OrthoMap, float4(o.x * 0.5f + 0.5f, o.y * -0.5f + 0.5f, 0.0f, 0.0f)).r;
	return top > o.z - TESR_AmbientOcclusionSky.w ? 1.0f : 0.0f;
}

float SkyVisibility(float3 viewPosition, float3 viewNormal, float turn)
{
	float3 p = mul(float4(viewPosition, 0.0f), TESR_InvViewTransform).xyz;
	float3 n = normalize(mul(float4(viewNormal, 0.0f), TESR_InvViewTransform).xyz);
	p += n * 16.0f;	// off the surface's own texel of the map
	float reach = TESR_AmbientOcclusionSky.z;
	float up = max(n.z, 0.0f);
	float open = up * SkyOpen(p + float3(0.0f, 0.0f, reach / 3.0f)) * SkyOpen(p + float3(0.0f, 0.0f, reach));
	float total = up;
	[unroll] for (int i = 0; i < 6; i++) {
		float a = turn + i * (PI / 3.0f);
		float e = fmod((float)i, 2.0f) > 0.5f ? 0.5236f : 1.0472f;	// 30 or 60 degrees up
		float3 dir = float3(cos(a) * cos(e), sin(a) * cos(e), sin(e));
		float w = max(dot(dir, n), 0.0f);
		open += w * SkyOpen(p + dir * (reach / 3.0f)) * SkyOpen(p + dir * reach);
		total += w;
	}
	return lerp(1.0f, total > 1.0e-4f ? open / total : 1.0f, TESR_AmbientOcclusionSky.y);
}

// The estimate pass of the GTAO techniques: the same outputs as PackedEstimate (AO, depth).
float4 GTAOEstimate(VSOUT IN) : COLOR0
{
	float ao = GTAOValue(IN.UVCoord);
	float3 skyOrigin = reconstructPosition(IN.UVCoord);
	float3 skyNormal = GetNormal(IN.UVCoord);
	float skyTurn = random(IN.UVCoord).y * 2.0f * PI;
	[branch] if (TESR_AmbientOcclusionSky.x > 0.0f && skyOrigin.z < endFade) ao *= SkyVisibility(skyOrigin, skyNormal, skyTurn);	// UNOFFICIAL Skylighting
	return float4(ao, min(readDepth(IN.UVCoord), (float)endFade), 0, 1);
}

float4 PackedEstimate(VSOUT IN) : COLOR0
{
	// Same two five-sample kernels, evaluated together without duplicate setup/noise reads.
	float2 values = PackedSSAOValues(IN.UVCoord);
	float ao = values.x * values.y;
	// UNOFFICIAL 2026-10-17 Skylighting: the reads that need gradients at top level, the map test behind the branch.
	float3 skyOrigin = reconstructPosition(IN.UVCoord);
	float3 skyNormal = GetNormal(IN.UVCoord);
	float skyTurn = random(IN.UVCoord).y * 2.0f * PI;
	[branch] if (TESR_AmbientOcclusionSky.x > 0.0f && skyOrigin.z < endFade) ao *= SkyVisibility(skyOrigin, skyNormal, skyTurn);
	return float4(ao, min(readDepth(IN.UVCoord), (float)endFade), 0, 1);
}

float4 PackedBlur(VSOUT IN, uniform float2 axis) : COLOR0
{
	float2 center = tex2D(TESR_RenderedBuffer, PackedUV(IN.UVCoord)).rg;
	if (center.y >= endFade) return float4(1, center.y, 0, 1);
	float rawDepth = projectedDepthFromLinear(tex2D(TESR_DepthBuffer, IN.UVCoord).x);
	if (invertedDepth) rawDepth = 1 - rawDepth;
	float depthTolerance = blurDrop * (0.5 + 2 * compress(GetNormal(IN.UVCoord).z));
	float sum = center.x * 0.114725602f;
	float weights = 0.114725602f;
	[unroll] for (int i = 0; i < cKernelSize; ++i) {
		// Preserve the original screen-space blur radius, not twice its width.
		float2 uv = IN.UVCoord + BlurOffsets[i] * axis * blurRadius / max(rawDepth, 1.0e-6);
		float2 sample = tex2D(TESR_RenderedBuffer, PackedUV(uv)).rg;
		float weight = BlurWeights[i] * (abs(center.y - sample.y) <= depthTolerance);
		sum += sample.x * weight;
		weights += weight;
	}
	return float4(sum / weights, center.y, 0, 1);
}

float4 PackedCombine(VSOUT IN) : COLOR0
{
	float2 position = IN.UVCoord / NVR_AOLayout.zw - 0.5;
	float2 base = floor(position);
	float2 fraction = frac(position);
	float depth = readDepth(IN.UVCoord);
	if (depth >= endFade) return float4(tex2D(TESR_SourceBuffer, IN.UVCoord).rgb, 1);
	float sum = 0, weights = 0;
	[unroll] for (int y = 0; y < 2; ++y) {
		[unroll] for (int x = 0; x < 2; ++x) {
			float2 uv = (base + float2(x, y) + 0.5) * NVR_AOLayout.zw;
			float2 sample = tex2D(TESR_RenderedBuffer, PackedUV(uv)).rg;
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1 + abs(sample.y - depth) / max(blurDrop, 0.001);
			sum += sample.x * weight;
			weights += weight;
		}
	}
	float ao = lerp(AOclamp, 1, sum / max(weights, 1.0e-6));
	float3 source = tex2D(TESR_SourceBuffer, IN.UVCoord).rgb;
	float3 color = pows(source, 2.2);
	ao = lerp(ao, 1, saturate((luma(color) - AOlumThreshold) * 3));
	#if viewao
		return float4(ao, ao, ao, 1);
	#endif
	return float4(source * pow(ao, 1.0 / 2.2), 1);
}

// UNOFFICIAL 2026-10-17 Skylighting v2: the sky visibility V at the AO's resolution, from the top-down ortho map (the highest surface over
// each spot; ShadowManager draws it for v2 with alpha test and without blended-only geometry, camera-centred, redrawn when the camera
// moves). A height-field visibility test written for this mod: straight up and 8 azimuths x 2 elevations (60 and 30 degrees up; the
// azimuths 22.5 degrees off the world axes, so they do not run along walls), each ray probed at 0.4 and the full ray length; it is
// open where the map's top lies below both probes. Each ray stands for the solid angle of its band (0-45, 45-75, 75-90 degrees up)
// weighted by its cosine to the normal, so a ground pixel's estimate follows the cosine-weighted sky visibility. The comparison is a
// depth ramp (SoftWidth) instead of a step, and nothing here is random: no noise for the composites' upsample to show. Height-field
// sky/ambient occlusion: Max 1988 (horizon mapping), Sloan & Cohen 2000; the shipped D3D9-era precedent is CryEngine 3's height-map
// AO; Community Shaders' Skylighting (sky occlusion from the precipitation map) gave the idea of using the rain's map (method only).
// Its output: (the factor for the sky's share, Includes/SkyShare.hlsl SkyFactor; depth), at the AO's resolution: the composites
// upsample it with the AO's own weights.
// (Outside the map the sampler's border (1 = the far plane) makes every probe open; a probe above the map's top is open too.)
sampler2D NVR_OrthoMapBorder : register(s6) = sampler_state { ADDRESSU = BORDER; ADDRESSV = BORDER; BORDERCOLOR = 0xFFFFFFFF; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
float SkyOpenV2(float3 o, float softScale)
{
	float top = tex2Dlod(NVR_OrthoMapBorder, float4(o.x * 0.5f + 0.5f, o.y * -0.5f + 0.5f, 0.0f, 0.0f)).r;
	return saturate((top - o.z) * softScale + 0.5f);	// the map's depth beyond the probe's = its surface is below the probe
}

float4 SkyVisibilityV2PS(VSOUT IN) : COLOR0
{
	// On a full-resolution depth texel (the linear filter would blend a silhouette's two depths into a surface that is not there).
	float2 uv = (floor(IN.UVCoord / TESR_ReciprocalResolution.xy) + 0.5f) * TESR_ReciprocalResolution.xy;
	float depth = readDepth(uv);
	float3 viewPosition = reconstructPosition(uv);
	float3 viewNormal = GetNormal(uv);
	// The surface's normal from the depth itself (on each axis the neighbour with the smaller depth step, so edges keep their own
	// side's slope). Launch 1b showed the normals buffer is unusable here: Goodsprings' road meshes and ground stains (and the I-15
	// road) give normals pointing away from the sky, which put the test's start point under the ground: 'covered' and V ~0.3 on
	// open sunlit road. v1 used the same buffer (its open-road darkening).
	float3 px0 = reconstructPosition(uv - float2(TESR_ReciprocalResolution.x, 0.0f)), px1 = reconstructPosition(uv + float2(TESR_ReciprocalResolution.x, 0.0f));
	float3 py0 = reconstructPosition(uv - float2(0.0f, TESR_ReciprocalResolution.y)), py1 = reconstructPosition(uv + float2(0.0f, TESR_ReciprocalResolution.y));
	float3 ddxP = abs(px1.z - viewPosition.z) < abs(viewPosition.z - px0.z) ? px1 - viewPosition : viewPosition - px0;
	float3 ddyP = abs(py1.z - viewPosition.z) < abs(viewPosition.z - py0.z) ? py1 - viewPosition : viewPosition - py0;
	float3 depthNormal = normalize(cross(ddxP, ddyP));
	depthNormal = dot(depthNormal, viewPosition) > 0.0f ? -depthNormal : depthNormal;	// facing the camera
	viewNormal = TESR_AmbientOcclusionSky3.x > 0.5f ? depthNormal : viewNormal;
	float result = 1.0f;
	float sunMask = TESR_AmbientOcclusionSky4.z > 0.5f ? lerp(TESR_ShadowFade.x, 1.0f, tex2Dlod(NVR_SunMask, float4(uv, 0.0f, 0.0f)).r) : 1.0f;
	[branch] if (depth < endFade) {
		float3 p = mul(float4(viewPosition, 0.0f), TESR_InvViewTransform).xyz;
		float3 n = normalize(mul(float4(viewNormal, 0.0f), TESR_InvViewTransform).xyz);
		p += n * 24.0f;	// off the surface's own texels of the map (~10 units each)
		float4 o4 = mul(float4(p, 1.0f), TESR_ShadowCameraToLightTransformOrtho);
		float3 o = o4.xyz / o4.w;
		// The map is orthographic: a world offset d moves the map position by d x M (row vectors), the same everywhere.
		float3x3 M = (float3x3)TESR_ShadowCameraToLightTransformOrtho;
		float reach = TESR_AmbientOcclusionSky4.x;
		float softScale = 1.0f / max(TESR_AmbientOcclusionSky4.y * abs(M[2][2]), 1.0e-9f);
		{
			float total = 0.034f * saturate(n.z);
			float open = total * SkyOpenV2(o + M[2] * (reach * 0.4f), softScale) * SkyOpenV2(o + M[2] * reach, softScale);
			[unroll] for (int i = 0; i < 8; i++) {
				float a = (i + 0.5f) * (PI / 4.0f);
				float2 h = float2(cos(a), sin(a));
				[unroll] for (int k = 0; k < 2; k++) {
					float3 dir = k ? float3(h * 0.8660254f, 0.5f) : float3(h * 0.5f, 0.8660254f);	// 30 / 60 degrees up
					float w = (k ? 0.0884f : 0.0324f) * saturate(dot(dir, n));	// band solid angle / 2 pi / 8 x cosine
					float3 stepO = mul(dir, M);
					open += w * SkyOpenV2(o + stepO * (reach * 0.4f), softScale) * SkyOpenV2(o + stepO * reach, softScale);
					total += w;
				}
			}
			float visibility = total > 1.0e-5f ? open / total : 1.0f;
			// Fade to open towards the map's edge (~3000 units round the camera), so its border never shows as a ring.
			visibility = lerp(visibility, 1.0f, saturate((max(abs(o.x), abs(o.y)) - 0.8f) * 5.0f));
			// Nearly open sky counts as open (OpenThreshold): the last few per cent of a height-field estimate on open ground near
			// buildings are below what it can tell apart (no openings under eaves, a coarse map), and open sunlit ground keeps today's look.
			visibility = saturate(visibility * TESR_AmbientOcclusionSky3.y);
			float covered = 1.0f - SkyOpenV2(o + M[2] * 32.0f, softScale);	// (debug view 4: the map's top is above the point)
			result = SkyFactor(visibility, n, sunMask, covered);
		}
	}
	return float4(result, min(depth, (float)endFade), 0, 1);
}

technique SkyVisibilityV2
{
	pass Sky { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 SkyVisibilityV2PS(); }
}

technique PackedAO
{
	pass Estimate { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedEstimate(); }
	pass BlurX { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedBlur(io.xy); }
	pass BlurY { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedBlur(io.yx); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 PackedCombine(); }
}

float2 DedicatedUV(float2 uv)
{
	return clamp(uv, 0.5 * NVR_AOLayout.zw, 1 - 0.5 * NVR_AOLayout.zw);
}

float4 DedicatedBlur(VSOUT IN, uniform float2 axis) : COLOR0
{
	float2 center = tex2D(NVR_AOBuffer, DedicatedUV(IN.UVCoord)).rg;
	if (center.y >= endFade) return float4(1, center.y, 0, 1);
	float rawDepth = projectedDepthFromLinear(tex2D(TESR_DepthBuffer, IN.UVCoord).x);
	if (invertedDepth) rawDepth = 1 - rawDepth;
	float depthTolerance = blurDrop * (0.5 + 2 * compress(GetNormal(IN.UVCoord).z));
	float sum = center.x * 0.114725602f;
	float weights = 0.114725602f;
	[unroll] for (int i = 0; i < cKernelSize; ++i) {
		float2 uv = IN.UVCoord + BlurOffsets[i] * axis * blurRadius / max(rawDepth, 1.0e-6);
		float2 sample = tex2D(NVR_AOBuffer, DedicatedUV(uv)).rg;
		float weight = BlurWeights[i] * (abs(center.y - sample.y) <= depthTolerance);
		sum += sample.x * weight;
		weights += weight;
	}
	return float4(sum / weights, center.y, 0, 1);
}

float4 DedicatedCombine(VSOUT IN) : COLOR0
{
	float2 position = IN.UVCoord / NVR_AOLayout.zw - 0.5;
	float2 base = floor(position);
	float2 fraction = frac(position);
	float depth = readDepth(IN.UVCoord);
	if (depth >= endFade) return float4(tex2D(TESR_SourceBuffer, IN.UVCoord).rgb, 1);
	float sum = 0, weights = 0;
	[unroll] for (int y = 0; y < 2; ++y) {
		[unroll] for (int x = 0; x < 2; ++x) {
			float2 uv = (base + float2(x, y) + 0.5) * NVR_AOLayout.zw;
			float2 sample = tex2D(NVR_AOBuffer, DedicatedUV(uv)).rg;
			float weight = (x ? fraction.x : 1 - fraction.x) * (y ? fraction.y : 1 - fraction.y);
			weight /= 1 + abs(sample.y - depth) / max(blurDrop, 0.001);
			sum += sample.x * weight;
			weights += weight;
		}
	}
	float ao = lerp(AOclamp, 1, sum / max(weights, 1.0e-6));
	float3 source = tex2D(TESR_SourceBuffer, IN.UVCoord).rgb;
	float3 linearColor = pows(source, 2.2);
	ao = lerp(ao, 1, saturate((luma(linearColor) - AOlumThreshold) * 3));
	#if viewao
		return float4(ao, ao, ao, 1);
	#endif
	float3 result = source * pow(ao, 1.0 / 2.2);
	[branch] if (TESR_AmbientOcclusionSky4.w > 0.5f) {	// UNOFFICIAL Skylighting v2: the sky factor, with the AO's upsample weights
		float skySum = 0, skyWeights = 0;
		[unroll] for (int sy = 0; sy < 2; ++sy) {
			[unroll] for (int sx = 0; sx < 2; ++sx) {
				float2 uv = (base + float2(sx, sy) + 0.5) * NVR_AOLayout.zw;
				float2 sample = tex2Dlod(NVR_AOBuffer, float4(DedicatedUV(uv), 0, 0)).rg;
				float weight = (sx ? fraction.x : 1 - fraction.x) * (sy ? fraction.y : 1 - fraction.y);
				weight /= 1 + abs(sample.y - depth) / max(blurDrop, 0.001);
				skySum += tex2Dlod(NVR_SkyVis, float4(DedicatedUV(uv), 0, 0)).r * weight;
				skyWeights += weight;
			}
		}
		result *= pow(saturate(skySum / max(skyWeights, 1.0e-6)), 1.0 / 2.2);	// (a linear factor on a gamma colour)
	}
	return float4(result, 1);
}

technique DedicatedAO
{
	pass Estimate { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedEstimate(); }
	pass BlurX { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlur(io.xy); }
	pass BlurY { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlur(io.yx); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 DedicatedCombine(); }
}

// UNOFFICIAL 2026-10-16: the same two techniques with the GTAO estimate (AmbientOcclusion.cpp picks them with the switch on).
technique PackedGTAO
{
	pass Estimate { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 GTAOEstimate(); }
	pass BlurX { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedBlur(io.xy); }
	pass BlurY { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedBlur(io.yx); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 PackedCombine(); }
}

technique DedicatedGTAO
{
	pass Estimate { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 GTAOEstimate(); }
	pass BlurX { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlur(io.xy); }
	pass BlurY { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlur(io.yx); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 DedicatedCombine(); }
}
// UNOFFICIAL 2026-10-17 FastPaths ([Shaders.AmbientOcclusion.FastPaths] Enabled; AmbientOcclusion.cpp picks DedicatedAOFast):
// the same picture, the far-pixel early returns made real. The game's effect compiler (D3DX 9.43) cannot branch around reads that
// need screen derivatives (tex2D), so it had turned 'if (origin.z > endFade) return 1' (estimate) and 'if (center.y >= endFade)
// return' (both blurs) into selects at the end: every pixel, sky and distant land included, ran the whole 10-sample kernel and the
// whole blur. Here the reads at the pixel itself stay tex2D in uniform flow (the same reads as before), and the reads behind the
// branch are tex2Dlod level 0 on single-level targets (depth, AO ping-pong, normals) or the point-sampled, no-mip blue noise: the
// same texels and weights. Arithmetic is the original's, line for line.
float3 randomLod(float2 seed)
{
	return tex2Dlod(TESR_BlueNoiseSampler, float4((seed/256 + 0.5) / TESR_ReciprocalResolution.xy, 0, 0)).xyz;
}

float2 PackedSSAOValuesFast(float2 uv, float3 origin, float3 normal, float3 noise0)
{
	// noise0 = random(uv), read at the top (the original's compile shares that read for the angle, i = 0 and i = 2's scale)
	float uRadius = abs(AOrange);
	float bias = saturate(AOangleBias);
	float angle = -noise0.x / 2 * PI;
	float3 kernelRotation = float3(-sin(angle), cos(angle), 0);
	float3 tangent = normalize(kernelRotation - normal * dot(kernelRotation, normal));
	float3 bitangent = cross(normal, tangent);
	float3x3 tbn = float3x3(tangent, bitangent, normal);
	float2 occlusion = 0;

	[unroll] for (int i = 0; i < kernelSize; ++i) {
		float3 rand = i == 0 ? noise0 : randomLod(uv + i * TESR_ReciprocalResolution.x);
		float3 raw = float3(expand(rand.xy), rand.z);
		float3 sampleX = mul(normalize(raw * float3(1, 0, 1)), tbn);
		float3 sampleY = mul(normalize(raw * float3(0, 1, 1)), tbn);
		float3 randomScale = i == 2 ? noise0 : randomLod(uv * i / 2);	// (i = 2: uv * 2 / 2 = uv, the same read)
		sampleX *= randomScale;
		sampleY *= randomScale;
		float scale = lerp(bias, 1.0f, pows(1 + float(i) / float(kernelSize), 2));
		sampleX *= scale * (dot(normal, sampleX) < 0 ? -1 : 1);
		sampleY *= scale * (dot(normal, sampleY) < 0 ? -1 : 1);

		float3 pointX = origin + sampleX * uRadius;
		float3 pointY = origin + sampleY * uRadius;
		float2 sampleDepth = float2(readDepthLod(projectPosition(pointX).xy), readDepthLod(projectPosition(pointY).xy));
		float2 actualDepth = float2(pointX.z, pointY.z);
		float2 distance = abs(actualDepth - sampleDepth);
		float2 influence = (sampleDepth < actualDepth) * (distance < uRadius);
		influence *= lerp(1 - distance * distance / (uRadius * uRadius), 1 - distance / uRadius, i < kernelSize / 4);
		occlusion += influence;
	}

	float2 values = 1 - occlusion / kernelSize * AOstrength;
	float fogColor = luma(TESR_FogColor.rgb);
	values = clamp(lerp(values, fogColor, fogCoeff(origin.z)), values, 1);
	return lerp(values, 1, saturate(invlerp(startFade, endFade, origin.z)));
}

float4 PackedEstimateFast(VSOUT IN) : COLOR0
{
	float3 skyOrigin = reconstructPosition(IN.UVCoord);
	float3 skyNormal = GetNormal(IN.UVCoord);
	float3 noise0 = random(IN.UVCoord);
	float skyTurn = noise0.y * 2.0f * PI;
	float storedDepth = min(readDepth(IN.UVCoord), (float)endFade);
	float ao = 1.0f;
	[branch] if (!(skyOrigin.z > endFade)) {
		float2 values = PackedSSAOValuesFast(IN.UVCoord, skyOrigin, skyNormal, noise0);
		ao = values.x * values.y;
	}
	[branch] if (TESR_AmbientOcclusionSky.x > 0.0f && skyOrigin.z < endFade) ao *= SkyVisibility(skyOrigin, skyNormal, skyTurn);
	return float4(ao, storedDepth, 0, 1);
}

float4 DedicatedBlurFast(VSOUT IN, uniform float2 axis) : COLOR0
{
	float2 center = tex2D(NVR_AOBuffer, DedicatedUV(IN.UVCoord)).rg;
	[branch] if (center.y >= endFade) return float4(1, center.y, 0, 1);
	float rawDepth = projectedDepthFromLinear(tex2Dlod(TESR_DepthBuffer, float4(IN.UVCoord, 0, 0)).x);
	if (invertedDepth) rawDepth = 1 - rawDepth;
	float depthTolerance = blurDrop * (0.5 + 2 * compress((tex2Dlod(TESR_NormalsBuffer, float4(IN.UVCoord, 0, 0)).xyz * 2 - 1).z));
	float sum = center.x * 0.114725602f;
	float weights = 0.114725602f;
	[unroll] for (int i = 0; i < cKernelSize; ++i) {
		float2 uv = IN.UVCoord + BlurOffsets[i] * axis * blurRadius / max(rawDepth, 1.0e-6);
		float2 sample = tex2Dlod(NVR_AOBuffer, float4(DedicatedUV(uv), 0, 0)).rg;
		float weight = BlurWeights[i] * (abs(center.y - sample.y) <= depthTolerance);
		sum += sample.x * weight;
		weights += weight;
	}
	return float4(sum / weights, center.y, 0, 1);
}

technique DedicatedAOFast
{
	pass Estimate { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 PackedEstimateFast(); }
	pass BlurX { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlurFast(io.xy); }
	pass BlurY { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlurFast(io.yx); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 DedicatedCombine(); }
}

// UNOFFICIAL 2026-10-17 FastPaths for GTAO (DedicatedGTAOFast, picked with GTAO and FastPaths both on): the far-pixel return of
// GTAOValue made real, as in DedicatedAOFast (the compiler had flattened it: every pixel ran the 2 x 8 horizon taps).
float3 GTAOPositionLod(float2 uv)
{
	float2 texel = (floor(uv / TESR_ReciprocalResolution.xy) + 0.5) * TESR_ReciprocalResolution.xy;
	float x = texel.x * 2 - 1;
	float y = (1 - texel.y) * 2 - 1;
	float z = projectedDepthFromLinear(tex2Dlod(TESR_DepthBuffer, float4(texel, 0, 0)).x);
	float4 viewSpace = mul(float4(x, y, z, 1.0f), TESR_InvProjectionTransform);
	viewSpace /= viewSpace.w;
	return viewSpace.xyz;
}

float GTAOValueFast(float2 uv, float3 P, float3 normalRaw, float3 noise)
{
	float3 V = normalize(-P);
	float3 N = normalize(normalRaw);
	N = dot(N, V) < 0.0 ? -N : N;
	float radius = abs(AOrange);
	float2 radiusUV = radius / P.z * 0.5 * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]);
	radiusUV *= min(1.0, TESR_AmbientOcclusionGTAO.w * TESR_ReciprocalResolution.x / max(radiusUV.x, 1e-6));
	float falloffMul = -1.0 / (0.615 * radius);
	float falloffAdd = (1.0 - 0.615) / 0.615 + 1.0;
	float visibility = 0.0;
	[unroll] for (int slice = 0; slice < 2; slice++) {
		float phi = (slice + noise.x) * (PI / 2.0);
		float2 omega = float2(cos(phi), sin(phi));
		float3 direction = float3(omega, 0.0);
		float2 stepUV = float2(omega.x, -omega.y) * radiusUV;
		float3 orthoDirection = direction - dot(direction, V) * V;
		float3 axis = normalize(cross(orthoDirection, V));
		float3 projectedN = N - axis * dot(N, axis);
		float projectedLength = length(projectedN);
		float cosN = saturate(dot(projectedN, V) / max(projectedLength, 1e-4));
		float n = (dot(orthoDirection, projectedN) < 0.0 ? -1.0 : 1.0) * acos(cosN);
		float low0 = cos(n + PI / 2.0);
		float low1 = cos(n - PI / 2.0);
		float horizon0 = low0, horizon1 = low1;
		[unroll] for (int j = 0; j < 4; j++) {
			float t = (j + noise.y) / 4.0;
			t *= t;
			float2 offset = stepUV * t + float2(omega.x, -omega.y) * TESR_ReciprocalResolution.xy * 2.0;
			float3 s0 = GTAOPositionLod(uv + offset) - P;
			float3 s1 = GTAOPositionLod(uv - offset) - P;
			float l0 = length(s0), l1 = length(s1);
			horizon0 = max(horizon0, lerp(low0, dot(s0, V) / max(l0, 1e-4), saturate(l0 * falloffMul + falloffAdd)));
			horizon1 = max(horizon1, lerp(low1, dot(s1, V) / max(l1, 1e-4), saturate(l1 * falloffMul + falloffAdd)));
		}
		float h0 = n + clamp(-acos(horizon1) - n, -PI / 2.0, PI / 2.0);
		float h1 = n + clamp(acos(horizon0) - n, -PI / 2.0, PI / 2.0);
		float sinN = sin(n);
		visibility += projectedLength * ((cosN + 2.0 * h0 * sinN - cos(2.0 * h0 - n)) + (cosN + 2.0 * h1 * sinN - cos(2.0 * h1 - n))) * 0.25;
	}
	visibility *= 0.5;
	float darkness = saturate(1.0 - (1.0 - saturate(visibility)) * AOstrength);
	float fogColor = luma(TESR_FogColor.rgb);
	darkness = clamp(lerp(darkness, fogColor, fogCoeff(P.z)), darkness, 1.0);
	return lerp(darkness, 1.0, saturate(invlerp(startFade, endFade, P.z)));
}

float4 GTAOEstimateFast(VSOUT IN) : COLOR0
{
	float3 skyOrigin = reconstructPosition(IN.UVCoord);
	float3 skyNormal = GetNormal(IN.UVCoord);
	float3 noise = random(IN.UVCoord);
	float skyTurn = noise.y * 2.0f * PI;
	float storedDepth = min(readDepth(IN.UVCoord), (float)endFade);
	float ao = 1.0f;
	[branch] if (!(skyOrigin.z > endFade)) ao = GTAOValueFast(IN.UVCoord, skyOrigin, skyNormal, noise);
	[branch] if (TESR_AmbientOcclusionSky.x > 0.0f && skyOrigin.z < endFade) ao *= SkyVisibility(skyOrigin, skyNormal, skyTurn);
	return float4(ao, storedDepth, 0, 1);
}

technique DedicatedGTAOFast
{
	pass Estimate { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 GTAOEstimateFast(); }
	pass BlurX { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlurFast(io.xy); }
	pass BlurY { VertexShader = compile vs_3_0 PackedVS(); PixelShader = compile ps_3_0 DedicatedBlurFast(io.yx); }
	pass Combine { VertexShader = compile vs_3_0 FrameVS(); PixelShader = compile ps_3_0 DedicatedCombine(); }
}

#endif
