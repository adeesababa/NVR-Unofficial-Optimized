#ifndef HANDBLOOD_INCLUDED
#define HANDBLOOD_INCLUDED

#ifndef HAND_BLOOD
	#define HAND_BLOOD 0
#endif
#if HAND_BLOOD

float4 HandBloodDraw : register(c218);
float4 HandBloodColor : register(c219);

static float handBloodCover = 0.0f;
static float handBloodWet = 0.0f;
static float2 handBloodSlope = 0.0f;

float HandBloodHash(float2 p) {
	float3 p3 = frac(float3(p.xyx) * 0.1031f);
	p3 += dot(p3, p3.yzx + 33.33f);
	return frac((p3.x + p3.y) * p3.z);
}

float HandBloodLayer(float2 p, float seed, float fill, float2 size, float stretch, float2 spray, float pixel, float ragged, float2 down, float run) {
	float2 cell = floor(p);
	float2 f = frac(p);
	float h0 = HandBloodHash(cell + seed);
	float h1 = HandBloodHash(cell + seed + 17.31f);
	float h2 = HandBloodHash(cell + seed + 41.97f);
	float h3 = HandBloodHash(cell + seed + 73.13f);
	float shown = HandBloodDraw.x * fill - h0;
	float r = lerp(size.x, size.y, h1 * h1);
	float len = run * (h3 > 0.35f ? lerp(0.3f, 1.0f, h2) : 0.0f) * (0.5f + r * 2.5f);
	len = min(len, max(0.9f - 2.0f * r, 0.0f));
	float2 centre = 0.5f + (float2(h2, h3) - 0.5f) * max(1.0f - 2.0f * r * stretch - len, 0.0f) - down * len * 0.5f;
	float2 d = f - centre;
	float along = dot(d, spray), across = dot(d, float2(-spray.y, spray.x));
	float dist = length(float2(along / stretch, across));
	float a = atan2(d.y, d.x);
	dist *= 1.0f + ragged * (sin(a * 3.0f + h1 * 6.28f) * 0.18f + sin(a * 7.0f + h2 * 6.28f) * 0.08f);
	float edge = r - dist;
	[flatten] if (len > 0.01f) {
		float t = saturate(dot(d, down) / len);
		float streak = r * lerp(0.45f, 0.22f, t) - length(d - down * len * t);
		float bead = r * 0.5f - length(d - down * len);
		edge = max(edge, max(streak, bead));
	}
	return shown > 0.0f ? saturate(edge / max(pixel, 0.002f) + 0.5f) * saturate(shown * 8.0f) : 0.0f;
}

float3 HandBloodRunCell(float2 p, float i, float seed, float fill, float2 size, float2 down, float run, float pixel, float ragged) {
	float2 across = float2(-down.y, down.x);
	float2 cell = floor(p - down * i);
	float h0 = HandBloodHash(cell + seed);
	float h1 = HandBloodHash(cell + seed + 17.31f);
	float h2 = HandBloodHash(cell + seed + 41.97f);
	float h3 = HandBloodHash(cell + seed + 73.13f);
	float h4 = HandBloodHash(cell + seed + 5.71f);
	float h5 = HandBloodHash(cell + seed + 29.57f);
	float shown = HandBloodDraw.x * fill - h0;
	float r = lerp(size.x, size.y, h1 * h1);
	float2 centre = cell + 0.5f + (float2(h2, h3) - 0.5f) * (1.0f - 2.0f * r);
	float2 d = p - centre;
	float a = atan2(d.y, d.x);
	float rr = r * (1.0f - ragged * (sin(a * 3.0f + h1 * 6.28f) * 0.18f + sin(a * 7.0f + h2 * 6.28f) * 0.08f));
	float edge = rr - length(d);
	float2 slope = d / max(rr, 1e-3f);
	float len = run * (h4 > 0.45f ? lerp(0.25f, 2.8f, h5 * h5) : 0.0f) * (0.6f + r * 2.0f);
	[branch] if (len > 0.02f) {
		float2 start = centre + down * r * 0.55f + across * (h3 - 0.5f) * r * 1.1f;
		float2 ds = p - start;
		float along = dot(ds, down);
		float s = clamp(along, 0.0f, len);
		float wander = (sin(s * 3.1f + h1 * 6.28f) + 0.5f * sin(s * 7.7f + h2 * 6.28f)) * 0.05f * (0.5f + h5);
		float width = r * lerp(0.4f, 0.13f, s / len) * (0.8f + 0.4f * h2);
		float lateral = dot(ds, across) - wander;
		float streak = width - length(float2(along - s, lateral));
		float endWander = (sin(len * 3.1f + h1 * 6.28f) + 0.5f * sin(len * 7.7f + h2 * 6.28f)) * 0.05f * (0.5f + h5);
		float beadR = r * lerp(0.25f, 0.6f, h4);
		float2 toBead = p - (start + down * len + across * endWander);
		float bead = frac(h0 * 7.13f) > 0.35f ? beadR - length(toBead) : -1.0f;
		if (streak > edge) { edge = streak; slope = across * (lateral / max(width, 1e-3f)) * 0.8f + down * saturate((along - s) / max(width, 1e-3f)) * 0.5f; }
		if (bead > edge) { edge = bead; slope = toBead / max(beadR, 1e-3f); }
	}
	float c = shown > 0.0f ? saturate(edge / max(pixel, 0.002f) + 0.5f) * saturate(shown * 8.0f) : 0.0f;
	return float3(c, clamp(slope, -1.0f, 1.0f));
}

