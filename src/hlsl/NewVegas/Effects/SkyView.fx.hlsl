float4 TESR_PhysSky;
float4 TESR_PhysSkySun;

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
