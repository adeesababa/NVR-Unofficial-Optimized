
#ifndef TEX_COUNT
    #define TEX_COUNT 1
#endif

sampler2D BaseMap[TEX_COUNT] : register(s0);
#if TEX_COUNT < 7
sampler2D DisplaceDepth : register(s6);
#endif

struct PS_INPUT {
    float4 projectionPosition : TEXCOORD6;
};

float4 main(PS_INPUT IN) : COLOR0 {
    float4 color = float4(IN.projectionPosition.z / IN.projectionPosition.w, 0.0f, 0.0f, 1.0f);
    [branch] if (IN.projectionPosition.w < -1.0e30f) {
        float unused = 0.0f;
        [unroll] for (int i = 0; i < TEX_COUNT; i++) unused += tex2Dlod(BaseMap[i], float4(0.5f, 0.5f, 0.0f, 0.0f)).a;
#if TEX_COUNT < 7
        unused += tex2Dlod(DisplaceDepth, float4(0.5f, 0.5f, 0.0f, 0.0f)).x;
#endif
        color.y += unused;
    }
    return color;
}
