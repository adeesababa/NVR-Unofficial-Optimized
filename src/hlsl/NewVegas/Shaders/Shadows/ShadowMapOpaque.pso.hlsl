#ifndef SHADOW_FIXED_MODE
    #define SHADOW_FIXED_MODE 2
#endif

float4 TESR_ShadowData : register(c0);
float4 TESR_ShadowFormatData : register(c1);

sampler2D DiffuseMap : register(s0) = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = POINT; };

static const float FormatBits = TESR_ShadowFormatData.y;

float2 GetEVSMExponents(in float positiveExponent, in float negativeExponent) {
    const float maxExponent = FormatBits == 0.0 ? 5.54f : 42.0f;

    float2 lightSpaceExponents = float2(positiveExponent, negativeExponent);

    return min(lightSpaceExponents, maxExponent);
}

float2 WarpDepth(float depth, float2 exponents) {
    depth = 2.0f * depth - 1.0f;
    float pos = exp(exponents.x * depth);
    float neg = -exp(-exponents.y * depth);
    return float2(pos, neg);
}

struct VS_OUTPUT {
    float4 texcoord_0 : TEXCOORD0;
	float4 texcoord_1 : TEXCOORD1;
};

struct PS_OUTPUT {
    float4 color_0 : COLOR0;
};

PS_OUTPUT main(VS_OUTPUT IN) {
    PS_OUTPUT OUT;

	
	float depth = IN.texcoord_0.z / IN.texcoord_0.w;

	if (TESR_ShadowData.z) {
		OUT.color_0 = float4(depth, 0.0f, 0.0f, 1.0f);
	}
	else {
#if SHADOW_FIXED_MODE == 0
		float dx = ddx(depth);
		float dy = ddy(depth);
		float moment2 = depth * depth + 0.25 * (dx * dx + dy * dy);
		OUT.color_0 = float4(depth, moment2, 0.0f, 1.0f);
#elif SHADOW_FIXED_MODE == 1
		float2 exponents = GetEVSMExponents(40.0f, 5.0f);
		float2 evsm2 = WarpDepth(depth, exponents);
		OUT.color_0 = float4(evsm2, 0.0f, 1.0f);
#elif SHADOW_FIXED_MODE == 3
		OUT.color_0 = float4(depth, 0.0f, 0.0f, 1.0f);
#else
		float2 exponents = GetEVSMExponents(40.0f, 5.0f);
		float2 evsm2 = WarpDepth(depth, exponents);
		OUT.color_0 = float4(evsm2, evsm2 * evsm2);
#endif
	}

	return OUT;	
};
