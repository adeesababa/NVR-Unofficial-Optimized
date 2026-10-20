float4 TESR_TerrainVariationData : register(c95);
float4 TESR_TerrainVariationExtra : register(c96);

static const float2x2 TERRAIN_VARIATION_SKEW = float2x2(1.0f, 0.0f, -0.57735027f, 1.15470054f);
static const float2 TERRAIN_VARIATION_HASH = float2(1271.5151f, 3337.8237f);
static const float3 TERRAIN_VARIATION_LUMA = float3(0.2126f, 0.7152f, 0.0722f);
static const float TERRAIN_VARIATION_TAP_FADE = 0.1f;
static const float TERRAIN_VARIATION_DOMINANCE_GUARD = 0.02f;

struct StochasticOffsets {
	float2 offset1;
	float2 offset2;
	float  tap1Weight;
};

float2 TerrainVariationHash(float2 s) {
	s = frac(s * TERRAIN_VARIATION_HASH);
	s += dot(s, s.yx + 19.19f);
	return frac((s.xx + s.yy) * s.yx);
}

StochasticOffsets ComputeStochasticOffsets(float2 uv, float scale) {
	float2 skewUV = mul(TERRAIN_VARIATION_SKEW, uv * scale);
	float2 vxID = floor(skewUV);
	float2 f = frac(skewUV);
	float bz = 1.0f - f.x - f.y;

	float3 c0, c1, c2;
	if (bz > 0.0f) {
		c0 = float3(vxID, bz);
		c1 = float3(vxID + float2(0.0f, 1.0f), f.y);
		c2 = float3(vxID + float2(1.0f, 0.0f), f.x);
	}
	else {
		c0 = float3(vxID + 1.0f, -bz);
		c1 = float3(vxID + float2(1.0f, 0.0f), 1.0f - f.y);
		c2 = float3(vxID + float2(0.0f, 1.0f), 1.0f - f.x);
	}

	float3 t;
	if (c1.z > c0.z) { t = c0; c0 = c1; c1 = t; }
	if (c2.z > c0.z) { t = c0; c0 = c2; c2 = t; }
	if (c2.z > c1.z) { t = c1; c1 = c2; c2 = t; }

	float w1 = saturate(c0.z);
	w1 *= w1;
	float w2 = saturate(c1.z);
	w2 *= w2;
	float rankFade = smoothstep(0.0f, TERRAIN_VARIATION_TAP_FADE, c1.z - c2.z);
	float dominanceGuard = smoothstep(0.0f, TERRAIN_VARIATION_DOMINANCE_GUARD, c0.z - c1.z);
	w2 *= lerp(1.0f, rankFade, dominanceGuard);

	StochasticOffsets o;
	o.offset1 = TerrainVariationHash(c0.xy);
	o.offset2 = TerrainVariationHash(c1.xy);
	o.tap1Weight = w1 / max(w1 + w2, 1e-8f);
	return o;
}

float StochasticWeight(float4 s1, float4 s2, float tap1Weight) {
	float h1 = lerp(dot(s1.rgb, TERRAIN_VARIATION_LUMA), s1.a, step(0.001f, s1.a));
	float h2 = lerp(dot(s2.rgb, TERRAIN_VARIATION_LUMA), s2.a, step(0.001f, s2.a));
	float influence = TESR_TerrainVariationData.y;
	float w1 = tap1Weight * (1.0f + influence * h1);
	float w2 = (1.0f - tap1Weight) * (1.0f + influence * h2);
	return w1 / max(w1 + w2, 1e-8f);
}

float4 StochasticSampleAlbedo(sampler2D tex, float2 uv, float2 dx, float2 dy, StochasticOffsets o, out float weight) {
	float4 s1 = tex2Dgrad(tex, uv + o.offset1, dx, dy);
	float4 s2 = tex2Dgrad(tex, uv + o.offset2, dx, dy);
	weight = StochasticWeight(s1, s2, o.tap1Weight);
	return lerp(s2, s1, weight);
}

float4 StochasticSampleWith(sampler2D tex, float2 uv, float2 dx, float2 dy, StochasticOffsets o, float weight) {
	float4 s1 = tex2Dgrad(tex, uv + o.offset1, dx, dy);
	float4 s2 = tex2Dgrad(tex, uv + o.offset2, dx, dy);
	return lerp(s2, s1, weight);
}
