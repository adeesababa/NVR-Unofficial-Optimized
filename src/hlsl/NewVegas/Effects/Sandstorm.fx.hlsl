float4 TESR_ReciprocalResolution;
float4 TESR_SandstormData;
float4 TESR_SandstormDust;
float4 TESR_SandstormWind;
float4 TESR_SandstormGrains;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

static const float SkyCutoff = 0.996f;
static const float SkyHaze = 0.85f;
static const float NoisePeriod = 256.0f;

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

float hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float valueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float y0 = i.y - NoisePeriod * floor(i.y / NoisePeriod);
    float y1 = y0 + 1.0f;
    y1 -= NoisePeriod * floor(y1 / NoisePeriod);
    float a = hash21(float2(i.x, y0));
    float b = hash21(float2(i.x + 1.0f, y0));
    float c = hash21(float2(i.x, y1));
    float d = hash21(float2(i.x + 1.0f, y1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float4 SandstormPS(VSOUT IN) : COLOR0
{
    float2 uv = IN.UVCoord;

    float3 gammaColor = tex2D(TESR_RenderedBuffer, uv).rgb;
    float depth = readDepth(uv);

    float storm = TESR_SandstormData.x;
    [branch] if (storm <= 0.0f) return float4(gammaColor, 1.0f);
    float3 color = linearize(max(gammaColor, 0.0f));

    float density = TESR_SandstormData.y;
    float heightFalloff = TESR_SandstormData.z;
    float skyDarken = TESR_SandstormData.w;
    float3 dust = TESR_SandstormDust.rgb;
    float streakStrength = TESR_SandstormDust.w;
    float windSpeed = TESR_SandstormWind.z;
    float streakScale = TESR_SandstormGrains.x;
    float nearDistance = TESR_SandstormGrains.y;
    float streakScroll = TESR_SandstormGrains.z;
    float grainScroll = TESR_SandstormGrains.w;

    float isSky = step(SkyCutoff * farZ, depth);
    float3 ray = toWorld(uv);
    float climb = max(0.0f, ray.z) * depth / heightFalloff;
    float heightTerm = climb > 0.0001f ? (1.0f - exp(-climb)) / climb : 1.0f;
    float tau = density * depth * heightTerm;
    float haze = lerp(min(1.0f - exp(-tau), SkyHaze), SkyHaze, isSky) * storm;
    float skyKeep = lerp(1.0f, lerp(1.0f, skyDarken, storm), isSky);
    color = lerp(color * skyKeep, dust, haze);

    float aspect = TESR_ReciprocalResolution.z;
    float3 wind = float3(TESR_SandstormWind.xy, 0.0f);
    float3 camRight = float3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]);
    float3 camUp = float3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]);
    float3 camForward = float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]);
    float2 flow = float2(TESR_ProjectionTransform[0][0] * dot(wind, camRight) - (2.0f * uv.x - 1.0f) * dot(wind, camForward),
                         -TESR_ProjectionTransform[1][1] * dot(wind, camUp) - (2.0f * uv.y - 1.0f) * dot(wind, camForward));
    flow.x *= aspect;
    float flowLength = length(flow);
    float2 windDir = flow / max(flowLength, 0.0001f);
    float2 windPerp = float2(-windDir.y, windDir.x);

    float2 p = float2(uv.x * aspect, uv.y);
    float along = dot(p, windDir);
    float across = dot(p, windPerp);

    float2 streakCoord = float2(across * streakScale, along * streakScale * 0.06f - streakScroll);
    float2 grainCoord = float2(across * streakScale * 2.5f, along * streakScale * 0.625f - grainScroll) + 17.0f;

    float threshold = 0.7f - 0.1f * windSpeed;
    float streaks = smoothstep(threshold, threshold + 0.2f, valueNoise(streakCoord));
    float grains = smoothstep(threshold + 0.05f, threshold + 0.25f, valueNoise(grainCoord));
    float grainLight = saturate(streaks + 0.7f * grains);

    float nearWeight = saturate(depth / nearDistance);
    float facing = saturate(flowLength * 3.0f);
    float amount = streakStrength * storm * nearWeight * facing;
    color += dust * 1.6f * grainLight * amount;

    return float4(delinearize(max(color, 0.0f)), 1.0f);
}

technique
{
    pass
    {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 SandstormPS();
    }
}
