#define SAMPLES 17

float4 TESR_SkinScatterData;
float4 TESR_SkinScatterKernel[SAMPLES];
float4 TESR_SkinScatterDebug;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_DepthBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SkinScatterBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SkinAlbedoBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"

struct VSOUT {
    float4 vertPos : POSITION;
    float2 UVCoord : TEXCOORD0;
};

struct VSIN {
    float4 vertPos : POSITION0;
    float2 UVCoord : TEXCOORD0;
};

VSOUT FrameVS(VSIN IN) {
    VSOUT OUT = (VSOUT)0.0f;
    OUT.vertPos = IN.vertPos;
    OUT.UVCoord = IN.UVCoord;
    return OUT;
}

bool ColourStillSkin(float sceneLuma, float drawnLuma) {
    return sceneLuma > drawnLuma * 0.97f - 0.01f && sceneLuma < drawnLuma * 1.6f + 0.02f;
}

bool DistanceStillSkin(float skinDistance, float sceneDistance) {
    float gap = skinDistance - sceneDistance;
    return abs(gap) < sceneDistance * TESR_SkinScatterData.z + 0.02f || (gap > 0.0f && gap < 1.5f);
}

bool IsSkin(float4 scatter, float drawnLuma, float3 sceneColor, float viewDepth, float2 uv) {
    float distance = viewDepth * length(toWorld(uv));
    return scatter.a > 0.0f
        && DistanceStillSkin(scatter.a, distance)
        && ColourStillSkin(luma(sceneColor), drawnLuma);
}

float3 Decode(float3 c) { return c * c; }
float3 Encode(float3 c) { return sqrt(max(c, 0.0f)); }

float3 Participation(float3 albedo) { return smoothstep(0.01f, 0.04f, albedo); }
float3 AlbedoFactor(float3 albedo) { return pow(max(albedo, 1e-4f), TESR_SkinScatterData.w); }

float3 ToLight(float3 color, float3 highlights, float3 albedoGamma) {
    float3 albedo = Decode(albedoGamma);
    float3 light = Decode(max(color - highlights, 0.0f)) * Participation(albedo) / AlbedoFactor(albedo);
    return min(light, 16.0f);
}

float3 Pack(float3 x) { return x / (1.0f + x); }
float3 Unpack(float3 x) { return x / max(1.0f - x, 1e-4f); }

float4 SkinBlur(float2 uv, float2 dir, bool firstPass) {
    float4 colorM = tex2Dlod(TESR_RenderedBuffer, float4(uv, 0.0f, 0.0f));
    float4 scatterM = tex2Dlod(TESR_SkinScatterBuffer, float4(uv, 0.0f, 0.0f));
    [branch] if (!(scatterM.a > 0.0f)) return colorM;
    float4 albedoM = tex2Dlod(TESR_SkinAlbedoBuffer, float4(uv, 0.0f, 0.0f));
    float depthM = tex2Dlod(TESR_DepthBuffer, float4(uv, 0.0f, 0.0f)).x * farZ;
    float4 sceneM = firstPass ? colorM : tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0f, 0.0f));

    if (!firstPass && TESR_SkinScatterDebug.x > 0.0f && TESR_SkinScatterDebug.x < 2.5f && scatterM.a > 0.0f) {
        float3 tint;
        if (TESR_SkinScatterDebug.x > 1.5f) {
            float ratio = luma(sceneM.rgb) / max(albedoM.a, 1e-3f);
            tint = ratio < 1.0f ? lerp(float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.2f, 1.0f), saturate((1.0f - ratio) * 5.0f))
                                : lerp(float3(0.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f), saturate((ratio - 1.0f) * 5.0f));
        }
        else {
            float distance = depthM * length(toWorld(uv));
            bool distanceOK = DistanceStillSkin(scatterM.a, distance);
            bool colourOK = ColourStillSkin(luma(sceneM.rgb), albedoM.a);
            tint = !distanceOK ? float3(1.0f, 0.0f, 0.0f) : (!colourOK ? float3(0.0f, 0.2f, 1.0f) : float3(0.0f, 1.0f, 0.0f));
        }
        return float4(lerp(sceneM.rgb, tint, 0.6f), sceneM.a);
    }

    if (!IsSkin(scatterM, albedoM.a, sceneM.rgb, depthM, uv)) return colorM;

    float3 lightM = firstPass ? ToLight(colorM.rgb, scatterM.rgb, albedoM.rgb) : Unpack(colorM.rgb);

    float2 stepUV = dir * float2(TESR_ProjectionTransform[0][0], TESR_ProjectionTransform[1][1]) * (0.5f * TESR_SkinScatterData.x / max(depthM, 1.0f));

    float3 blurred = lightM * TESR_SkinScatterKernel[0].rgb;
    [unroll]
    for (int i = 1; i < SAMPLES; i++) {
        float4 sampleUV = float4(uv + TESR_SkinScatterKernel[i].a * stepUV, 0.0f, 0.0f);
        float4 color = tex2Dlod(TESR_RenderedBuffer, sampleUV);
        float4 scatter = tex2Dlod(TESR_SkinScatterBuffer, sampleUV);
        float4 albedo = tex2Dlod(TESR_SkinAlbedoBuffer, sampleUV);
        float depth = tex2Dlod(TESR_DepthBuffer, sampleUV).x * farZ;
        float3 scene = firstPass ? color.rgb : tex2Dlod(TESR_SourceBuffer, sampleUV).rgb;

        float3 light = firstPass ? ToLight(color.rgb, scatter.rgb, albedo.rgb) : Unpack(color.rgb);

        float follow = saturate(TESR_SkinScatterData.y * abs(depthM - depth));
        light = lerp(light, lightM, follow * follow);
        light = IsSkin(scatter, albedo.a, scene, depth, sampleUV.xy) ? light : lightM;

        blurred += TESR_SkinScatterKernel[i].rgb * light;
    }

    if (firstPass) return float4(Pack(blurred), colorM.a);

    float4 original = sceneM;
    float3 albedo = Decode(albedoM.rgb);
    float3 participation = Participation(albedo);
    float3 light = blurred * AlbedoFactor(albedo) * participation
                 + Decode(max(original.rgb - scatterM.rgb, 0.0f)) * (1.0f - participation * participation);
    float3 result = Encode(light) + scatterM.rgb;

    if (TESR_SkinScatterDebug.x > 2.5f) {
        float change = luma(result) - luma(original.rgb);
        float3 tint = change > 0.0f ? float3(1.0f, 0.35f, 0.25f) : float3(0.25f, 0.45f, 1.0f);
        float amount = saturate(length(result - original.rgb) * 8.0f);
        return float4(luma(original.rgb) * 0.25f + tint * amount, original.a);
    }
    return float4(result, original.a);
}

float4 BlurHorizontal(VSOUT IN) : COLOR0 {
    return SkinBlur(IN.UVCoord, float2(1.0f, 0.0f), true);
}

float4 BlurVertical(VSOUT IN) : COLOR0 {
    return SkinBlur(IN.UVCoord, float2(0.0f, 1.0f), false);
}

technique {
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 BlurHorizontal();
    }
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 BlurVertical();
    }
}
