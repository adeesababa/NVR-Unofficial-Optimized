// Snow accumulation fullscreen shader for Oblivion Reloaded

float4x4 TESR_WorldViewProjectionTransform;
float4x4 TESR_ShadowCameraToLightTransformOrtho;
float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SunAmbient;
float4 TESR_SnowAccumulationParams; // x:BlurNormDropThreshhold, y:BlurRadiusMultiplier, z:SunPower, w:SnowAmount 
float4 TESR_SnowAccumulationColor;
float4 TESR_WaterSettings;
float4 TESR_ShadowFade;
float4 TESR_ShadowData;
float4 TESR_ShadowScreenSpaceData;
float4 TESR_OrthoData;

float4 TESR_ShadowLightPosition[12];
float4 TESR_LightPosition[12];
float4 TESR_LightColor[24];

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_SourceBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_DepthBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_OrthoMapBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_PointShadowBuffer : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_SnowNormSampler : register(s6) < string ResourceName = "Precipitations\snow_NRM.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BlueNoiseSampler : register(s7) < string ResourceName = "Effects\bluenoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
// UNOFFICIAL 2026-10-27 Persistent ([Shaders.SnowAccumulation.Main] Persistent, CoverMap.h): snow depth kept per spot. Declared last
// (samplers bind in declaration order); only technique 1 reads it.
sampler2D TESR_CoverMapBuffer : register(s8) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
#include "Includes/CoverMap.hlsl"

static const int KernelSize = 24;
static const float diffusePower = 0.56f * TESR_SnowAccumulationParams.z;
static const float specularPower = 1.0f * TESR_SnowAccumulationParams.z;
static const float fresnelPower = 0.65f * TESR_SnowAccumulationParams.z;
static const float DARKNESS = 1-TESR_ShadowData.y;
static const float orthoRadius = TESR_SnowAccumulationParams.x; // radius of the area to sample ortho from
static const float noiseSize = 200; // scale for the noise used for snow coverage
static const float useShadows = TESR_ShadowScreenSpaceData.x || TESR_ShadowFade.y;


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

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/BlurDepth.hlsl"


float GetOrtho(float4 worldPos){
    float thickness = 0.002f; // thickness of the valid areas around the ortho map depth that will receive the effect (cancels out too far above or below ortho value)

	// get puddle mask from ortho map
	float4 ortho_pos = mul(worldPos, TESR_ShadowCameraToLightTransformOrtho);

	// apply perspective (perspective division) and convert from -1/1 to range to 0/1 (shadowMap range);
	ortho_pos.xyz /= ortho_pos.w;
	ortho_pos.x = ortho_pos.x * 0.5f + 0.5f;
	ortho_pos.y = ortho_pos.y * -0.5f + 0.5f;

	float outOfBounds = (saturate(ortho_pos.x) != ortho_pos.x) || (saturate(ortho_pos.y) != ortho_pos.y);

	float ortho = tex2D(TESR_OrthoMapBuffer, ortho_pos.xy).r; // ortho height

	float aboveGround = ortho_pos.z < ortho + thickness;
	float belowGround = ortho_pos.z > ortho - thickness;

	return outOfBounds || (belowGround && aboveGround);
}


// hash based 3d value noise
// function taken from https://www.shadertoy.com/view/XslGRr
// Created by inigo quilez - iq/2013
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License.
// ported from GLSL to HLSL by https://stackoverflow.com/questions/15628039/simplex-noise-shader

float hash( float n )
{
    return frac(sin(n)*43758.5453);
}

float noise( float3 x )
{
    // The noise function returns a value in the range -1.0f -> 1.0f

    float3 p = floor(x);
    float3 f = frac(x);

    f       = f*f*(3.0-2.0*f);
    float n = p.x + p.y*57.0 + 113.0*p.z;

    return lerp(lerp(lerp( hash(n+0.0), hash(n+1.0),f.x),
            lerp( hash(n+57.0), hash(n+58.0),f.x),f.y),
            lerp(lerp( hash(n+113.0), hash(n+114.0),f.x),
            lerp( hash(n+170.0), hash(n+171.0),f.x),f.y),f.z);
}

