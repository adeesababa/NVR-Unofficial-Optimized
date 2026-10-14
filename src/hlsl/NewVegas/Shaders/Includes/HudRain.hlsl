#ifndef HUDRAIN_INCLUDED
#define HUDRAIN_INCLUDED

#ifndef HUD_RAIN
	#define HUD_RAIN 0
#endif
#if HUD_RAIN

float4 HudRainDraw : register(c221);
float4 HudRainLook : register(c222);

static float2 hudRainOffset = 0.0f;
static float2 hudRainSlope = 0.0f;
static float hudRainGloss = 0.0f;
static float hudRainCover = 0.0f;
static float2 hudRainLocal = 0.0f;

float HudRainHash(float2 p) {
	float3 p3 = frac(float3(p.xyx) * 0.1031f);
	p3 += dot(p3, p3.yzx + 33.33f);
	return frac((p3.x + p3.y) * p3.z);
}

float3 HudRainLayer(float2 p, float seed, float period, float2 down, float slide) {
	float2 cell = floor(p);
	float t = HudRainDraw.z / period + HudRainHash(cell + seed);
	float gen = floor(t);
	float life = frac(t);
	float2 key = cell + seed + gen * 7.31f;
	float h0 = HudRainHash(key), h1 = HudRainHash(key + 17.31f), h2 = HudRainHash(key + 41.97f), h3 = HudRainHash(key + 73.13f);
	float present = h0 - HudRainDraw.y;
	float r = lerp(0.12f, 0.38f, h1 * h1) * saturate(1.15f - life * life);
	float2 centre = 0.5f + (float2(h2, h3) - 0.5f) * max(1.0f - 2.0f * r, 0.0f);
	centre += down * slide * life * life * (r > 0.25f ? 1.0f : 0.0f);
	float2 d = frac(p) - centre;
	float2 local = d / max(r, 1e-3f);
	float cover = present > 0.0f ? saturate((1.0f - length(local)) * 6.0f) * saturate(present * 6.0f) : 0.0f;
	return float3(cover, local);
}

void HudRain(float2 uv, float3 position, bool valid) {
	[branch] if (HudRainDraw.x > 0.0f && valid) {
		float2 uvDx = ddx(uv), uvDy = ddy(uv);
		float3 posDx = ddx(position), posDy = ddy(position);
		float unitsPerUV = (length(posDx) + length(posDy)) / max(length(uvDx) + length(uvDy), 1e-6f);
		float tiles = exp2(round(log2(max(unitsPerUV, 1e-3f) / max(HudRainLook.w, 0.05f))));
		float det = uvDx.x * uvDy.y - uvDy.x * uvDx.y;
		float2 heightPerUV = float2(posDx.z * uvDy.y - posDy.z * uvDx.y, posDy.z * uvDx.x - posDx.z * uvDy.x) / (abs(det) > 1e-12f ? det : 1e-12f);
		float slope = length(heightPerUV);
		float2 down = slope > 1e-6f ? -heightPerUV / slope : float2(0.0f, 1.0f);
		float steep = saturate(slope / max(unitsPerUV, 1e-3f));
		float3 n = normalize(cross(posDx, posDy));
		float up = abs(n.z);
		float2 p = uv * tiles;
		float3 a = HudRainLayer(p, 3.0f, 9.0f, down, steep * 0.5f);
		float3 b = HudRainLayer(p * 1.87f + float2(0.23f, 0.46f), 11.0f, 5.5f, down, steep * 0.5f);
		float3 drop = a.x >= b.x ? a : b;
		float scale = a.x >= b.x ? 1.0f : 1.87f;
		drop.x *= HudRainDraw.x * saturate(up + 0.25f);
		hudRainCover = drop.x;
		hudRainLocal = drop.yz;
		float h = sqrt(saturate(1.0f - dot(drop.yz, drop.yz)));
		float2 domeSlope = drop.yz / max(h, 0.35f);
		hudRainSlope = domeSlope * drop.x * HudRainLook.x;
		hudRainOffset = -drop.yz * h * drop.x * HudRainDraw.w * 0.15f / (tiles * scale);
		hudRainGloss = saturate(HudRainDraw.x * HudRainLook.z * saturate(up + 0.25f) + drop.x * HudRainLook.y);
	}
}

#define HudRainUV(uv) ((uv) + hudRainOffset)
#define HUD_RAIN_TEX(s, uv) tex2Dgrad(s, (uv) + hudRainOffset, ddx(uv), ddy(uv))

float3 HudRainNormal(float3 normalTS) {
	return normalize(normalTS + float3(hudRainSlope, 0.0f));
}

float HudRainRoughness(float roughness) {
	return lerp(roughness, 0.05f, hudRainGloss);
}

float2 HudRainLens(float3 lightTS) {
	float r = length(hudRainLocal);
	float rim = smoothstep(0.55f, 0.95f, r) * hudRainCover * 0.45f;
	float2 spot = hudRainLocal + normalize(lightTS).xy * 0.45f;
	float caustic = saturate(1.0f - length(spot) * 2.2f);
	caustic = caustic * caustic * hudRainCover * saturate(lightTS.z * 2.0f) * 1.5f;
	return float2(1.0f - rim, caustic);
}

#else
	#define HudRain(uv, position, valid)
	#define HudRainUV(uv) (uv)
	#define HUD_RAIN_TEX(s, uv) tex2D(s, uv)
	#define HudRainNormal(n) (n)
	#define HudRainRoughness(r) (r)
	#define HudRainLens(l) float2(1.0f, 0.0f)
#endif

#endif