void HandBlood(float2 uv, float3 position, bool valid) {
	[branch] if (HandBloodDraw.x > 0.0f) {
		float2 uvDx = ddx(uv);
		float2 uvDy = ddy(uv);
		float3 posDx = ddx(position);
		float3 posDy = ddy(position);
		float unitsPerUV = (length(posDx) + length(posDy)) / max(length(uvDx) + length(uvDy), 1e-6f);
		float tiles = valid ? exp2(round(log2(max(unitsPerUV, 1e-3f) / max(HandBloodDraw.z, 0.05f)))) : 8.0f;
		float2 p = uv * tiles;
		float pixel = (length(uvDx) + length(uvDy)) * 0.5f * tiles;
		float det = uvDx.x * uvDy.y - uvDy.x * uvDx.y;
		float2 heightPerUV = float2(posDx.z * uvDy.y - posDy.z * uvDx.y, posDy.z * uvDx.x - posDx.z * uvDy.x) / (abs(det) > 1e-12f ? det : 1e-12f);
		float slope = length(heightPerUV);
		float2 down = slope > 1e-6f ? -heightPerUV / slope : float2(0.0f, 1.0f);
		float steep = valid ? saturate(slope / max(unitsPerUV, 1e-3f) * 1.4f) : 0.0f;
		float run = HandBloodColor.w * steep;
		float angle = HandBloodDraw.w * 0.37f;
		float2 spray = float2(cos(angle), sin(angle));
		float seed = HandBloodDraw.w;
		float mist = HandBloodLayer(p * 3.1f + 0.37f, seed, 0.75f, float2(0.04f, 0.11f), 1.3f, spray, pixel * 3.1f, 0.0f, down, 0.0f);
		float3 bead = 0.0f;
		[loop] for (int j = 0; j < 6; j++) {
			bool big = j >= 3;
			float3 one = HandBloodRunCell(big ? p * 0.4f + 0.71f : p, big ? (float)(j - 3) : (float)j, seed + (big ? 9.0f : 5.0f),
				big ? 0.22f * HandBloodDraw.x : 0.55f, big ? float2(0.16f, 0.3f) : float2(0.07f, 0.2f), down, big ? run * 0.8f : run,
				big ? pixel * 0.4f : pixel, big ? 1.0f : 0.3f);
			if (one.x > bead.x) bead = one;
		}
		handBloodCover = saturate(max(mist * 0.9f, bead.x));
		handBloodSlope = bead.yz * bead.x;
		handBloodWet = handBloodCover * HandBloodDraw.y;
	}
}

float3 HandBloodAlbedo(float3 baseColor) {
	float3 blood = HandBloodColor.rgb * lerp(float3(0.55f, 0.5f, 0.5f), float3(1.0f, 1.0f, 1.0f), HandBloodDraw.y);
	return lerp(baseColor, blood, handBloodCover);
}

float HandBloodRoughness(float roughness) {
	return lerp(lerp(roughness, 0.55f, handBloodCover), 0.08f, handBloodWet);
}

float3 HandBloodNormal(float3 normalTS) {
	return normalize(normalTS + float3(handBloodSlope * (0.5f + 0.9f * HandBloodDraw.y), 0.0f));
}

#define HAND_BLOOD_METAL(m) ((m) * (1.0f - handBloodCover))

#else
	#define HandBlood(uv, position, valid)
	#define HandBloodAlbedo(c) (c)
	#define HandBloodRoughness(r) (r)
	#define HandBloodNormal(n) (n)
	#define HAND_BLOOD_METAL(m) (m)
#endif

#endif
