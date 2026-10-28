
float4 TESR_ShadowData : register(c0);
sampler2D DiffuseMap : register(s0);

struct VS_OUTPUT {
    float4 texcoord_0 : TEXCOORD0;
	float4 texcoord_1 : TEXCOORD1;
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};


PS_OUTPUT main(VS_OUTPUT IN) {
    PS_OUTPUT OUT;
	
	OUT.color_0 = length(IN.texcoord_0) / TESR_ShadowData.z;
    return OUT;
	
};
