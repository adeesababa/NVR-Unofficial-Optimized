// Bloom.
// https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/
// https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom
// https://www.froyok.fr/blog/2021-12-ue4-custom-bloom/
//
// Designed to work without thresholding, for HDR rendering.
// Should minimize potential bloom issues of filtering artifacts and fireflies.
//

float4 TESR_BloomResolution;
float4 TESR_BloomData; // .x filterRadius x axis, .y filterRadius y axis, .z blendingCoefficient, .w inverse of number of passes for upscale
float4 TESR_BloomGlow;
float4 TESR_BloomGlowExtra;

sampler2D TESR_BloomBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer2 : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer4 : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer8 : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer16 : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer32 : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer64 : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloomBuffer128 : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_RenderedBuffer : register(s8) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

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

#include "Includes/Sampling.hlsl"
#include "Includes/RenderRect.hlsl"

static const bool BloomRectOn = TESR_RenderScale.x < 0.9999 || TESR_RenderScale.y < 0.9999;
float4 RectTap(uniform sampler2D buffer, float2 uv, float2 texel) { return tex2Dlod(buffer, float4(clampToRect(uv, texel), 0, 0)); }

float4 DownsampleBox13Rect(uniform sampler2D buffer, float2 uv, float2 texelSize, float2 sourceTexel) {
    const float x = texelSize.x;
    const float y = texelSize.y;
    float4 a = RectTap(buffer, float2(uv.x - 2 * x, uv.y + 2 * y), sourceTexel);
    float4 b = RectTap(buffer, float2(uv.x, uv.y + 2 * y), sourceTexel);
    float4 c = RectTap(buffer, float2(uv.x + 2 * x, uv.y + 2 * y), sourceTexel);
    float4 d = RectTap(buffer, float2(uv.x - 2 * x, uv.y), sourceTexel);
    float4 e = RectTap(buffer, float2(uv.x, uv.y), sourceTexel);
    float4 f = RectTap(buffer, float2(uv.x + 2 * x, uv.y), sourceTexel);
    float4 g = RectTap(buffer, float2(uv.x - 2 * x, uv.y - 2 * y), sourceTexel);
    float4 h = RectTap(buffer, float2(uv.x, uv.y - 2 * y), sourceTexel);
    float4 i = RectTap(buffer, float2(uv.x + 2 * x, uv.y - 2 * y), sourceTexel);
    float4 j = RectTap(buffer, float2(uv.x - x, uv.y + y), sourceTexel);
    float4 k = RectTap(buffer, float2(uv.x + x, uv.y + y), sourceTexel);
    float4 l = RectTap(buffer, float2(uv.x - x, uv.y - y), sourceTexel);
    float4 m = RectTap(buffer, float2(uv.x + x, uv.y - y), sourceTexel);
    float4 boxes[5];
    boxes[0] = (a + b + d + e) * 0.25;
    boxes[1] = (b + c + e + f) * 0.25;
    boxes[2] = (d + e + g + h) * 0.25;
    boxes[3] = (e + f + h + i) * 0.25;
    boxes[4] = (j + k + l + m) * 0.25;
    return (boxes[0] + boxes[1] + boxes[2] + boxes[3]) * 0.125 + boxes[4] * 0.5;
}

float4 UpsampleTent9Rect(uniform sampler2D buffer, float2 uv, float2 filterRadius, float2 sourceTexel) {
    float x = filterRadius.x;
    float y = filterRadius.y;
    float4 a = RectTap(buffer, float2(uv.x - x, uv.y + y), sourceTexel);
    float4 b = RectTap(buffer, float2(uv.x, uv.y + y), sourceTexel);
    float4 c = RectTap(buffer, float2(uv.x + x, uv.y + y), sourceTexel);
    float4 d = RectTap(buffer, float2(uv.x - x, uv.y), sourceTexel);
    float4 e = RectTap(buffer, float2(uv.x, uv.y), sourceTexel);
    float4 f = RectTap(buffer, float2(uv.x + x, uv.y), sourceTexel);
    float4 g = RectTap(buffer, float2(uv.x - x, uv.y - y), sourceTexel);
    float4 h = RectTap(buffer, float2(uv.x, uv.y - y), sourceTexel);
    float4 i = RectTap(buffer, float2(uv.x + x, uv.y - y), sourceTexel);
    return (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0);
}

