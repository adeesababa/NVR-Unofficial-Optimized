// Shader to compute the complete point-light shadow pass in one screen draw.

float4 TESR_ShadowLightPosition[12];
float4 TESR_LightPosition[12];
float4 TESR_LightColor[24];
float4 TESR_ShadowFade;
float4 TESR_PointShadowNear; // UNOFFICIAL x: NearFade, world units (0 off); y: EdgeFade (0 off); z: LampReachMask, 1 / LightRadiusMult (0 off)
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


// One shadow-casting light: GetPointLightAmount() * weight, with the cubemap read behind an exact test.
// GetPointLightAtten() is exactly 0 once the normalised distance reaches 1 (s = 1 -> (1 - s)^2 = 0), and
// GetPointLightAmount() is 0 for a light without a radius (distance is then INF or NaN, which
// !(distance < 1) also catches), so in those cases the shadow lookup cannot change the result and is
// skipped. The test depends on the pixel, so it is a dynamic branch: a test on constants alone would
// use one of ps_3_0's 16 boolean registers per light and run out. The branch needs an explicit-LOD
// read (gradients are illegal in dynamic flow control); the cubemaps have a single level, so it
// returns what texCUBE did.
// UNOFFICIAL near: NearFade / the light's radius. A blocker within NearFade of the lamp shadows less, none at the lamp
// itself: a real lamp is no point, and a blocker that close casts a shadow too blurred to see, where a point casts it
// sharp, as large as the room, and on every side when the point is inside the blocker (a lamp at the player's head).
// Shaders\Includes\PointShadowForward.hlsl PointShadowNear does the same for the forward shadows.
float PointLightAmountValueLod(samplerCUBE cube, float3 lightDir, float distance, float near) {
	if (TESR_ShadowFade.z == 0) return 1;

	float lightDepth = texCUBElod(cube, float4(lightDir, 0)).r;
	float Shadow = lightDepth + BIAS * distance > distance;
	float blocker = near > 0.0f ? smoothstep(0.0f, near, lightDepth) : 1.0f;
	return lerp(1, Shadow, (lightDepth > 0.0f && lightDepth < 1.0f) * blocker);
}

// UNOFFICIAL EdgeFade (TESR_PointShadowNear.y, a fraction of the lamp's radius): a lamp's light amount is attenuation
// times colour luma times intensity, clamped to 1; a bright lamp keeps that at 1 across most of its radius and drops only
// in the last few percent, so the un-darkened disc round it, and every shadow inside it, ended on a hard circle. Each
// lamp's amount is windowed to 0 over the outer EdgeFade of its radius instead.
float EdgeWindow(float distance) {
	return TESR_PointShadowNear.y > 0.0f ? 1.0f - smoothstep(1.0f - TESR_PointShadowNear.y, 1.0f, distance) : 1.0f;
}

// UNOFFICIAL LampReachMask ([Shaders.ShadowsInteriors.Main]; TESR_PointShadowNear.z = 1 / LightRadiusMult, 0 = off). With
// forward shadows on, this pass's sum is only the room darkening, applied on top of the game's own lamp lighting, which
// already fades with distance and angle; applying a falloff here as well darkened the floor a second time, so a shadow
// on it seemed to stop at the circle where the lamp's clamped amount began to drop. With the switch on a lamp counts in
// full out to its game radius and fades over the rest of the cube map's radius: the game's lighting does the falloff,
// and the lamp's shadows (forward, or this pass's own with forward off) show in full.
float LampReach(float distance) {
	return 1.0f - smoothstep(TESR_PointShadowNear.z, 1.0f, distance);
}

// The light amount this pass gives a lamp at a normalised distance: the old attenuation, or LampReach with the switch.
float LampAmount(float3 lightDir, float distance, float4 normal) {
	return TESR_PointShadowNear.z > 0.0f ? LampReach(distance) : GetPointLightAtten(lightDir, distance, normal);
}

float ShadowedLight(samplerCUBE cube, float4 worldPos, float4 lightPos, float4 normal, float weight) {
	float3 lightDir = lightPos.xyz - worldPos.xyz;
	float distance = length(lightDir) / lightPos.w; // normalize distance over light range
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

	Shadow = saturate(Shadow);
	
	return float4(Shadow, Shadow, Shadow, 1.0f);
}

technique MergedPointShadows {
	pass {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 Shadow();
	}
}
