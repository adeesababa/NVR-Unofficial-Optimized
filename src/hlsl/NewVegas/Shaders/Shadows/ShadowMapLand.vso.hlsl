
row_major float4x4 NvrMainModelViewProj : register(c0);
row_major float4x4 NvrShadowModelViewProj : register(c4);
float4 NvrMainEye : register(c8);

struct VS_INPUT {
    float4 position : POSITION;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float3 normal : NORMAL;
    float4 uv : TEXCOORD0;
    float4 vertex_color : COLOR0;
    float4 blend_0 : TEXCOORD1;
    float4 blend_1 : TEXCOORD2;
};

struct VS_OUTPUT {
    float4 blend_0 : COLOR0;
    float4 blend_1 : COLOR1;
    float4 position : POSITION;
    float4 uv : TEXCOORD0;
    float4 vertex_color : TEXCOORD1;
    float4 lPosition : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 binormal : TEXCOORD4;
    float4 normal : TEXCOORD5;
    float4 projectionPosition : TEXCOORD6;
    float4 viewPosition : TEXCOORD7;
};

VS_OUTPUT main(VS_INPUT IN) {
    VS_OUTPUT OUT;

    const float4x4 mainMvp = float4x4(NvrMainModelViewProj[0], NvrMainModelViewProj[1], NvrMainModelViewProj[2], NvrMainModelViewProj[3]);
    const float4x4 shadowMvp = float4x4(NvrShadowModelViewProj[0], NvrShadowModelViewProj[1], NvrShadowModelViewProj[2], NvrShadowModelViewProj[3]);
    const float4 mainClip = mul(mainMvp, IN.position);
    float4 shadowClip = mul(shadowMvp, IN.position);
    shadowClip.z = max(shadowClip.z, 0.0f);
    const float4 shadowNormal = float4(dot(shadowMvp[0].xyz, IN.normal), dot(shadowMvp[1].xyz, IN.normal),
        dot(shadowMvp[2].xyz, IN.normal), dot(shadowMvp[3].xyz, IN.normal));

    OUT.blend_0 = IN.blend_0;
    OUT.blend_1 = IN.blend_1;
    OUT.position = shadowClip;
    OUT.uv = float4(IN.uv.xy, mainClip.xy);
    OUT.vertex_color = float4(saturate(IN.vertex_color.rgb), mainClip.z);
    OUT.lPosition = float4(IN.position.xyz, mainClip.w);
    OUT.tangent = float4(IN.tangent, shadowNormal.x);
    OUT.binormal = float4(IN.binormal, shadowNormal.y);
    OUT.normal = float4(IN.normal, shadowNormal.z);
    OUT.projectionPosition = shadowClip;
    OUT.viewPosition = float4(NvrMainEye.xyz, shadowNormal.w);
    return OUT;
}