float4 UpsampleBlur(uniform sampler2D buffer, float2 uv, float2 filterRadius) {
    [branch] if (BloomRectOn) return UpsampleTent9Rect(buffer, uv, filterRadius, 2.0 * TESR_BloomResolution.zw);
    return UpsampleTent9(buffer, uv, filterRadius);
}

float2 FirstTap(float2 uv) { return BloomRectOn ? clampToRect(uv, 0.5 * TESR_BloomResolution.zw) : uv; }

// Downsample with the 13 tap box.
float4 Downsample(VSOUT IN, uniform sampler2D buffer) : COLOR0 {
    float2 uv = IN.UVCoord;

    float2 texelSize = { TESR_BloomResolution.z, TESR_BloomResolution.w };

    [branch] if (BloomRectOn) return DownsampleBox13Rect(buffer, uv, texelSize, 0.5 * texelSize);

    float4 downsample = DownsampleBox13(buffer, uv, texelSize);

    return downsample;
}

float GlowAmount(float3 c, float threshold)
{
    float peak = max(c.r, max(c.g, c.b));
    float saturation = 1.0f - min(c.r, min(c.g, c.b)) / max(peak, 1.0e-4f);
    float brightness = lerp(dot(c, float3(0.2126f, 0.7152f, 0.0722f)), peak, saturation);
    return saturate((brightness - threshold) / threshold);
}

float3 GlowLinear(float3 c) { return c * (c * (c * 0.305306011f + 0.682171111f) + 0.012522878f); }
float3 GlowEncode(float3 c) {
    float3 s1 = sqrt(c), s2 = sqrt(s1), s3 = sqrt(s2);
    return max(0.662002687f * s1 + 0.684122060f * s2 - 0.323583601f * s3 - 0.0225411470f * c, 0.0f);
}
float4 GlowTap(float4 c, float threshold)
{
    float3 lin = GlowLinear(max(c.rgb, 0.0f));
    lin *= 1.0f + TESR_BloomGlow.z * GlowAmount(lin, threshold);
    return float4(TESR_BloomGlowExtra.y > 0.0f ? lin : GlowEncode(lin), c.a);
}

float4 DownsampleFirst(VSOUT IN, uniform sampler2D buffer) : COLOR0 {
    const float2 uv = IN.UVCoord;
    const float x = TESR_BloomResolution.z;
    const float y = TESR_BloomResolution.w;

    float4 a = tex2D(buffer, FirstTap(float2(uv.x - 2 * x, uv.y + 2 * y)));
    float4 b = tex2D(buffer, FirstTap(float2(uv.x, uv.y + 2 * y)));
    float4 c = tex2D(buffer, FirstTap(float2(uv.x + 2 * x, uv.y + 2 * y)));

    float4 d = tex2D(buffer, FirstTap(float2(uv.x - 2 * x, uv.y)));
    float4 e = tex2D(buffer, FirstTap(float2(uv.x, uv.y)));
    float4 f = tex2D(buffer, FirstTap(float2(uv.x + 2 * x, uv.y)));

    float4 g = tex2D(buffer, FirstTap(float2(uv.x - 2 * x, uv.y - 2 * y)));
    float4 h = tex2D(buffer, FirstTap(float2(uv.x, uv.y - 2 * y)));
    float4 i = tex2D(buffer, FirstTap(float2(uv.x + 2 * x, uv.y - 2 * y)));

    float4 j = tex2D(buffer, FirstTap(float2(uv.x - x, uv.y + y)));
    float4 k = tex2D(buffer, FirstTap(float2(uv.x + x, uv.y + y)));
    float4 l = tex2D(buffer, FirstTap(float2(uv.x - x, uv.y - y)));
    float4 m = tex2D(buffer, FirstTap(float2(uv.x + x, uv.y - y)));

    [branch] if (TESR_BloomGlow.z > 0.0f || TESR_BloomGlowExtra.y > 0.0f) {
        float t = max(TESR_BloomGlow.w, 0.002f);
        a = GlowTap(a, t); b = GlowTap(b, t); c = GlowTap(c, t); d = GlowTap(d, t); e = GlowTap(e, t); f = GlowTap(f, t); g = GlowTap(g, t);
        h = GlowTap(h, t); i = GlowTap(i, t); j = GlowTap(j, t); k = GlowTap(k, t); l = GlowTap(l, t); m = GlowTap(m, t);
    }

    float4 boxes[5];
    boxes[0] = (a + b + d + e) * 0.25;
    boxes[1] = (b + c + e + f) * 0.25;
    boxes[2] = (d + e + g + h) * 0.25;
    boxes[3] = (e + f + h + i) * 0.25;
    boxes[4] = (j + k + l + m) * 0.25;

    [branch] if (TESR_BloomGlowExtra.x > 0.0f) {
        float weights[5] = { 0.125f, 0.125f, 0.125f, 0.125f, 0.5f };
        float4 sum = 0.0f;
        float total = 0.0f;
        [unroll] for (int n = 0; n < 5; n++) {
            float w = weights[n] / (1.0f + max(dot(boxes[n].rgb, float3(0.2126f, 0.7152f, 0.0722f)), 0.0f));
            sum += boxes[n] * w;
            total += w;
        }
        return sum / total;
    }
    float2 weights = float2(0.125, 0.5);
    return boxes[0] * weights[0] + boxes[1] * weights[0] + boxes[2] * weights[0] + boxes[3] * weights[0] + boxes[4] * weights[1];
}

