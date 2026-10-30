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
float4 TESR_SkyTint: register(c19);     // UNOFFICIAL 2026-10-25 rgb: [Shaders.Sky.Main] TintDay / TintNight for this hour (white = no change)
// UNOFFICIAL 2026-10-29: c32, not c20. c20 is the sky object id the SkyShader hook (Hooks/Shaders.cpp) writes for SKYTEX.pso on every sky
// draw, after NVR's constants are set: here it overwrote the horizon tint with (2, the sun light's red, green), so the horizon came out
// red x 2 (a pink band, red at dusk, in the sky and its water reflection) whatever TintHorizonDay / Night or ColorLook said.
float4 TESR_SkyTintHorizon: register(c32);  // UNOFFICIAL 2026-10-26 rgb: TintHorizonDay / TintHorizonNight for this hour (white = no change)
float4 TESR_SkyLook: register(c21);     // UNOFFICIAL 2026-10-26 x: Saturation for this hour (1 = no change), y: TintGradientHeight
// UNOFFICIAL 2026-10-26: for the true view direction (Includes/Sky.hlsl SkyViewDirection)
float4 TESR_ReciprocalResolution: register(c22);
row_major float4x4 TESR_InvProjectionTransform: register(c23);
row_major float4x4 TESR_InvViewTransform: register(c27);
float4 TESR_RenderScale: register(c31);      // UNOFFICIAL 2026-10-27 RenderScale v2: xy = the world's drawn part of the screen (1 off), zw = 1 / xy
#define SKY_VIEW_DIRECTION
// UNOFFICIAL 2026-10-14 physical sky ([Shaders.Sky.Physical], Includes/Atmosphere.hlsl, Sky.cpp). Compiled in only when on at
// startup (PHYSICAL_SKY_ON, ShaderRecord::LoadShader); otherwise this shader is exactly what it was.
#ifndef PHYSICAL_SKY_ON
    #define PHYSICAL_SKY_ON 0
#endif
#if PHYSICAL_SKY_ON
float4 TESR_PhysSky: register(c17);     // x: share of the physical sky (0 = the game's; it fades out as the sun sets), y: its brightness
                                        // (matched to the weather's sky by Sky.cpp), z: weather tint, w: haze
float4 TESR_PhysSkySun: register(c18);  // xyz: the sun's real direction (TESR_SunPosition is turned upward at night)
// The air's light, worked out once a frame into a small table (Effects/SkyView.fx.hlsl: u the bearing from the sun's, v the
// square root of the height above the horizon; stored x 1000).
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
    // UNOFFICIAL physical sky: the air's own light, at the weather's overall brightness, its colour pulled toward the
    // weather's at the same spot by WeatherTint. Below the horizon the view is held just above it (no ground in the air).
    // Sky.cpp PhysicalSkyBlend mirrors this for the sky light on objects.
    [branch] if (TESR_PhysSky.x > 0.0f) {
        float horizontal = length(eyeDir.xy), sunHorizontal = length(TESR_PhysSkySun.xy);
        float cosBearing = (horizontal > 1e-4f && sunHorizontal > 1e-4f) ? dot(eyeDir.xy, TESR_PhysSkySun.xy) / (horizontal * sunHorizontal) : 1.0f;
        float2 tableUV = float2(acos(clamp(cosBearing, -1.0f, 1.0f)) / 3.14159265f, sqrt(asin(saturate(eyeDir.z)) / 1.57079633f));
        float3 phys = tex2Dlod(TESR_SkyViewLUT, float4(tableUV, 0.0f, 0.0f)).rgb * (0.001f * TESR_PhysSky.y);
        float3 weatherHue = skyColor / max(luma(skyColor), 1e-6f);
        skyColor = lerp(skyColor, lerp(phys, luma(phys) * weatherHue, TESR_PhysSky.z), TESR_PhysSky.x);
    }
#endif
    skyColor = SkyLook(skyColor, SkyViewDirection(IN.screen).z, TESR_SkyTint.rgb, TESR_SkyTintHorizon.rgb, TESR_SkyLook);   // UNOFFICIAL 2026-10-25/26 tint, horizon tint, saturation (Sky.cpp does the same to the sky light on objects)

    OUT.color_0 = delinearize(float4(skyColor * TESR_SunsetColor.w, 1)) ; // multiply sky strength for HDR

    // dithering
	OUT.color_0.rgb += ditherMat[ (IN.screen.x)%4 ][ (IN.screen.y)%4 ] / 255;

    // OUT.color_0.rgb = lerp(green, red, sunDir);

    return OUT;
};
