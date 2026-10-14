//
//
// Parameters:

float2 Params : register(c4);
float4 TESR_SkyColor : register(c5);
float4 TESR_SkyLowColor: register(c6);
float4 TESR_HorizonColor: register(c7);
float4 TESR_SunColor: register(c8);
float4 TESR_SunDirection: register(c9);
float4 TESR_SkyData: register(c10); // x: athmosphere thickness, y: sun influence, z: sun strength w: sky strength
float4 TESR_SunAmount : register(c11); // x: dayTime, y:sunGlareAmnt, z:replace sun
float4 TESR_DebugVar: register(c12);
float4 TESR_SunPosition: register(c13);
float4 TESR_SunsetColor: register(c14); // sunsetColor.w is sky strength multiplier
float4 TESR_HDRBloomData: register(c15);
float4 TESR_SunDiskColor: register(c16);
#ifndef PHYSICAL_SKY_ON
    #define PHYSICAL_SKY_ON 0
#endif
#if PHYSICAL_SKY_ON
float4 TESR_PhysSky: register(c17);
float4 TESR_PhysSkySun: register(c18);
sampler2D TESR_SkyViewLUT : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
#endif

static const float4x4 ditherMat = {{0.0588, 0.5294, 0.1765, 0.6471},
									{0.7647, 0.2941, 0.8824, 0.4118},
									{0.2353, 0.7059, 0.1176, 0.5882},
									{0.9412, 0.4706, 0.8235, 0.3259}};

static const float SUNINFLUENCE = 1/TESR_SkyData.y;

// Registers:
//
//   Name         Reg   Size
//   ------------ ----- ----
//   Params       const_4       1
//


// Structures:

struct VS_INPUT {
    float4 color_0 : COLOR0;
    float2 screen : VPOS;
    float3 eye : TEXCOORD0_centroid;
};

struct VS_OUTPUT {
    float4 color_0 : COLOR0;
};

#include "Includes/Helpers.hlsl"
#include "Includes/Sky.hlsl"


VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    float3 up = float3(0, 0, 1);
    float3 eyeDir = normalize(IN.eye.xyz);
    float verticality = pows(compress(dot(eyeDir, up)), 3);
    float sunHeight = shade(TESR_SunPosition.xyz, up);

    float athmosphere = pows(1 - verticality, 8) * TESR_SkyData.x;
    float sunDir = compress(dot(eyeDir, TESR_SunPosition.xyz));
    float sunInfluence = pows(sunDir, SUNINFLUENCE);

    float3 sunColor = GetSunColor(sunHeight, TESR_SkyData.x, TESR_SunAmount.x, TESR_SunDiskColor.rgb, TESR_SunsetColor.rgb);
    float3 skyColor = GetSkyColor(verticality, athmosphere, sunHeight, sunInfluence, TESR_SkyData.z, TESR_SkyColor.rgb, TESR_SkyLowColor.rgb, TESR_HorizonColor.rgb, sunColor);

#if PHYSICAL_SKY_ON
    [branch] if (TESR_PhysSky.x > 0.0f) {
        float horizontal = length(eyeDir.xy), sunHorizontal = length(TESR_PhysSkySun.xy);
        float cosBearing = (horizontal > 1e-4f && sunHorizontal > 1e-4f) ? dot(eyeDir.xy, TESR_PhysSkySun.xy) / (horizontal * sunHorizontal) : 1.0f;
        float2 tableUV = float2(acos(clamp(cosBearing, -1.0f, 1.0f)) / 3.14159265f, sqrt(asin(saturate(eyeDir.z)) / 1.57079633f));
        float3 phys = tex2Dlod(TESR_SkyViewLUT, float4(tableUV, 0.0f, 0.0f)).rgb * (0.001f * TESR_PhysSky.y);
        float3 weatherHue = skyColor / max(luma(skyColor), 1e-6f);
        skyColor = lerp(skyColor, lerp(phys, luma(phys) * weatherHue, TESR_PhysSky.z), TESR_PhysSky.x);
    }
#endif

    OUT.color_0 = delinearize(float4(skyColor * TESR_SunsetColor.w, 1)) ; // multiply sky strength for HDR

    // dithering
	OUT.color_0.rgb += ditherMat[ (IN.screen.x)%4 ][ (IN.screen.y)%4 ] / 255;

    // OUT.color_0.rgb = lerp(green, red, sunDir);

    return OUT;
};
