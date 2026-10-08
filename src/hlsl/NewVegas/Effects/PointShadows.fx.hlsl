float4 TESR_ShadowLightPosition[12];
float4 TESR_LightPosition[12];
float4 TESR_LightColor[24];
float4 TESR_ExtraLights[16];
float4 TESR_ShadowFade;
float4 TESR_PointShadowNear;
float4 TESR_SpotLightPosition;
float4 TESR_SpotLightDirection;
float4 TESR_SpotLightColor;


//sampler_state removed to avoid a artifact. TODO investigate
sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_NormalsBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = NONE; MINFILTER = NONE; MIPFILTER = NONE; };
samplerCUBE TESR_ShadowCubeMapBuffer0 : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer1 : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer2 : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer3 : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer4 : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer5 : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer6 : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer7 : register(s9) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer8 : register(s10) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer9 : register(s11) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
samplerCUBE TESR_ShadowCubeMapBuffer10 : register(s12) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; ADDRESSW = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"
#include "Includes/Shadows.hlsl"

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


float GetSpotLightAmount(float4 worldPos, float4 spotLightPosition, float4 spotLightDirection, float4 normal){
    float3 lightToWorld = spotLightPosition.rgb - worldPos.xyz;
    float3 lightVector = normalize(lightToWorld);

	// radius based attenuation based on https://lisyarus.github.io/blog/graphics/2022/07/30/point-light-attenuation.html
    float radius = spotLightPosition.w;
    float Distance = length(lightToWorld)/radius;
	float s = saturate(Distance * Distance); 
	float atten = saturate(((1 - s) * (1 - s)) / (1 + 5.0 * s));

	float angleCosMax = cos(radians(spotLightDirection.w));
	float angleCosMin = cos(radians(spotLightDirection.w * 0.5));
	float cone = pow(invlerps(angleCosMax, angleCosMin, shades(spotLightDirection.xyz, lightVector * -1)), 2.0);

    float diffuse = shade(lightVector, normal.xyz);
	return diffuse * cone * atten;
}


float PointLightAmountValueLod(samplerCUBE cube, float3 lightDir, float distance, float near) {
	if (TESR_ShadowFade.z == 0) return 1;

	float lightDepth = texCUBElod(cube, float4(lightDir, 0)).r;
	float Shadow = lightDepth + BIAS * distance > distance;
	float blocker = near > 0.0f ? smoothstep(0.0f, near, lightDepth) : 1.0f;
	return lerp(1, Shadow, (lightDepth > 0.0f && lightDepth < 1.0f) * blocker);
}

float EdgeWindow(float distance) {
	return TESR_PointShadowNear.y > 0.0f ? 1.0f - smoothstep(1.0f - TESR_PointShadowNear.y, 1.0f, distance) : 1.0f;
}

float LampReach(float distance) {
	return 1.0f - smoothstep(TESR_PointShadowNear.z, 1.0f, distance);
}

float LampAmount(float3 lightDir, float distance, float4 normal) {
	return TESR_PointShadowNear.z > 0.0f ? LampReach(distance) : GetPointLightAtten(lightDir, distance, normal);
}

float ShadowedLight(samplerCUBE cube, float4 worldPos, float4 lightPos, float4 normal, float weight) {
	float3 lightDir = lightPos.xyz - worldPos.xyz;
	float distance = length(lightDir) / lightPos.w;
	[branch] if (!(distance < 1.0) || weight == 0) return 0;

	float amount = PointLightAmountValueLod(cube, lightDir * float3(-1, -1, 1), distance, TESR_PointShadowNear.x / lightPos.w) * LampAmount(lightDir, distance, normal);
	return saturate(saturate(amount) * weight) * EdgeWindow(distance);
}


float4 Shadow( VSOUT IN ) : COLOR0 {

	float2 uv = IN.UVCoord;
	float depth = readDepth(uv);
    float3 camera_vector = toWorld(uv) * depth;
    float4 world_pos = float4(TESR_CameraPosition.xyz + camera_vector, 1.0f);	
	float4 normal = float4(GetWorldNormal(uv), 1);

	float Shadow = ShadowedLight(TESR_ShadowCubeMapBuffer0, world_pos, TESR_ShadowLightPosition[0], normal, luma(TESR_LightColor[0].rgb) * TESR_LightColor[0].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer1, world_pos, TESR_ShadowLightPosition[1], normal, luma(TESR_LightColor[1].rgb) * TESR_LightColor[1].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer2, world_pos, TESR_ShadowLightPosition[2], normal, luma(TESR_LightColor[2].rgb) * TESR_LightColor[2].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer3, world_pos, TESR_ShadowLightPosition[3], normal, luma(TESR_LightColor[3].rgb) * TESR_LightColor[3].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer4, world_pos, TESR_ShadowLightPosition[4], normal, luma(TESR_LightColor[4].rgb) * TESR_LightColor[4].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer5, world_pos, TESR_ShadowLightPosition[5], normal, luma(TESR_LightColor[5].rgb) * TESR_LightColor[5].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer6, world_pos, TESR_ShadowLightPosition[6], normal, luma(TESR_LightColor[6].rgb) * TESR_LightColor[6].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer7, world_pos, TESR_ShadowLightPosition[7], normal, luma(TESR_LightColor[7].rgb) * TESR_LightColor[7].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer8, world_pos, TESR_ShadowLightPosition[8], normal, luma(TESR_LightColor[8].rgb) * TESR_LightColor[8].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer9, world_pos, TESR_ShadowLightPosition[9], normal, luma(TESR_LightColor[9].rgb) * TESR_LightColor[9].w);
	Shadow += ShadowedLight(TESR_ShadowCubeMapBuffer10, world_pos, TESR_ShadowLightPosition[10], normal, luma(TESR_LightColor[10].rgb) * TESR_LightColor[10].w);
	[branch] if (TESR_ShadowLightPosition[11].w) {
		float4 light11 = GetPointLightDistance(world_pos, TESR_ShadowLightPosition[11]);
		Shadow += saturate(LampAmount(light11.xyz, light11.w, normal)) * EdgeWindow(light11.w);
	}

	[branch] if (TESR_SpotLightPosition.w)
		Shadow += GetSpotLightAmount(world_pos, TESR_SpotLightPosition, TESR_SpotLightDirection, normal) * luma(TESR_SpotLightColor.rgb) * TESR_SpotLightColor.w;
	
	for (int i = 0; i< 12; i++){
		[branch] if (TESR_LightPosition[i].w) {
			float4 light = GetPointLightDistance(world_pos, TESR_LightPosition[i]);
			Shadow += saturate(LampAmount(light.xyz, light.w, normal) * luma(TESR_LightColor[i + 12].rgb) * TESR_LightColor[i + 12].w) * EdgeWindow(light.w);
		}
	}

	[loop] for (int e = 0; e < 16; e++) {
		[branch] if (!TESR_ExtraLights[e].w || Shadow >= 1.0f) break;
		float radius = floor(TESR_ExtraLights[e].w * 0.125f);
		float weight = TESR_ExtraLights[e].w - radius * 8.0f;
		float4 light = GetPointLightDistance(world_pos, float4(TESR_ExtraLights[e].xyz, radius));
		Shadow += saturate(LampAmount(light.xyz, light.w, normal) * weight) * EdgeWindow(light.w);
	}

	Shadow = saturate(Shadow);
	
	return float4(Shadow, Shadow, Shadow, 1.0f);
}

technique MergedPointShadows {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Shadow();
	}
}
