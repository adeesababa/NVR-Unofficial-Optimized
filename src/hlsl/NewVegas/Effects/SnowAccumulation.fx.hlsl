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


// UNOFFICIAL 2026-10-16: SmoothFacets. The normals buffer is rebuilt from depth, so every triangle of a low-poly rock is
// perfectly flat, and the snow's "faces up" test cut such rocks into white and bare triangles (user report, snowy mod
// area). With SmoothFacets on, the coverage pass also averages the surface direction over a patch facetRadius game units
// across (samples more than twice that away in 3D are left out, so a rock does not take the direction of the ground
// behind it) and stores it in g/b; the two blur passes soften it together with the coverage, and Snow() places and lights
// the snow with it. 0: the per-pixel normal, as before.
static const float facetRadius = TESR_SnowAccumulationColor.w;

static const float2 FacetTaps[12] =
{
	float2( 0.5,    0.0), float2( 0.25,  0.433), float2(-0.25,  0.433),
	float2(-0.5,    0.0), float2(-0.25, -0.433), float2( 0.25, -0.433),
	float2( 0.866,  0.5), float2( 0.0,   1.0),   float2(-0.866,  0.5),
	float2(-0.866, -0.5), float2( 0.0,  -1.0),   float2( 0.866, -0.5),
};

float3 ViewNormalLod(float2 uv)
{
	return tex2Dlod(TESR_NormalsBuffer, float4(uv, 0.0, 0.0)).xyz * 2 - 1;
}

// explicit-LOD reads: it runs behind a [branch]
float3 SmoothedWorldNormal(float2 uv)
{
	float depth = readDepthLod(uv);
	float3 center = toWorld(uv) * depth;
	float2 uvRadius = min(0.5 * facetRadius / max(depth, 1.0) * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]), 0.1);

	// Neighbours count by distance and by how alike they face: facets of one rock (a few tens of degrees apart) merge, but the
	// flat ground round a small rock (about 90 degrees from its sides) is left out, or it buried the rock (first test).
	float3 n0 = ViewNormalLod(uv);
	float3 sum = n0;
	[unroll]
	for (int i = 0; i < 12; i++) {
		float2 uv2 = uv + FacetTaps[i] * uvRadius;
		float3 p = toWorld(uv2) * readDepthLod(uv2);
		float3 n = ViewNormalLod(uv2);
		sum += n * (saturate(2.0 - length(p - center) / facetRadius) * smoothstep(0.4, 0.85, dot(n0, n)));
	}
	return mul(TESR_ViewTransform, float4(normalize(sum), 1)).xyz; // view to world, as GetWorldNormal
}

// Kept on the upper half so Snow() can rebuild z as the positive root: a surface facing down is stored as facing
// sideways, which gets no snow either way.
float2 EncodeSnowNormal(float3 n)
{
	if (n.z < 0) {
		float l = length(n.xy);
		n.xy = l > 0.001 ? n.xy / l : float2(1, 0);
	}
	return n.xy * 0.5 + 0.5;
}

// Compute the areas of the screen that must be covered with snow
float4 SnowCoverage( VSOUT IN ) : COLOR0
{
	// compute at quarter scale
    float2 uv = IN.UVCoord * 4;
	if (uv.x > 1 || uv.y > 1) return white;

    float depth;
    float4 world = reconstructWorldPosition(uv, depth);

	float2 facets = float2(0.5, 0.5);
	[branch] if (facetRadius > 0) facets = EncodeSnowNormal(SmoothedWorldNormal(uv));

	if (depth > TESR_OrthoData.x) return float4(1, facets, 1); // early out for the sky pixels

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


	return float4(ortho, facets, 1);
}


float4 Snow( VSOUT IN ) : COLOR0
{
	float4 color = tex2D(TESR_SourceBuffer, IN.UVCoord);
	float3 world = toWorld(IN.UVCoord);
	
	float depth = readDepth(IN.UVCoord);
	float3 camera_vector = world * depth;
	float3 eyeDirection = -1 * normalize(world);
	float4 worldPos = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);

	float4 mask = tex2D(TESR_RenderedBuffer, IN.UVCoord);
	float ortho = mask.x;
	float3 norm = GetWorldNormal(IN.UVCoord);

	// UNOFFICIAL 2026-10-16: the smoothed direction from the coverage pass (SmoothFacets); the water test keeps the pixel's own
	float3 shapeNorm = norm;
	[branch] if (facetRadius > 0) {
		float2 xy = mask.yz * 2 - 1;
		shapeNorm = float3(xy, sqrt(saturate(1 - dot(xy, xy))));
	}

	// early out for the character gun, water surfaces/areas and surfaces above the ortho map (such as actors)
	float waterTreshold = (depth/farZ) * 200;
	float isWaterSurface = (dot(norm, float3(0, 0, 1)) > 0.9) && (worldPos.z > TESR_WaterSettings.x - waterTreshold) && (worldPos.z < TESR_WaterSettings.x + waterTreshold);
	if (isWaterSurface || !(ortho > 0)) return color;
	
    color = linearize(color);
	float3 sunColor = linearize(TESR_SunColor).rgb;

	float2 uv = worldPos.xy / 200.0f;
	float3 localNorm = expand(tex2D(TESR_SnowNormSampler, uv).xyz);
	float3 surfaceNormal = normalize(float3(localNorm.xy + shapeNorm.xy, localNorm.z * shapeNorm.z));
	float4 normal =  float4(surfaceNormal, 1);

	float3 snow_tex = TESR_SnowAccumulationColor.rgb;

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

	// float4 snowColor = float4(ambient + diffuse + spec + fresnel, 1);
	float4 snowColor = float4(ambient + diffuse + spec + fresnel + sparkles, 1);
	float pointLightsPower = 0.5;

	shadow.g = 1; // disable point light shadows for debug

	for (int i = 0; i<12; i++){
		snowColor.rgb += GetPointLightContribution(worldPos, TESR_ShadowLightPosition[i], normal) * linearize(float4(TESR_LightColor[i].rgb * TESR_LightColor[i].a, 1)) * pointLightsPower * shadow.g;
		snowColor.rgb += GetPointLightContribution(worldPos, TESR_LightPosition[i], normal) * linearize(float4(TESR_LightColor[12 + i].rgb * TESR_LightColor[12 + i].a, 1)) * pointLightsPower * shadow.g;
	}

	// create a noisy pattern of accumulation over time
	float coverage = saturate(pows(noise(worldPos.xyz / (noiseSize * 0.5)) * noise(worldPos.xyz / (noiseSize * 3)) * noise(worldPos.xyz / noiseSize), 2 - 2 * TESR_SnowAccumulationParams.w));
	coverage = saturate(smoothstep(0.2, 0.4, coverage));
	float vertical = smoothstep(0.5, 0.8, shade(float3(0, 0, 1), shapeNorm));

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
		PixelShader = compile ps_3_0 Snow();
	}
}
