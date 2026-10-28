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

float4 TESR_SnowAccumulationExtra;
float4 TESR_GameTime;
float4 TESR_SnowData;

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
sampler2D TESR_DepthBufferViewModel : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_CoverMapBuffer : register(s9) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_SnowNoiseSampler : register(s10) < string ResourceName = "Effects\Snownoise256.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_CapturedNormalsBuffer : register(s11) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
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
#include "Includes/CapturedNormals.hlsl"

float3 SnowRay(float2 uv) { return toWorld(uv + TESR_SnowAccumulationExtra.zw * TESR_RenderScale.xy); }
float4 SnowWorldPosition(float2 uv, out float viewDepth) {
	float2 s01 = rectToScreen(uv) + TESR_SnowAccumulationExtra.zw;
	float4 clipSpace = float4(s01.x * 2 - 1, (1 - s01.y) * 2 - 1, projectedDepthFromLinear(tex2D(TESR_DepthBuffer, uv).x), 1.0f);
	float4 viewSpace = mul(clipSpace, TESR_InvProjectionTransform);
	viewSpace /= viewSpace.w;
	viewDepth = viewSpace.z;
	return float4(mul(viewSpace, TESR_InvViewTransform).xyz, 1.0f);
}

bool SnowCapturedNormal(float2 uv, float distance, float3 rebuilt, out float3 n)
{
	n = float3(0.0f, 0.0f, 1.0f);
	[branch] if (TESR_SnowAccumulationExtra.x <= 0.0f) return false;
	return CapturedNormalAgrees(uv, distance, rebuilt, n);
}

float4 SnowCapturedView(float2 uv, float distance)
{
	float3 rebuilt = GetWorldNormal(uv);
	float3 n;
	bool captured = CapturedNormal(uv, distance, n);
	if (TESR_SnowAccumulationExtra.y > 1.5f) return captured && dot(n, rebuilt) <= TESR_CapturedNormalsData.w ? float4(1.0f, 0.0f, 0.0f, 1.0f) : float4((captured ? saturate(1.0f - dot(n, rebuilt)) : 0.0f).xxx, 1.0f);
	captured = captured && dot(n, rebuilt) > TESR_CapturedNormalsData.w;
	return float4(captured ? n * 0.5f + 0.5f : (rebuilt * 0.5f + 0.5f) * 0.3f, 1.0f);
}


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
	if (uv.x > TESR_RenderScale.x || uv.y > TESR_RenderScale.y) return white;
	
    float depth;
    float4 world = SnowWorldPosition(uv, depth);

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


static const float2 SnowSmoothTaps[12] = {
	float2(0.35f, 0.0f), float2(-0.35f, 0.0f), float2(0.0f, 0.35f), float2(0.0f, -0.35f),
	float2(0.5f, 0.5f), float2(-0.5f, 0.5f), float2(0.5f, -0.5f), float2(-0.5f, -0.5f),
	float2(1.0f, 0.0f), float2(-1.0f, 0.0f), float2(0.0f, 1.0f), float2(0.0f, -1.0f) };

float4 TESR_CoverMapFeet[24];
float4 TESR_CoverMapFeetData;

float SnowFootprint(float3 rel)
{
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
			float2 q = float2(dot(d, ahead), dot(d, float2(-ahead.y, ahead.x)) * sign(foot.w));
			float shape;
			[branch] if (boot) {
				float toes = length((q - float2(0.26f * half, -0.05f * half)) / float2(0.66f * half, 0.40f * half));
				float heel = length((q - float2(-0.58f * half, 0.0f)) / float2(0.36f * half, 0.31f * half));
				shape = 1.0f - smoothstep(0.8f, 1.05f, min(toes, heel));
			}
			else shape = 1.0f - smoothstep(0.55f, 1.05f, length(q / float2(0.95f * half, 0.45f * half)));
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
	float2 perUnit = 0.5f * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) / max(depth, 1.0f);
	float2 s01 = rectToScreen(uv);
	float3 sum = norm;
	[unroll] for (int i = 0; i < 12; i++) {
		float2 raw = s01 + SnowSmoothTaps[i] * radius * perUnit;
		float2 tapUV = screenToRect(saturate(raw));
		float onScreen = all(raw == saturate(raw)) ? 1.0f : 0.0f;
		float tapDepth = readDepthLod(tapUV);
		float3 tapPos = SnowRay(tapUV) * tapDepth;
		float3 tapNormal = GetWorldNormalLod(tapUV);
		float weight = saturate(1.0f - length(tapPos - center) / (2.0f * radius)) * (tapDepth < farZ * 0.99f ? 1.0f : 0.0f) * onScreen *
			smoothstep(0.5f, 0.85f, dot(tapNormal, norm));
		sum += tapNormal * weight;
	}
	return normalize(sum);
}

