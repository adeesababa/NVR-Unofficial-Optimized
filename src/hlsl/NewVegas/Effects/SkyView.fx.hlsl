// UNOFFICIAL 2026-10-14: the physical sky's lookup table ([Shaders.Sky.Physical], src/effects/SkyView.cpp). Once a frame, the
// light the air sends toward the eye from every direction of the upper half of the sky (Includes/Atmosphere.hlsl), into a
// 128 x 64 table that the sky shader (SKY.pso.hlsl) reads instead of working the air out for every pixel of the sky
// (Hillaire 2020's sky-view table).
//   u: the angle round from the sun's bearing, 0 (toward the sun) to 1 (away from it); the sky is the same on both sides.
//   v: the height above the horizon, as the square root of the angle (0 horizon, 1 straight up), so most rows sit near the
//      horizon, where the sky changes fastest.
// Stored x 1000 (TableScale): half floats lose the faint light of a low sun otherwise.

float4 TESR_PhysSky;        // x: share of the physical sky, y: brightness, z: weather tint, w: haze (Sky.cpp)
float4 TESR_PhysSkySun;     // xyz: the sun's real direction

#include "Includes/Atmosphere.hlsl"

static const float TableScale = 1000.0f;

struct VSOUT { float4 vertPos : POSITION; float2 UVCoord : TEXCOORD0; };
struct VSIN { float4 vertPos : POSITION0; float2 UVCoord : TEXCOORD0; };

VSOUT FrameVS(VSIN IN) {
    VSOUT OUT = (VSOUT)0.0f;
    OUT.vertPos = IN.vertPos;
    OUT.UVCoord = IN.UVCoord;
    return OUT;
}

float4 SkyViewPS(VSOUT IN) : COLOR0 {
    float azimuth = IN.UVCoord.x * AtmPi;
    float elevation = IN.UVCoord.y * IN.UVCoord.y * (0.5f * AtmPi);
    // In the sun's own frame: the sun along +x at its height, the view at this bearing from it.
    float3 dir = float3(cos(elevation) * cos(azimuth), cos(elevation) * sin(azimuth), max(sin(elevation), 0.002f));
    float3 sun = float3(sqrt(saturate(1.0f - TESR_PhysSkySun.z * TESR_PhysSkySun.z)), 0.0f, TESR_PhysSkySun.z);
    return float4(AtmSky(normalize(dir), sun, 300.0f, TESR_PhysSky.w) * TableScale, 1.0f);
}

technique SkyView {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 SkyViewPS();
    }
}
