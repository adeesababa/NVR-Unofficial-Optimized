
sampler2D DiffuseMap : register(s0);
float4 CasterParams : register(c0);

struct PS_INPUT {
    float2 uv    : TEXCOORD0;
    float4 light : TEXCOORD1;
    float4 sun   : TEXCOORD5;
};

float4 main(PS_INPUT IN, float2 vPos : VPOS) : COLOR0 {
    clip(tex2D(DiffuseMap, IN.uv).a - 0.4f);
    float dither = frac(52.9829189f * frac(dot(vPos, float2(0.06711056f, 0.00583715f))));
    clip(IN.sun.w - dither);
    return float4(CasterParams.x > 0.5f ? 0.0f : IN.light.z, 0.0f, 0.0f, 1.0f);
}