float4 Snow( VSOUT IN, uniform bool persist ) : COLOR0
{
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	float3 world = SnowRay(IN.UVCoord);
	
	float depth = readDepth(IN.UVCoord);
	float3 camera_vector = world * depth;
	float3 eyeDirection = -1 * normalize(world);
	float4 worldPos = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);

	[branch] if (TESR_SnowAccumulationExtra.y > 0.5f) return SnowCapturedView(IN.UVCoord, length(camera_vector));
	[branch] if (CapturedActor(IN.UVCoord, length(camera_vector))) return color;

	float ortho = tex2D(TESR_RenderedBuffer, IN.UVCoord).x;
	float3 norm = GetWorldNormal(IN.UVCoord);
	float3 capturedNorm;
	bool captured = SnowCapturedNormal(IN.UVCoord, length(camera_vector), norm, capturedNorm);
	if (captured) norm = capturedNorm;

	// early out for the character gun, water surfaces/areas and surfaces above the ortho map (such as actors)
	float waterTreshold = (depth/farZ) * 200;
	float isWaterSurface = (dot(norm, float3(0, 0, 1)) > 0.9) && (worldPos.z > TESR_WaterSettings.x - waterTreshold) && (worldPos.z < TESR_WaterSettings.x + waterTreshold);
	if (isWaterSurface || !(ortho > 0)) return color;
	[branch] if (!captured) norm = SnowSmoothNormal(IN.UVCoord, camera_vector, norm, depth);

    color = linearize(color);
	float3 sunColor = linearize(TESR_SunColor).rgb;

	float2 uv = worldPos.xy / 200.0f;
	float4 cover = float4(0, 0, 0, 0);
	float trodden = 0.0f;
	if (persist)
	{
		float coverInside;
		cover = CoverMapReadAll(camera_vector.xy, coverInside);
		trodden = saturate(CoverMapTrodden(cover.w));
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


static const float SA5_diffusePower    = 0.56f;
static const float SA5_specularPower   = 1.0f;
static const float SA5_fresnelPower    = 0.65f;
static const float INV_NOISE_SIZE       = 1.0f / 200.0f;
static const float INV_NOISE_SIZE_HALF  = 1.0f / 100.0f;
static const float INV_NOISE_SIZE_X3    = 1.0f / 600.0f;
static const float INV_200   = 1.0f / 200.0f;
static const float INV_64    = 1.0f / 64.0f;
static const float INV_96    = 1.0f / 96.0f;
static const float INV_512   = 1.0f / 512.0f;
static const float INV_2048  = 1.0f / 2048.0f;
static const float ACCUM_SNOW_READY_START   = 0.94f;
static const float ACCUM_SNOW_READY_FULL    = 0.995f;
static const float ACCUM_DELAY_START        = 0.25f;
static const float ACCUM_DELAY_FULL         = 0.82f;
static const float ACCUM_PERSIST_START      = 0.72f;
static const float ACCUM_PERSIST_FULL       = 0.88f;

static const int SA_Debug = 0;
static const float SA_SnowRoundness = 1.0f;
static const float SA_SnowBroadDetail = 0.12f;
static const float SA_SnowBroadScale = 700.0f;
static const float SA_SnowSlopeSoftness = 0.26f;
static const float SA_SnowSlopeNoise = 0.12f;
static const float SA_SnowSlopeLift = 0.35f;
static const float SA_FootInnerShadow = 0.32f;
static const float SA_FootOuterGlow = 0.12f;
static const float SA_FootInnerWidth = 0.82f;
static const float SA_FootOuterWidth = 0.88f;
static const float SA_FootprintRemoval = 0.90f;
#define SA_SMOOTH_NORMALS 1
#define SA_SMOOTH_COVERAGE 1
static const float SA_NormalRadiusWorld = 48.0f;
static const float SA_NormalSmoothStrength = 1.0f;

float3 SA_SmoothSnowNormal(float2 uv, float centerDepth, float3 original) {
#if SA_SMOOTH_NORMALS
    float radius = SA_NormalRadiusWorld;
    if (radius <= 0.0f || centerDepth >= farZ * 0.99f) return original;
    float3 center = normalize(original);
    float3 centerPos = SnowRay(uv) * centerDepth;
    float3 total = center * 2.0f;
    static const float2 offsets[8] = {
        float2(0.5f,0), float2(-0.5f,0), float2(0,0.5f), float2(0,-0.5f),
        float2(0.7071f,0.7071f), float2(-0.7071f,0.7071f),
        float2(0.7071f,-0.7071f), float2(-0.7071f,-0.7071f)
    };
    float2 centerScreen = rectToScreen(uv);
    float2 radiusScreen = 0.5f * abs(float2(TESR_ProjectionTransform[0][0],
                                           TESR_ProjectionTransform[1][1]))
                         * radius / max(centerDepth, 1.0f);
    [unroll] for (int i = 0; i < 8; ++i) {
        float2 tapScreen = centerScreen + offsets[i] * radiusScreen;
        float inScreen = step(0.0f, tapScreen.x) * step(tapScreen.x, 1.0f)
                       * step(0.0f, tapScreen.y) * step(tapScreen.y, 1.0f);
        float2 sampleUV = screenToRect(saturate(tapScreen));
        float sampleDepth = readDepthLod(sampleUV);
        float3 sampled = normalize(GetWorldNormalLod(sampleUV));
        float3 samplePos = SnowRay(sampleUV) * sampleDepth;
        float separation = length(samplePos - centerPos);
        float weight = 1.0f - smoothstep(0.5f * radius, 2.0f * radius, separation);
        weight *= smoothstep(0.25f, 0.70f, dot(center, sampled));
        weight *= inScreen * (sampleDepth < farZ * 0.99f ? 1.0f : 0.0f);
        total += sampled * weight;
    }
    return normalize(lerp(center, normalize(total), saturate(SA_NormalSmoothStrength)));
#else
    return original;
#endif
}



float SA_Hash21(float2 p) {
    p = frac(p * float2(123.34f, 345.45f));
    p += dot(p, p + 34.345f);
    return frac(p.x * p.y);
}

float2 SA_Rotate2D(float2 p) {
    return float2(0.8192f * p.x - 0.5736f * p.y,
                  0.5736f * p.x + 0.8192f * p.y);
}

float SA_ValueNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = SA_Hash21(i);
    float b = SA_Hash21(i + float2(1, 0));
    float c = SA_Hash21(i + float2(0, 1));
    float d = SA_Hash21(i + float2(1, 1));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float SA5_hash(float n) {
    return frac(sin(n) * 43758.5453f);
}

float SA5_noise(float3 x) {
    x.xy = float2(0.8f*x.x - 0.6f*x.y, 0.6f*x.x + 0.8f*x.y);
    x.xz = float2(0.8f*x.x - 0.6f*x.z, 0.6f*x.x + 0.8f*x.z);
    float3 p = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    float n = p.x + p.y * 57.0f + 113.0f * p.z;
    return lerp(
        lerp(lerp(SA5_hash(n       ), SA5_hash(n +  1.0f), f.x),
             lerp(SA5_hash(n + 57.0f), SA5_hash(n + 58.0f), f.x), f.y),
        lerp(lerp(SA5_hash(n +113.0f), SA5_hash(n +114.0f), f.x),
             lerp(SA5_hash(n +170.0f), SA5_hash(n +171.0f), f.x), f.y), f.z);
}


float SA_ImpactCell(float2 uv, float time)
{
    float2 gv = frac(uv) - 0.5f;
    float2 id = floor(uv);

    float rnd1 = SA_Hash21(id);
    float rnd2 = SA_Hash21(id + 17.23f);
    float rnd3 = SA_Hash21(id + 43.71f);

    float2 center = float2(rnd1, rnd2) - 0.5f;
    float  cycle  = frac(time + rnd3);
    float  life   = saturate(cycle / 0.35f);
    float  alive  = 1.0f - step(0.35f, cycle);

    float fadeIn  = smoothstep(0.0f, 0.05f, life);
    float fadeOut = 1.0f - smoothstep(0.22f, 0.60f, life);
    float fade    = fadeIn * fadeOut * alive;

    float2 q = gv - center;
    q.y -= (0.01f + life * (0.08f + rnd1 * 0.04f));

    float d      = length(q);
    float radius = lerp(0.025f, 0.14f, life);
    float angle  = atan2(q.y, q.x);

    float curve    = sin(angle * 2.0f + id.y * 1.3f) * 0.35f;
    float spikes   = abs(sin((angle + curve) * 3.0f + id.x * 1.7f));
    float spikeMask = lerp(1.0f, spikes, 0.45f);

    float grain    = SA_Hash21(floor(q * 6.0f + id * 0.3f));
    float edgeWarp = (grain - 0.5f) * radius * 0.35f;
    float warpedRadius = radius * spikeMask + edgeWarp;

    float core   = 1.0f - smoothstep(radius * 0.07f, radius * 0.18f, d);
    float splash = 1.0f - smoothstep(warpedRadius * 0.6f, warpedRadius, d);
    float shape  = splash * 0.7f + core * 0.3f;

    float core2 = core * core;
    shape += core2 * core2 * 0.6f;

    return shape * fade * step(0.001f, fade);
}


float SA5_GetOrtho(float4 worldPos) {
    const float thickness = 0.0025f;
    const float feather   = 0.02f;
    const float baseSoft  = 0.0055f;

    float4 ortho_pos = mul(worldPos, TESR_ShadowCameraToLightTransformOrtho);
    ortho_pos.xyz /= ortho_pos.w;
    ortho_pos.xy = ortho_pos.xy * float2(0.5f, -0.5f) + 0.5f.xx;

    float2 uv = ortho_pos.xy;
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
        return 0.0f;

    float2 d       = min(uv, 1.0f - uv);
    float edgeMask = smoothstep(0.0f, feather, min(d.x, d.y));

    float orthoDepth = tex2D(TESR_OrthoMapBuffer, uv).r;
    float delta      = abs(ortho_pos.z - orthoDepth);

    float soft = baseSoft + 0.001f;
    float band = 1.0f - smoothstep(thickness, thickness + soft, delta);
    return band * edgeMask;
}

float GetOrthoAverage(float4 worldPos, float orthoRadius) {
    const float2 offsets[9] = {
        float2( 0.0f,  0.0f),
        float2(-1.0f,  0.0f), float2(1.0f,  0.0f),
        float2( 0.0f, -1.0f), float2(0.0f,  1.0f),
        float2(-0.7f, -0.7f), float2(-0.7f, 0.7f),
        float2( 0.7f, -0.7f), float2( 0.7f, 0.7f)
    };

    float ortho = 0.0f;
    for (int i = 0; i < 9; i++) {
        ortho += SA5_GetOrtho(worldPos + float4(offsets[i] * orthoRadius, 0.0f, 0.0f));
    }
    return ortho * (1.0f / 9.0f);
}

float SA5_PointLight(float4 worldPos, float4 LightPos, float3 normal, float3 eyeDir) {
    float radius = LightPos.w;
    if (radius <= 0.0f) return 0.0f;

    float3 L      = LightPos.xyz - worldPos.xyz;
    float distSq  = dot(L, L);
    float radiusSq = radius * radius;
    if (distSq >= radiusSq) return 0.0f;

    float invLen  = rsqrt(max(distSq, 1e-6f));
    float distNorm = (distSq * invLen) / radius;
    float s       = distNorm * distNorm;
    float atten   = saturate((1.0f - s) / (1.0f + s));
    if (atten <= 0.001f) return 0.0f;

    float3 lightDir = L * invLen;
    float  diffuse  = saturate(dot(lightDir, normal));

    float3 halfway = normalize(lightDir + eyeDir);
    float  spec    = pow(saturate(dot(halfway, normal)), 20.0f);

    return saturate((diffuse + spec) * atten);
}


float4 SA5_SnowCoverage(VSOUT IN) : COLOR0 {
    float2 uv = IN.UVCoord * 4.0f;
    if (uv.x > TESR_RenderScale.x || uv.y > TESR_RenderScale.y) return 1.0f.xxxx;

    float  depth;
    float4 world = SnowWorldPosition(uv, depth);
    if (depth > TESR_OrthoData.x) return 1.0f.xxxx;
    if (depth >= 3500.0f)         return 1.0f.xxxx;

    float ortho = GetOrthoAverage(world, TESR_SnowAccumulationParams.x);
    ortho = smoothstep(0.1f, 0.9f, ortho);
    ortho = lerp(ortho, 1.0f, smoothstep(0.6f * TESR_OrthoData.x, TESR_OrthoData.x, depth));

    float orthoFade = 1.0f - smoothstep(2500.0f, 3500.0f, depth);
    ortho = lerp(1.0f, ortho, orthoFade);

    return float4(ortho, ortho, ortho, 1.0f);
}



float SA_SmoothMin(float a, float b, float k) {
    float h = saturate(0.5f + 0.5f * (b - a) / k);
    return lerp(b, a, h) - k * h * (1.0f - h);
}

float SA5_SnowFootprint(float3 rel, out float2 relief)
{
    relief = float2(0, 0);
    float occupied = 0.0f;
	float footHalfSize = TESR_CoverMapFeetData.w;
	float2 fromAll = rel.xy - TESR_CoverMapFeetData.xy;
	[branch] if (TESR_CoverMapFeetData.z <= 0.0f || dot(fromAll, fromAll) > TESR_CoverMapFeetData.z * TESR_CoverMapFeetData.z) return -1.0f;
	float best = 0.0f;
	float nearAny = 0.0f;
	[loop] for (int i = 0; i < 24; i++) {
		float4 foot = TESR_CoverMapFeet[i];
		float2 d = rel.xy - foot.xy;
		[branch] if (foot.w != 0.0f && dot(d, d) < 4.0f * footHalfSize * footHalfSize && abs(rel.z - foot.z) < 40.0f) {
			float packed = abs(foot.w);
			float heading = floor(packed) * (3.14159265f / 180.0f);
			float depth = frac(packed) / 0.99f;
			float2 ahead;
			sincos(heading, ahead.y, ahead.x);
			float2 q = float2(dot(d, ahead), dot(d, float2(-ahead.y, ahead.x)) * sign(foot.w));
            float2 sole = q / max(footHalfSize, 0.001f);
            float toes = length((sole - float2(0.29f, -0.025f)) / float2(0.64f, 0.39f)) - 1.0f;
            float heel = length((sole - float2(-0.59f, 0.0f)) / float2(0.34f, 0.30f)) - 1.0f;
            float arch = length((sole - float2(-0.16f, 0.045f)) / float2(0.47f, 0.255f)) - 1.0f;
            float soleDistance = SA_SmoothMin(SA_SmoothMin(toes, arch, 0.20f), heel, 0.20f);
            best = max(best, depth * (1.0f - smoothstep(-0.12f, 0.10f, soleDistance)));
            float innerWidth = max(SA_FootInnerWidth, 0.001f);
            float outerWidth = max(SA_FootOuterWidth, 0.001f);
            float innerBand = smoothstep(-innerWidth, -0.25f * innerWidth, soleDistance)
                            * (1.0f - smoothstep(-0.25f * innerWidth, 0.0f, soleDistance));
            float outerBand = smoothstep(0.0f, 0.25f * outerWidth, soleDistance)
                            * (1.0f - smoothstep(0.25f * outerWidth, outerWidth, soleDistance));
            relief = max(relief, saturate(depth) * float2(innerBand, outerBand));
            occupied = max(occupied, saturate(depth) * (1.0f - smoothstep(-0.08f, 0.0f, soleDistance)));
			nearAny = 1.0f;
		}
	}
    relief.y *= 1.0f - occupied;
	return nearAny > 0.0f ? best : -1.0f;
}

float4 SA5_Snow(VSOUT IN, uniform bool persist) : COLOR0 {
    float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);

    float3 world = SnowRay(IN.UVCoord);
    float  depth = readDepth(IN.UVCoord);

    float vmDepthX = tex2D(TESR_DepthBufferViewModel, IN.UVCoord).x;
    float vmMask   = 0.0f;

    if ((TESR_DepthConstants.z == 0 && vmDepthX < 0.9f) ||
        (TESR_DepthConstants.z > 0  && vmDepthX > 0.01f))
        vmMask = 1.0f;

    if (vmMask > 0.5f && depth < 120.0f) {
        float3 norm  = GetWorldNormal(IN.UVCoord);
        float  up    = saturate(norm.z);
        float  topZone = smoothstep(0.72f, 0.94f, up);

        float2 uv = IN.UVCoord * float2(10.0f, 6.0f);
        float  t  = TESR_GameTime.x * 0.020f;

        float hit1 = SA_ImpactCell(uv, t);
        float hit2 = SA_ImpactCell(uv * 1.08f + 3.17f, t * 0.93f);

        float impact = saturate(hit1 + hit2 * 0.6f) * topZone * 0.45f;

        float impactAllow = smoothstep(0.0f, 0.15f, TESR_SnowData.x);
        impact *= impactAllow;

        color.rgb = lerp(color.rgb, float3(0.92f, 0.94f, 0.96f), impact);
        return color;
    }

    float3 camera_vector = world * depth;
    [branch] if (TESR_SnowAccumulationExtra.y > 0.5f) return SnowCapturedView(IN.UVCoord, length(camera_vector));
    [branch] if (CapturedActor(IN.UVCoord, length(camera_vector))) return color;
    float4 cover = float4(0, 0, 0, 0);
    float trodden = 0.0f;
    float2 footprintRelief = float2(0, 0);
    if (persist) {
        float coverInside;
        cover = CoverMapReadAll(camera_vector.xy, coverInside);
        trodden = saturate(CoverMapTrodden(cover.w));
        float footprint = SA5_SnowFootprint(camera_vector, footprintRelief);
        footprintRelief *= saturate(TESR_CoverMapTrail.y * saturate(cover.z * 20.0f) * TESR_CoverMapGlobal.w);
        if (footprint >= 0.0f)
            trodden = footprint * TESR_CoverMapTrail.y * saturate(cover.z * 20.0f) * TESR_CoverMapGlobal.w;
    }

    if (SA_Debug == 1) {
        float amount = saturate(persist ? cover.z : TESR_SnowAccumulationParams.w);
        return float4(amount.xxx, 1);
    }
    if (SA_Debug == 2) {
        float n = SA5_noise((TESR_CameraPosition.xyz + camera_vector) * INV_NOISE_SIZE);
        return float4(n.xxx, 1);
    }
    if (SA_Debug == 3) return float4(normalize(GetWorldNormal(IN.UVCoord)) * 0.5f + 0.5f, 1);
    if (SA_Debug == 4) return float4(tex2D(TESR_RenderedBuffer, IN.UVCoord).xxx, 1);
    if (SA_Debug == 5) return float4(tex2D(TESR_SnowNoiseSampler,
        (TESR_CameraPosition.xy + camera_vector.xy) * INV_512).rgb, 1);

    float accumulationRaw = saturate(persist ? cover.z : TESR_SnowAccumulationParams.w);
    float snowfallReady = smoothstep(
        ACCUM_SNOW_READY_START,
        ACCUM_SNOW_READY_FULL,
        TESR_SnowData.x
    );
    float delayedAccumulation = smoothstep(
        ACCUM_DELAY_START,
        ACCUM_DELAY_FULL,
        accumulationRaw
    );
    float persistenceGate = smoothstep(
        ACCUM_PERSIST_START,
        ACCUM_PERSIST_FULL,
        accumulationRaw
    );
    float accumulationGate = max(delayedAccumulation * snowfallReady, persistenceGate);
    if (persist) accumulationGate = delayedAccumulation * smoothstep(0.0f, 0.05f, accumulationRaw);

    float detailFade  = 1.0f - smoothstep(1200.0f, 2200.0f, depth);
    float sparkleFade = 1.0f - smoothstep( 800.0f, 1600.0f, depth);
    float waterFade   = 1.0f - smoothstep(1000.0f, 1800.0f, depth);

    float3 eyeDirection  = -normalize(world);
    float4 worldPos      = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);

    float orthoRaw = tex2D(TESR_RenderedBuffer, IN.UVCoord).x;
    if (orthoRaw < 0.001f) return color;

    float3 originalNorm = GetWorldNormal(IN.UVCoord);
    float3 capturedNorm;
    bool captured = SnowCapturedNormal(IN.UVCoord, length(camera_vector), originalNorm, capturedNorm);
    if (captured) originalNorm = capturedNorm;
    float3 norm = originalNorm;
    [branch] if (!captured) norm = SA_SmoothSnowNormal(IN.UVCoord, depth, originalNorm);