// Intermediary upsamples.
float4 Upsample(VSOUT IN, uniform sampler2D buffer, uniform sampler2D addBuffer) : COLOR0 {
    float2 uv = IN.UVCoord;
    
    const float2 filterRadius = { TESR_BloomData.x, TESR_BloomData.y };
    
    float4 upsample = UpsampleBlur(buffer, uv, filterRadius);
    float4 nextMip = tex2D(addBuffer, uv);
    
    [branch] if (TESR_BloomData.z > 0.0) {
        return float4(lerp(nextMip.rgb, upsample.rgb, TESR_BloomData.z), 1);
    } else {
        return float4(upsample.rgb * TESR_BloomGlow.x + nextMip.rgb, 1);
    }
}

// Last upsample that normalizes the result if needed.
float4 UpsampleLast(VSOUT IN, uniform sampler2D buffer, uniform sampler2D addBuffer) : COLOR0 {
    float2 uv = IN.UVCoord;
    
    const float2 filterRadius = { TESR_BloomData.x, TESR_BloomData.y };
    
    float4 upsample = UpsampleBlur(buffer, uv, filterRadius);
    float4 nextMip = tex2D(addBuffer, uv);
    
    float3 result;
    [branch] if (TESR_BloomData.z > 0.0) {
        result = lerp(nextMip.rgb, upsample.rgb, TESR_BloomData.z);
    } else {
        result = (upsample.rgb * TESR_BloomGlow.x + nextMip.rgb) * TESR_BloomGlow.y;
    }
    [branch] if (TESR_BloomGlowExtra.y > 0.0f) result = delinearize(max(result, 0.0f));
    return float4(result, 1);
}

technique // 0
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 DownsampleFirst(TESR_RenderedBuffer); // output to BloomBuffer
    }	
}

technique // 1
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer); // output to BloomBuffer2
    }	
}

technique // 2
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer2); //output to BloomBuffer4
    }	
}

technique // 3
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer4); // output to BloomBuffer8
    }	
}

technique // 4
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer8); // output to BloomBuffer16
    }	
}

technique // 5
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer16); // output to BloomBuffer32
    }
}

technique // 6
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer32); // output to BloomBuffer64
    }
}

technique // 7
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Downsample(TESR_BloomBuffer64); // output to BloomBuffer128
    }
}

technique // 8
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Upsample(TESR_BloomBuffer128, TESR_BloomBuffer64); // output to BloomBuffer64
    }
}

technique // 9
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Upsample(TESR_BloomBuffer64, TESR_BloomBuffer32); // output to BloomBuffer32
    }
}

technique // 10
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Upsample(TESR_BloomBuffer32, TESR_BloomBuffer16); // output to BloomBuffer16
    }
}

technique // 11
{
    pass {
        VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Upsample(TESR_BloomBuffer16, TESR_BloomBuffer8); // output to BloomBuffer8
    }
}

technique // 12
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Upsample(TESR_BloomBuffer8, TESR_BloomBuffer4); // output to BloomBuffer4
    }	
}

technique // 13
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
        PixelShader = compile ps_3_0 Upsample(TESR_BloomBuffer4, TESR_BloomBuffer2); // output to BloomBuffer2
    }	
}

technique // 14
{
	pass
	{
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 UpsampleLast(TESR_BloomBuffer2, TESR_BloomBuffer); // output to BloomBuffer
	}	
}