// returns a point light contribution with no shadow map sampling
float GetPointLightContribution(float4 worldPos, float4 LightPos, float4 normal){
	if (LightPos.w == 0) return 0;

	float3 LightDir = LightPos.xyz - worldPos.xyz;
	float Distance = length(LightDir) / LightPos.w; // normalize distance over light range
	float4 light = float4(LightDir, Distance);

	// radius based attenuation based on https://lisyarus.github.io/blog/graphics/2022/07/30/point-light-attenuation.html
	float s = Distance * Distance; 
	float atten = saturate((1 - s) / (1 + s));

	LightDir = normalize(LightDir); // normalize
	float diffuse = dot(LightDir, normal.xyz);

	float3 eyeDir = normalize(TESR_CameraPosition.xyz - worldPos.xyz);
	float3 Halfway = normalize(LightDir + eyeDir);
	float spec = pows(shade(Halfway, normal.xyz), 20);

	return saturate((diffuse + spec) * atten);
}


// Compute the areas of the screen that must be covered with snow
float4 SnowCoverage( VSOUT IN ) : COLOR0
{
	// compute at quarter scale
    float2 uv = IN.UVCoord * 4;
	if (uv.x > TESR_RenderScale.x || uv.y > TESR_RenderScale.y) return white;	// UNOFFICIAL 2026-10-27 RenderScale v2: a quarter of the drawn part
	
    float depth;
    float4 world = reconstructWorldPosition(uv, depth);

	if (depth > TESR_OrthoData.x) return white; // early out for the sky pixels

	// sample an average ortho
    float ortho = GetOrtho(world);
    ortho += GetOrtho(world + float4(-1, 0, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(1, 0, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0, -1, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0, 1, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(-0.7, -0.7, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(-0.7, 0.7, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0.7, -0.7, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0.7, 0.7, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(-0.3, 0, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0.3, 0, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0, -0.3, 0, 0) * orthoRadius);
    ortho += GetOrtho(world + float4(0, 0.3, 0, 0) * orthoRadius);
	ortho /= 13;

	ortho = smoothstep(0.1, 0.9, ortho); // reduce glitches by removing outlier values 
    ortho = lerp(ortho, 1, smoothstep(0.6 * TESR_OrthoData.x, TESR_OrthoData.x, depth)); // fade out ortho with distance


	return float4(ortho.xxx, 1);
}


// UNOFFICIAL 2026-10-28 NormalSmoothing ([Shaders.SnowAccumulation.Main], TESR_SnowAccumulationColor.w: game units, 0 = off). User:
// "snow + tessellation ... we get these facets": the snow takes its slope (where it may lie) and its shading from the normal rebuilt
// from depth, one flat normal per triangle, so on displaced land the snow came and went triangle by triangle. Here the normal is the
// average over a disc of that radius on the surface (12 points round the pixel, projected at its depth, each weighed by its distance
// in the world, so a wall or a rock a few units away hardly counts): snow lies on the ground's overall shape, as real snow does.
static const float2 SnowSmoothTaps[12] = {
	float2(0.35f, 0.0f), float2(-0.35f, 0.0f), float2(0.0f, 0.35f), float2(0.0f, -0.35f),
	float2(0.5f, 0.5f), float2(-0.5f, 0.5f), float2(0.5f, -0.5f), float2(-0.5f, -0.5f),
	float2(1.0f, 0.0f), float2(-1.0f, 0.0f), float2(0.0f, 1.0f), float2(0.0f, -1.0f) };

// UNOFFICIAL 2026-10-28 footprints (effects/CoverMap.h MaxPrints; user: "snow foot steps just kinda leaves a line behind when we walk
// like we're riding a bicycle"): the player's last prints, one per step, left and right in turn. The cover map keeps each print as a
// dot (8 units a texel), so near these prints this draws them instead, per pixel, as boot prints: the ball and toes and the heel, two
// ellipses along the step's heading, the big-toe side towards the path. -1: no print near (the map's trodden snow applies).
float4 TESR_CoverMapFeet[24];	// xyz: centre (relative to the camera), w: side (+ left foot) x (heading in whole degrees + 0.99 x depth); 0: none
float4 TESR_CoverMapFeetData;	// xy, z: a circle round all of them (relative to the camera; z <= 0: none), w: a print's half length

float SnowFootprint(float3 rel)
{
	// UNOFFICIAL 2026-10-28 (macintroll: "footsteps are a nice addition but shape is weird, should be a single blob"): one soft oval per
	// step by default; a negative half length (PersistentFootprintShape 1) = the boot shape below (sole and heel).
	float half = abs(TESR_CoverMapFeetData.w);
	bool boot = TESR_CoverMapFeetData.w < 0.0f;
	float2 fromAll = rel.xy - TESR_CoverMapFeetData.xy;
	[branch] if (TESR_CoverMapFeetData.z <= 0.0f || dot(fromAll, fromAll) > TESR_CoverMapFeetData.z * TESR_CoverMapFeetData.z) return -1.0f;
	float best = 0.0f;
	float nearAny = 0.0f;
	[loop] for (int i = 0; i < 24; i++) {
		float4 foot = TESR_CoverMapFeet[i];
		float2 d = rel.xy - foot.xy;
		[branch] if (foot.w != 0.0f && dot(d, d) < 4.0f * half * half && abs(rel.z - foot.z) < 40.0f) {
			float packed = abs(foot.w);
			float heading = floor(packed) * (3.14159265f / 180.0f);
			float depth = frac(packed) / 0.99f;
			float2 ahead;
			sincos(heading, ahead.y, ahead.x);
			// along the heading, and sideways away from the path (mirrored for the right foot)
			float2 q = float2(dot(d, ahead), dot(d, float2(-ahead.y, ahead.x)) * sign(foot.w));
			float shape;
			[branch] if (boot) {
				float toes = length((q - float2(0.26f * half, -0.05f * half)) / float2(0.66f * half, 0.40f * half));
				float heel = length((q - float2(-0.58f * half, 0.0f)) / float2(0.36f * half, 0.31f * half));
				shape = 1.0f - smoothstep(0.8f, 1.05f, min(toes, heel));
			}
			else shape = 1.0f - smoothstep(0.55f, 1.05f, length(q / float2(0.95f * half, 0.45f * half)));	// one soft oval along the step
			best = max(best, depth * shape);
			nearAny = 1.0f;
		}
	}
	return nearAny > 0.0f ? best : -1.0f;
}

float3 SnowSmoothNormal(float2 uv, float3 center, float3 norm, float depth)
{
	float radius = TESR_SnowAccumulationColor.w;
	[branch] if (radius <= 0.0f || depth >= farZ * 0.99f) return norm;
	float2 perUnit = 0.5f * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) / max(depth, 1.0f);	// screen share per game unit
	float2 s01 = rectToScreen(uv);
	float3 sum = norm;
	// UNOFFICIAL 2026-10-28 (macintroll: white sheets over cars and rocks, streaks across the ground and ferns): a tap counts only where
	// its own normal is near this pixel's (the land's facets differ by a few tens of degrees; a car's side and the ground under it, or a
	// fern and the ground, by far more: their normals no longer tilt each other), and only on screen (a tap clamped to the edge read the
	// same border column for a whole band of pixels: streaks along the rows).
	[unroll] for (int i = 0; i < 12; i++) {
		float2 raw = s01 + SnowSmoothTaps[i] * radius * perUnit;
		float2 tapUV = screenToRect(saturate(raw));
		float onScreen = all(raw == saturate(raw)) ? 1.0f : 0.0f;
		float tapDepth = readDepthLod(tapUV);
		float3 tapPos = toWorld(tapUV) * tapDepth;
		float3 tapNormal = GetWorldNormalLod(tapUV);
		float weight = saturate(1.0f - length(tapPos - center) / (2.0f * radius)) * (tapDepth < farZ * 0.99f ? 1.0f : 0.0f) * onScreen *
			smoothstep(0.5f, 0.85f, dot(tapNormal, norm));
		sum += tapNormal * weight;
	}
	return normalize(sum);
}

// UNOFFICIAL 2026-10-27 persist (technique 1, [Shaders.SnowAccumulation.Main] Persistent): the snow's amount comes from the cover map
// per spot (built up where the snow reached, melted by the sun, drifts) instead of the one amount for the whole world, and trodden
// snow (footprints) is drawn compacted; a spot with no snow left gets none (the noise pattern alone would leave flecks). With
// persist = false every line is as before.
float4 Snow( VSOUT IN, uniform bool persist ) : COLOR0
{
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	float3 world = toWorld(IN.UVCoord);
	
	float depth = readDepth(IN.UVCoord);
	float3 camera_vector = world * depth;
	float3 eyeDirection = -1 * normalize(world);
	float4 worldPos = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);

	float ortho = tex2D(TESR_RenderedBuffer, IN.UVCoord).x;
	float3 norm = GetWorldNormal(IN.UVCoord);

	// early out for the character gun, water surfaces/areas and surfaces above the ortho map (such as actors)
	float waterTreshold = (depth/farZ) * 200;
	float isWaterSurface = (dot(norm, float3(0, 0, 1)) > 0.9) && (worldPos.z > TESR_WaterSettings.x - waterTreshold) && (worldPos.z < TESR_WaterSettings.x + waterTreshold);
	if (isWaterSurface || !(ortho > 0)) return color;
	norm = SnowSmoothNormal(IN.UVCoord, camera_vector, norm, depth);	// UNOFFICIAL 2026-10-28 NormalSmoothing (after the water test)

    color = linearize(color);
	float3 sunColor = linearize(TESR_SunColor).rgb;

	float2 uv = worldPos.xy / 200.0f;
	// UNOFFICIAL 2026-10-27 persist: this spot's snow (z) and trodden snow (w) from the cover map. Trodden (footprint) snow stays snow,
	// compacted: flatter (less of the snow's own bumps, no sparkle, less rim light), greyer and darker; only in the middle of a
	// hard-trodden trail does the ground show through.
	float4 cover = float4(0, 0, 0, 0);
	float trodden = 0.0f;
	if (persist)
	{
		float coverInside;
		cover = CoverMapReadAll(camera_vector.xy, coverInside);
		trodden = saturate(CoverMapTrodden(cover.w));
		// UNOFFICIAL 2026-10-28 footprints: near the last prints, the boot prints themselves instead of the map's dots (no snow: none)
		float print = SnowFootprint(camera_vector);
		if (print >= 0.0f) trodden = print * TESR_CoverMapTrail.y * saturate(cover.z * 20.0f) * TESR_CoverMapGlobal.w;
	}
	float3 localNorm = expand(tex2D(TESR_SnowNormSampler, uv).xyz);
	if (persist) localNorm.xy *= 1.0f - 0.75f * trodden;
	float3 surfaceNormal = normalize(float3(localNorm.xy + norm.xy, localNorm.z * norm.z));
	float4 normal =  float4(surfaceNormal, 1);

	float3 snow_tex = TESR_SnowAccumulationColor.rgb;
	if (persist) snow_tex = lerp(snow_tex, luma(snow_tex) * float3(0.56f, 0.58f, 0.62f), 0.8f * trodden);

	float fresnelCoeff = saturate(pow(1 - shade(eyeDirection, surfaceNormal), 5));
	float3 snowSpec = pows(shades(normalize(TESR_SunDirection.xyz + eyeDirection), surfaceNormal), 20) * fresnelCoeff;

	float3 ambient = snow_tex * pows(TESR_SunAmbient.rgb,2.2); // linearise
	float2 shadow = tex2D(TESR_PointShadowBuffer, IN.UVCoord).rg;
	shadow.r = lerp(1.0f, shadow.r, useShadows); // disable shadow sampling if shadows are disabled in game
	shadow.r = lerp(shadow.r, 1.0f, TESR_ShadowFade.x);	// fade shadows to light when sun is low

	float3 diffuse = snow_tex * shade(TESR_SunDirection, normal) * sunColor * diffusePower * shadow.r;
	float3 spec = snowSpec * sunColor * specularPower * shadow.r;
	float3 fresnel = fresnelCoeff * sunColor * fresnelPower;
	float3 sparkles = pows(shades(eyeDirection, normalize(expand(tex2D(TESR_BlueNoiseSampler, worldPos.xy / 200).rgb))), 1000) * 0.2 * shadow.r;
	if (persist)
	{
		fresnel *= 1.0f - 0.6f * trodden;
		sparkles *= 1.0f - trodden;
	}

	// float4 snowColor = float4(ambient + diffuse + spec + fresnel, 1);
	float4 snowColor = float4(ambient + diffuse + spec + fresnel + sparkles, 1);
	float pointLightsPower = 0.5;

	shadow.g = 1; // disable point light shadows for debug

	for (int i = 0; i<12; i++){
		snowColor.rgb += GetPointLightContribution(worldPos, TESR_ShadowLightPosition[i], normal) * linearize(float4(TESR_LightColor[i].rgb * TESR_LightColor[i].a, 1)) * pointLightsPower * shadow.g;
		snowColor.rgb += GetPointLightContribution(worldPos, TESR_LightPosition[i], normal) * linearize(float4(TESR_LightColor[12 + i].rgb * TESR_LightColor[12 + i].a, 1)) * pointLightsPower * shadow.g;
	}

	// create a noisy pattern of accumulation over time
	float snowAmount = persist ? cover.z : TESR_SnowAccumulationParams.w;
	float coverage = saturate(pows(noise(worldPos.xyz / (noiseSize * 0.5)) * noise(worldPos.xyz / (noiseSize * 3)) * noise(worldPos.xyz / noiseSize), 2 - 2 * snowAmount));
	coverage = saturate(smoothstep(0.2, 0.4, coverage));
	if (persist) coverage *= smoothstep(0.0, 0.05, snowAmount) * (1.0f - smoothstep(0.5f, 0.9f, trodden * (0.7f + 0.6f * noise(worldPos.xyz / 18.0f))));
	float vertical = smoothstep(0.5, 0.8, shade(float3(0, 0, 1), norm));

	color = lerp(color, snowColor, coverage * min(vertical, ortho));
	color.a = 1;

    color.rgb = delinearize(color);
	return color;
}


technique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SnowCoverage();
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Scale(TESR_RenderedBuffer, 4);
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskH, TESR_SnowAccumulationParams.y, 3500, TESR_OrthoData.x);
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskV, TESR_SnowAccumulationParams.y, 3500, TESR_OrthoData.x);
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Snow(false);
	}
}

// UNOFFICIAL 2026-10-27 Persistent (CoverMap.h): technique 1, the same passes with the snow amount per spot from the cover map.
technique PersistentTechnique
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 SnowCoverage();
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Scale(TESR_RenderedBuffer, 4);
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskH, TESR_SnowAccumulationParams.y, 3500, TESR_OrthoData.x);
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskV, TESR_SnowAccumulationParams.y, 3500, TESR_OrthoData.x);
	}
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Snow(true);
	}
}