#if SA_SMOOTH_COVERAGE
    float upNBase = norm.z;
#else
    float upNBase = originalNorm.z;
#endif
    float snowRoundness = saturate(SA_SnowRoundness);
    if (upNBase <= lerp(0.15f, -0.10f, snowRoundness)) return color;

    float waterThreshold = (depth / farZ) * 150.0f;
    float waterUpMask    = step(0.9f, dot(originalNorm, float3(0.0f, 0.0f, 1.0f)));
    float waterHeightMask = step(TESR_WaterSettings.x - waterThreshold, worldPos.z) *
                            step(worldPos.z, TESR_WaterSettings.x + waterThreshold);
    if (waterUpMask * waterHeightMask > 0.5f) return color;

    color = linearize(color);

    float2 worldXY_200  = worldPos.xy * INV_200;
    float2 bn64         = worldPos.xy * INV_64;
    float2 bn512        = worldPos.xy * INV_512;
    float2 bn2048       = worldPos.xy * INV_2048;

    float3 localNorm    = expand(tex2D(TESR_SnowNormSampler, worldXY_200).xyz);
    if (persist) localNorm.xy *= 1.0f - 0.75f * trodden;
    float3 lightingBase = norm;
    if (snowRoundness > 0.0f) {
        float2 broadUV = SA_Rotate2D(worldPos.xy) / max(SA_SnowBroadScale, 1.0f) + float2(0.37f, 0.61f);
        float3 broadDetail = expand(tex2D(TESR_SnowNormSampler, broadUV).xyz);
        float broadStrength = SA_SnowBroadDetail;
        if (persist) broadStrength *= 1.0f - 0.75f * trodden;
        float3 snowBase = normalize(float3(broadDetail.xy * broadStrength, 1.0f));
        lightingBase = normalize(lerp(normalize(norm), snowBase, snowRoundness));
    }
    float3 surfaceNormal = normalize(float3(localNorm.xy + lightingBase.xy, localNorm.z * lightingBase.z));

    float3 snowTex  = TESR_SnowAccumulationColor.rgb;
    if (persist) snowTex = lerp(snowTex, luma(snowTex) * float3(0.64f, 0.72f, 0.82f), 0.55f * trodden);
    float3 sunColor = linearize(TESR_SunColor).rgb;
    float3 ambient  = snowTex * (TESR_SunAmbient.rgb * TESR_SunAmbient.rgb);


    float ndv          = saturate(dot(eyeDirection, surfaceNormal));
    float fresnelCoeff = saturate(pow(1.0f - ndv, 5.0f));
    float3 sunHalf     = normalize(TESR_SunDirection.xyz + eyeDirection);
    float  snowSpec    = pow(saturate(dot(sunHalf, surfaceNormal)), 20.0f) * fresnelCoeff;

    float2 shadow = tex2D(TESR_PointShadowBuffer, IN.UVCoord).rg;
    shadow.r = lerp(shadow.r, 1.0f, TESR_ShadowFade.x);

    float sunPower = TESR_SnowAccumulationParams.z;
    float  NdotL   = saturate(dot(TESR_SunDirection.xyz, surfaceNormal));
    float3 diffuse = snowTex * NdotL * sunColor * SA5_diffusePower * shadow.r * sunPower;
    float3 spec    = snowSpec * sunColor * SA5_specularPower * shadow.r * sunPower;
    float3 fresnel = fresnelCoeff * sunColor * SA5_fresnelPower * sunPower;

    float3 sparkleNorm = normalize(expand(tex2D(TESR_SnowNoiseSampler, worldXY_200).rgb));
    float  sparkles    = pow(saturate(abs(dot(eyeDirection, sparkleNorm))), 1000.0f)
                       * 0.2f * shadow.r * sparkleFade;

    if (persist) {
        fresnel *= 1.0f - 0.6f * trodden;
        sparkles *= 1.0f - trodden;
    }
    float4 snowColor = float4(ambient + diffuse + spec + fresnel + sparkles, 1.0f);

    shadow.g = 1.0f;
    for (int i = 0; i < 12; i++) {
        if (TESR_ShadowLightPosition[i].w > 0.0f)
            snowColor.rgb += SA5_PointLight(worldPos, TESR_ShadowLightPosition[i], surfaceNormal, eyeDirection)
                           * linearize(float4(TESR_LightColor[i].rgb * TESR_LightColor[i].a, 1.0f)).rgb
                           * 0.5f * shadow.g;

        if (TESR_LightPosition[i].w > 0.0f)
            snowColor.rgb += SA5_PointLight(worldPos, TESR_LightPosition[i], surfaceNormal, eyeDirection)
                           * linearize(float4(TESR_LightColor[12 + i].rgb * TESR_LightColor[12 + i].a, 1.0f)).rgb
                           * 0.5f * shadow.g;
    }

    if (persist) {
        float innerShade = 1.0f - saturate(SA_FootInnerShadow) * footprintRelief.x;
        float outerLight = 1.0f + max(SA_FootOuterGlow, 0.0f) * footprintRelief.y;
        snowColor.rgb *= innerShade * outerLight;
    }

    float cov0 = SA5_noise(worldPos.xyz * INV_NOISE_SIZE_HALF);
    float cov1 = SA5_noise(worldPos.xyz * INV_NOISE_SIZE);
    float coverage = saturate(pow(abs(cov0 * cov1), 2.2f - 2.0f * delayedAccumulation));

    float2 p2    = SA_Rotate2D(worldPos.xz);
    float  cov   = smoothstep(0.05f, 0.6f, coverage);
    float breakup = SA_ValueNoise2D(p2 * 0.003f);
    cov          *= lerp(0.8f, 1.0f, breakup);
    float finalCoverage = cov;

    float upN = saturate(upNBase);
    float undersideGuard = lerp(1.0f, smoothstep(-0.10f, 0.25f, upNBase), snowRoundness);
    upN = lerp(upN, 1.0f, snowRoundness * saturate(SA_SnowSlopeLift));
    float slopeNoise = ((cov0 + cov1) - 1.0f) * SA_SnowSlopeNoise * snowRoundness;
    upN = saturate(upN + slopeNoise);
    float req = lerp(0.92f, 0.40f, delayedAccumulation);
    float slopeWidth = lerp(0.10f, max(SA_SnowSlopeSoftness, 0.10f), snowRoundness);
    float allow = smoothstep(req - (slopeWidth - 0.10f), req + 0.10f, upN) * undersideGuard;
    finalCoverage *= allow;


    float  noise64   = tex2D(TESR_SnowNoiseSampler, bn64).r;
    float  noise512  = tex2D(TESR_SnowNoiseSampler, bn512).r;
    float4 noise2048_rgba = tex2D(TESR_SnowNoiseSampler, bn2048);
    float  noise2048 = noise2048_rgba.r;

    const float PATCH_A = 0.50f;
    const float PATCH_B = 0.30f;
    const float PATCH_C = 0.20f;
    float patchNoise = noise2048_rgba.r * PATCH_A
                     + noise2048_rgba.g * PATCH_B
                     + noise2048_rgba.b * PATCH_C;

    float slope  = upN;
    const float MACRO_LARGE = 0.75f;
    const float MACRO_MID   = 0.50f;
    float macro  = patchNoise * MACRO_LARGE + noise512 * MACRO_MID;
    slope += (macro - 1.0f) * 0.3f;

    float vertical = smoothstep(lerp(0.35f, 0.20f, snowRoundness), 0.72f, slope);
    float edge     = 1.0f - abs(vertical * 2.0f - 1.0f);
    edge           = saturate(edge * edge);

    float d = noise64 * 0.65f + noise512 * 0.35f;
    vertical = saturate(vertical + (d - 0.5f) * 0.18f * edge * detailFade);

    float snowMask = finalCoverage * min(vertical, orthoRaw);

    snowMask *= accumulationGate;
    if (persist) {
        snowMask *= 1.0f - SA_FootprintRemoval * smoothstep(0.5f, 0.9f,
            trodden * (0.7f + 0.6f * SA5_noise(worldPos.xyz / 18.0f)));
    }

    float grainRaw = (noise64 * 0.70f + noise2048_rgba.a * 0.30f) - 0.5f;
    float grain    = sign(grainRaw) * sqrt(abs(grainRaw) * 2.0f) * 0.5f;
    grain         *= detailFade;

    float partial = smoothstep(0.01f, 0.1f, snowMask) * (1.0f - smoothstep(0.07f, 0.80f, snowMask));
    snowMask *= (1.0f + grain * partial);

    float shorelineOpacity = 1.0f;

    float waterLevel = TESR_WaterSettings.x;
    float dist       = abs(worldPos.z - waterLevel);

    const float WATER_EDGE_WIDTH = 5.0f;

    if (dist < WATER_EDGE_WIDTH) {
        float nearWater   = 1.0f - saturate(dist / WATER_EDGE_WIDTH);
        float solidRemove = smoothstep(0.75f, 1.0f, nearWater);
        float keepProb    = 1.0f - nearWater;
        float waterNoise  = lerp(0.5f, noise2048 * 0.60f + noise64 * 0.40f, waterFade);
        float keepMask    = (1.0f - smoothstep(keepProb - 0.001f, keepProb + 0.001f, waterNoise))
                          * (1.0f - solidRemove);
        snowMask *= keepMask;

        const float WATER_EDGE_MIN_OPACITY = 0.05f;
        const float WATER_EDGE_MAX_OPACITY = 0.15f;
        float noisyOpacity = lerp(WATER_EDGE_MIN_OPACITY, WATER_EDGE_MAX_OPACITY, waterNoise);
        shorelineOpacity  = lerp(1.0f, noisyOpacity, nearWater);
    }

    const float edgeBand = 0.12f;
    float nearEdge = 1.0f - saturate(snowMask / edgeBand);
    float zone     = smoothstep(0.05f, 0.90f, nearEdge);
    if (zone > 0.0f) {
        float solidRemove = smoothstep(0.70f, 0.98f, nearEdge);
        float keepProb    = 1.0f - nearEdge * (1.0f + 0.15f * (1.0f - nearEdge));
        float edgeNoise   = lerp(0.5f, noise2048 * 0.60f + noise64 * 0.40f, detailFade);
        float keepMask    = 1.0f - smoothstep(keepProb - 0.001f, keepProb + 0.001f, edgeNoise);
        keepMask         *= (1.0f - solidRemove);
        snowMask         *= lerp(1.0f, keepMask, zone);
    }

    snowMask *= shorelineOpacity;



    float shadowFactor = lerp(DARKNESS, 1.0f, shadow.r);
    float4 shadowedSnow = float4(lerp(color.rgb, snowColor.rgb * shadowFactor, snowMask), 1.0f);
    color.rgb = shadowedSnow.rgb;
    color.a  = 1.0f;
    color.rgb = delinearize(color.rgb);
    return color;
}

technique LookTechnique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 SA5_SnowCoverage();
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 Scale(TESR_RenderedBuffer, 4);
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskH, TESR_SnowAccumulationParams.y, 8500, TESR_OrthoData.x);
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskV, TESR_SnowAccumulationParams.y, 8500, TESR_OrthoData.x);
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 SA5_Snow(false);
    }
}

technique LookPersistentTechnique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 SA5_SnowCoverage();
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 Scale(TESR_RenderedBuffer, 4);
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskH, TESR_SnowAccumulationParams.y, 8500, TESR_OrthoData.x);
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 DepthBlur(TESR_RenderedBuffer, OffsetMaskV, TESR_SnowAccumulationParams.y, 8500, TESR_OrthoData.x);
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader  = compile ps_3_0 SA5_Snow(true);
    }
}
