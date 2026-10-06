float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SunAmbient;

float4 SmokeInfo;
float4 SmokeParams;
float4 SmokeFlash;
float4 SmokeFlashColor;
float4 SmokeLook;
float4 HazeShape[40];
float4 HazeLook[40];
float4 HazeInfo;
float4 BulletFrom[16];
float4 BulletWay[16];
float4 BulletMove;
float4 EnvInfo;

sampler2D TESR_RenderedBuffer : register(s0) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
sampler2D TESR_DepthBuffer : register(s1) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
sampler2D TESR_SmokeBuffer : register(s2) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE;
};
sampler2D TESR_DepthBufferViewModel : register(s3) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
sampler2D TESR_SmokeBufferLow : register(s4) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE;
};
sampler2D TESR_SmokeHaze : register(s5) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE;
};
sampler2D TESR_SmokeEnv : register(s6) = sampler_state {
	ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE;
};
sampler2D TESR_SmokeCut : register(s7) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Depth.hlsl"

float4 SmokeAt(float2 uv) { return tex2D(TESR_SmokeBuffer, uv) + tex2D(TESR_SmokeBufferLow, uv); }
float4 SmokeAtLod(float2 uv) { return tex2Dlod(TESR_SmokeBuffer, float4(uv, 0, 0)) + tex2Dlod(TESR_SmokeBufferLow, float4(uv, 0, 0)); }

float hash3(float3 p) {
	p = frac(p * 0.3183099 + 0.1);
	p *= 17.0;
	return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise3(float3 x) {
	float3 i = floor(x);
	float3 f = frac(x);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(lerp(hash3(i), hash3(i + float3(1, 0, 0)), f.x), lerp(hash3(i + float3(0, 1, 0)), hash3(i + float3(1, 1, 0)), f.x), f.y),
	            lerp(lerp(hash3(i + float3(0, 0, 1)), hash3(i + float3(1, 0, 1)), f.x), lerp(hash3(i + float3(0, 1, 1)), hash3(i + float3(1, 1, 1)), f.x), f.y), f.z);
}
float erfcApprox(float x) {
	float z = abs(x);
	float t = 1.0 + z * (0.278393 + z * (0.230389 + z * (0.000972 + z * 0.078108)));
	float e = 1.0 / (t * t * t * t);
	return x >= 0.0 ? e : 2.0 - e;
}
float3 LightDirection() { return SmokeInfo.z > 0.5 ? normalize(TESR_SunDirection.xyz) : float3(0.0, 0.0, 1.0); }

struct SEGMENT { float4 position : POSITION0; float4 a : TEXCOORD0; float4 b : TEXCOORD1; float4 c : TEXCOORD2; float4 d : TEXCOORD3; };
SEGMENT SegmentVS(SEGMENT input) { return input; }

float4 SegmentPS(SEGMENT input, float2 vpos : VPOS) : COLOR0 {
	float2 uv = (vpos + 0.5) * TESR_ReciprocalResolution.xy * max(SmokeLook.z, 1.0);
	float viewDepth;
	float3 world = reconstructWorldPosition(uv, viewDepth).xyz;
	float3 cam = mul(float4(0.0, 0.0, 0.0, 1.0), TESR_InvViewTransform).xyz;
	float3 toPixel = world - cam;
	float sceneDist = length(toPixel);
	float3 dir = toPixel / max(sceneDist, 0.0001);
	float scale = max(SmokeLook.z, 1.0);
	[branch] if (scale > 1.0) {
		const float2 corners[4] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0) };
		float2 reach = (0.5 * scale - 0.5) * TESR_ReciprocalResolution.xy;
		[unroll] for (int k = 0; k < 4; k++) {
			float cornerDepth;
			float3 corner = reconstructWorldPositionLod(uv + corners[k] * reach, cornerDepth).xyz;
			sceneDist = max(sceneDist, length(corner - cam));
		}
	}
	float debug = SmokeInfo.y;

	float4 a = input.a;
	float4 b = input.b;
	float4 c = input.c;
	float4 d = input.d;
	float3 A = a.xyz - cam;
	float3 AB = b.xyz - a.xyz;
	float len2 = dot(AB, AB);
	float sRaw = 0.0;
	if (len2 > 0.0001) {
		float bb = dot(dir, AB), cc = dot(dir, A), ee = dot(AB, A);
		float denom = len2 - bb * bb;
		sRaw = denom > 0.00001 ? (bb * cc - ee) / denom : 0.0;
	}
	float flags = floor(c.w + 0.5);
	float fillA = fmod(flags, 16.0) / 15.0;
	float fillB = fmod(floor(flags / 16.0 + 0.001), 16.0) / 15.0;
	float segLen = sqrt(len2);
	float eA = fillA < 0.999 ? min(a.w / max(segLen, 0.01), 0.5) : 0.0;
	float eB = fillB < 0.999 ? min(b.w / max(segLen, 0.01), 0.5) : 0.0;
	float s = clamp(sRaw, -eA, 1.0 + eB);
	float sEnd = saturate(s);
	float along = 1.0;
	if (eA > 0.0) along *= lerp(fillA, 1.0, smoothstep(-eA, eA, s));
	if (eB > 0.0) along *= lerp(fillB, 1.0, 1.0 - smoothstep(1.0 - eB, 1.0 + eB, s));
	float3 P = A + s * AB;
	float r = max(lerp(a.w, b.w, sEnd), 0.05);
	float tc = dot(P, dir);
	float t = clamp(tc, 0.0, sceneDist + debug * 100000.0);
	float3 off = P - t * dir;
	float rho2 = dot(off, off);
	if (rho2 > 9.0 * r * r) discard;
	float3 axis = segLen > 0.01 ? AB / segLen : float3(0.0, 0.0, 1.0);
	float age = lerp(d.z, d.w, sEnd);

	float style = floor(flags / 256.0 + 0.001);
	float pillows = floor(style / 16.0 + 0.001) / 15.0;
	float streaks = fmod(style, 16.0) / 15.0;
	float coord = lerp(d.x, d.y, s) + SmokeParams.w * 0.1;
	float3 q = (-off / r) * 1.2 + axis * coord;
	float3 acr = -off / r - axis * dot(-off / r, axis);
	float3 qFine = lerp(q * 2.7 + 13.1, acr * 3.6 + axis * (coord * 0.35) + 3.3, streaks);
	float lumps = noise3(q);
	float fine = noise3(qFine);
	lumps = lerp(lumps, 1.0 - abs(2.0 * lumps - 1.0), pillows);
	float re = r * (0.85 - 0.05 * pillows + (0.35 + 0.1 * pillows) * lumps);
	if (rho2 > 6.25 * re * re) discard;
	float g = exp(-rho2 / (re * re));
	g *= lerp(1.0, 0.45 + 0.75 * lumps, pillows) * lerp(1.0, 0.35 + fine, streaks);
	float erode = SmokeParams.x * (0.25 + 0.75 * age) * (1.0 - fine) * (1.0 - 0.4 * pillows);
	g = saturate((g - erode) / max(1.0 - erode, 0.05));
	if (g <= 0.0) discard;

	float sinA = segLen > 0.01 ? length(cross(dir, AB)) / segLen : 1.0;
	float path = 1.7725 * re / max(sinA, 1.7725 * re / (segLen + 1.7725 * re));
	float occ = max(saturate((sceneDist - tc) / max(r, 2.0) + 0.5), debug);
	float nearFade = saturate((tc - 6.0) / 18.0);
	float density = lerp(c.x, c.y, sEnd);
	float dens = density * g * path * along * occ * nearFade;
	if (dens <= 0.000001) discard;

	float3 L = LightDirection();
	float3 o = -off;
	float3 Lp = L - axis * dot(L, axis);
	float lp = length(Lp);
	float x = dot(o, Lp) / max(lp, 0.0001);
	float side2 = max(dot(o, o) - x * x, 0.0);
	float lightPath = 0.8862 * re / max(lp, 0.15) * exp(-side2 / (re * re)) * erfcApprox(x / re);
	lightPath = min(lightPath, segLen + 1.7725 * re);
	float lit = exp(-density * lightPath * 3.0 * SmokeParams.z);
	float3 across = o / (1.5 * re);
	float3 normal = normalize(across - dir * sqrt(saturate(1.0 - dot(across, across))) + 0.0001);
	float shape = clamp(1.0 + 0.4 * (dot(normal, L) - dot(-dir, L)), 0.7, 1.35);
	lit *= lerp(1.0, shape, SmokeLook.y);

	float3 toFlash = SmokeFlash.xyz - (cam + t * dir);
	float flash = SmokeFlash.w * exp(-dot(toFlash, toFlash) / max(SmokeFlashColor.w * SmokeFlashColor.w, 1.0));
	return float4(dens, dens * lit, dens * flash, dens * t);
}

struct VSIN { float4 position : POSITION0; float2 uv : TEXCOORD0; };
struct VSOUT { float4 position : POSITION0; float2 uv : TEXCOORD0; };
VSOUT FrameVS(VSIN input) { VSOUT o; o.position = input.position; o.uv = input.uv; return o; }

float4 EnvPS(VSOUT input) : COLOR0 {
	float3 env = 0.0;
	[unroll] for (int gy = 0; gy < 8; gy++)
		[unroll] for (int gx = 0; gx < 8; gx++)
			env += tex2Dlod(TESR_RenderedBuffer, float4(0.0625 + 0.125 * gx, 0.0625 + 0.125 * gy, 0, 0)).rgb;
	env = 1.8 * env / 64.0 + 0.05;
	env = lerp(dot(env, float3(0.299, 0.587, 0.114)).xxx, env, 0.5);
	return float4(env, saturate(EnvInfo.x));
}

float BlobStretch(float s, float kk, float iR2) {
	return s * (kk * kk - s * s * iR2 * (0.6666667 * kk - 0.2 * s * s * iR2));
}

static const float BulletBehind = 30.0;
float BulletHole(int j, float3 cam, float3 dir, float sceneDist, out float t0, out float t1, out float tPuff) {
	float4 from = BulletFrom[j], way = BulletWay[j];
	float since = from.w;
	float open = HazeInfo.z > 0.0 ? HazeInfo.z : 0.05, life = HazeInfo.w > 0.0 ? HazeInfo.w : 1.2;
	float closeFrom = max(open, 0.1 * life);
	float closing = saturate((since - closeFrom) / max(life - closeFrom, 0.05));
	float opened = saturate(since / open);
	float rt = (6.0 + 3.0 * min(way.w, 3.0)) * opened * (2.0 - opened) * (1.0 - closing * sqrt(closing));
	float3 w = cam - from.xyz;
	float wb = dot(w, way.xyz), db = dot(dir, way.xyz);
	float3 wp = w - way.xyz * wb, dp = dir - way.xyz * db;
	float A = dot(dp, dp), B = 2.0 * dot(wp, dp), C = dot(wp, wp);
	float ta = 6.0, tb = sceneDist;
	float u0 = (-BulletBehind - wb) / (abs(db) > 0.00001 ? db : 0.00001), u1 = (1500.0 - wb) / (abs(db) > 0.00001 ? db : 0.00001);
	ta = max(ta, min(u0, u1)); tb = min(tb, max(u0, u1));
	float tn = clamp(-B / (2.0 * max(A, 0.000001)), ta, tb);
	float3 off = wp + dp * tn;
	float dmin = length(off);
	float hole = 0.0;
	tPuff = db > 0.00001 ? (35.0 - wb) / db : 0.0;
	t0 = 1.0e9; t1 = -1.0e9;
	[branch] if (rt >= 0.3 && way.w > 0.0 && tb > ta && dmin < 1.7 * rt) {
		float3 e1 = normalize(cross(way.xyz, abs(way.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
		float3 e2 = cross(way.xyz, e1);
		float2 z = float2(dot(off, e1), dot(off, e2)) / max(dmin, 0.001);
		float2 z2 = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y);
		float2 z3 = float2(z2.x * z.x - z2.y * z.y, z2.x * z.y + z2.y * z.x);
		float2 z5 = float2(z3.x * z2.x - z3.y * z2.y, z3.x * z2.y + z3.y * z2.x);
		float seed = frac((SmokeParams.w - since) * 0.6180339) * 97.0;
		float3 phase = seed * float3(1.0, 1.7, 2.3) + (wb + db * tn) * float3(0.0012, 0.0018, 0.0026) + since * float3(0.9, -1.3, 1.7);
		float3 ps, pc;
		sincos(phase, ps, pc);
		float odd = smoothstep(0.0, 0.5, dmin / max(rt, 0.001));
		float ragged = 0.42 * (z2.y * pc.x + z2.x * ps.x) + odd * (0.33 * (z3.y * pc.y + z3.x * ps.y) + 0.25 * (z5.y * pc.z + z5.x * ps.z));
		float rn = rt * (1.0 + 0.35 * ragged);
		float x = dmin / rn;
		float k = exp(-2.0 * x * x) * saturate(way.w);
		float ta2 = ta, tb2 = tb;
		float R2 = 1.5625 * rn * rn;
		float disc = B * B - 4.0 * A * (C - R2);
		float sq = sqrt(max(disc, 0.0));
		float h0 = A > 0.000001 ? max(ta2, (-B - sq) / (2.0 * A)) : ta2;
		float h1 = A > 0.000001 ? min(tb2, (-B + sq) / (2.0 * A)) : (C < R2 ? tb2 : ta2);
		k *= smoothstep(0.3, 3.0, rt);
		[branch] if ((A <= 0.000001 || disc > 0.0) && h1 > h0 && k > 0.0) { t0 = h0; t1 = h1; hole = k; }
	}
	return hole;
}

float BulletField(float3 cam, float3 dir, float sceneDist, out float t0, out float t1, out float tPuff) {
	float total = 0.0, weight = 0.0, from = 0.0, to = 0.0, puff = 0.0;
	[loop] for (int j = 0; j < 16; j++) {
		if (j >= HazeInfo.y) break;
		float h0, h1, hp;
		float c = BulletHole(j, cam, dir, sceneDist, h0, h1, hp);
		[branch] if (c > 0.0) { float w = c * c * c; total += c; weight += w; from += w * h0; to += w * h1; puff += w * hp; }
	}
	t0 = weight > 0.0 ? from / weight : 1.0e9;
	t1 = weight > 0.0 ? to / weight : -1.0e9;
	tPuff = weight > 0.0 ? puff / weight : 0.0;
	return total > 0.0 ? smoothstep(0.07, 0.375, total) : 0.0;
}

float SegmentCut(float2 uv, float3 cam, float3 dir, float surface) {
	float cut = 0.0;
	float4 smoke = SmokeAtLod(uv);
	[branch] if (smoke.r >= 0.0005 && HazeInfo.y > 0.5) {
		float smokeDist = smoke.a / max(smoke.r, 0.000001);
		float spread = 6.0 + 0.15 * smokeDist;
		float total = 0.0, covered = 0.0, strongest = 0.0;
		[loop] for (int j = 0; j < 16; j++) {
			if (j >= HazeInfo.y) break;
			float t0, t1, tPuff;
			float c = BulletHole(j, cam, dir, surface, t0, t1, tPuff);
			[branch] if (c > 0.0) {
				total += c;
				covered += c * saturate((min(t1, smokeDist + spread) - max(t0, smokeDist - spread)) / (2.0 * spread));
				strongest = max(strongest, c);
			}
		}
		cut = total > 0.0 ? smoothstep(0.07, 0.375, total) * saturate(covered / strongest) : 0.0;
	}
	return cut;
}

struct HazeOutput {
	float4 haze : COLOR0;
	float4 cut : COLOR1;
};

HazeOutput HazePS(VSOUT input, float2 vpos : VPOS) {
	float scale = max(SmokeLook.z, 1.0);
	float2 uv = (vpos + 0.5) * TESR_ReciprocalResolution.xy * scale;
	float viewDepth;
	float3 world = reconstructWorldPositionLod(uv, viewDepth).xyz;
	float3 cam = mul(float4(0.0, 0.0, 0.0, 1.0), TESR_InvViewTransform).xyz;
	float3 toPixel = world - cam;
	float sceneDist = length(toPixel);
	float3 dir = toPixel / max(sceneDist, 0.0001);
	const float2 corners[4] = { float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0) };
	float2 reach = (0.5 * scale - 0.5) * TESR_ReciprocalResolution.xy;
	[unroll] for (int k = 0; k < 4; k++) {
		float cornerDepth;
		float3 corner = reconstructWorldPositionLod(uv + corners[k] * reach, cornerDepth).xyz;
		sceneDist = min(sceneDist, length(corner - cam));
	}
	float3 L = LightDirection();
	float flashReach2 = max(SmokeFlashColor.w * SmokeFlashColor.w, 1.0);
	float4 sum = 0.0;
	[loop] for (int i = 0; i < 40; i++) {
		if (i >= HazeInfo.x) break;
		float4 shape = HazeShape[i];
		float3 oc = shape.xyz - cam;
		float R2 = shape.w * shape.w;
		float tc = dot(oc, dir);
		float d2 = dot(oc, oc) - tc * tc;
		[branch] if (d2 < R2) {
			float h = sqrt(R2 - d2);
			float s0 = max(-h, 6.0 - tc), s1 = min(h, sceneDist - tc);
			[branch] if (s1 > s0) {
				float iR2 = 1.0 / R2, kk = 1.0 - d2 * iR2;
				float F1 = s1 * (kk * kk - s1 * s1 * iR2 * (0.6666667 * kk - 0.2 * s1 * s1 * iR2));
				float F0 = s0 * (kk * kk - s0 * s0 * iR2 * (0.6666667 * kk - 0.2 * s0 * s0 * iR2));
				float tau = HazeLook[i].x * 0.9375 / shape.w * (F1 - F0);
				float sm = 0.5 * (s0 + s1);
				float3 rel = (dir * (tc + sm) - oc) / shape.w;
				float lit = 0.75 + 0.25 * dot(rel, L);
				float3 toFlash = SmokeFlash.xyz - shape.xyz;
				float flash = SmokeFlash.w * exp(-dot(toFlash, toFlash) / (flashReach2 + 0.25 * R2));
				sum += tau * float4(1.0, lit, flash, tc + sm);
			}
		}
	}
	[branch] if (sum.x > 0.00001 && HazeInfo.y > 0.5) {
		float h0, h1, hp;
		float k = BulletField(cam, dir, sceneDist, h0, h1, hp);
		[branch] if (k > 0.0 && h1 > h0) {
			float inside = 0.0;
			[loop] for (int i = 0; i < 40; i++) {
				if (i >= HazeInfo.x) break;
				float4 shape = HazeShape[i];
				float3 oc = shape.xyz - cam;
				float R2 = shape.w * shape.w;
				float tc = dot(oc, dir);
				float d2 = dot(oc, oc) - tc * tc;
				[branch] if (d2 < R2) {
					float h = sqrt(R2 - d2);
					float iR2 = 1.0 / R2, kk = 1.0 - d2 * iR2;
					float s0 = max(-h, h0 - tc), s1 = min(h, h1 - tc);
					[branch] if (s1 > s0) inside += HazeLook[i].x * 0.9375 / shape.w * (BlobStretch(s1, kk, iR2) - BlobStretch(s0, kk, iR2));
				}
			}
			float seen = lerp(exp(-sum.x), exp(-max(sum.x - inside, 0.0)), k);
			sum *= saturate(-log(max(seen, 0.000001)) / sum.x);
		}
	}
	[branch] if (sum.x > 0.00001) {
		float3 p = (dir * (sum.w / sum.x) + TESR_CameraPosition.xyz) / 45.0 + float3(0.0, 0.0, -SmokeParams.w * 0.02);
		float n = 0.65 * noise3(p) + 0.35 * noise3(p * 2.3 + 7.1);
		sum *= lerp(0.25, 1.7, n);
	}
	HazeOutput o;
	o.haze = float4(sum.xyz, sceneDist);
	o.cut = float4(SegmentCut(uv, cam, dir, sceneDist), 0.0, 0.0, sceneDist);
	return o;
}

float CutAt(float2 suv, float surface) {
	float2 hres = ceil(1.0 / (4.0 * TESR_ReciprocalResolution.xy));
	float2 p = suv * hres - 0.5;
	float2 f = frac(p);
	float2 at = (floor(p) + 0.5) / hres, step = 1.0 / hres;
	float4 c00 = tex2Dlod(TESR_SmokeCut, float4(at, 0, 0));
	float4 c10 = tex2Dlod(TESR_SmokeCut, float4(at + float2(step.x, 0.0), 0, 0));
	float4 c01 = tex2Dlod(TESR_SmokeCut, float4(at + float2(0.0, step.y), 0, 0));
	float4 c11 = tex2Dlod(TESR_SmokeCut, float4(at + step, 0, 0));
	float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	float4 off = abs(float4(c00.a, c10.a, c01.a, c11.a) - surface) / (2.0 + 0.04 * surface);
	w *= 1.0 / (1.0 + off * off * off) + 0.0001;
	return dot(float4(c00.x, c10.x, c01.x, c11.x), w) / dot(w, 1.0);
}

float4 HazeAt(float2 suv, float surface) {
	float2 hres = ceil(1.0 / (4.0 * TESR_ReciprocalResolution.xy));
	float2 p = suv * hres - 0.5;
	float2 f = frac(p);
	float2 at = (floor(p) + 0.5) / hres, step = 1.0 / hres;
	float4 h00 = tex2Dlod(TESR_SmokeHaze, float4(at, 0, 0));
	float4 h10 = tex2Dlod(TESR_SmokeHaze, float4(at + float2(step.x, 0.0), 0, 0));
	float4 h01 = tex2Dlod(TESR_SmokeHaze, float4(at + float2(0.0, step.y), 0, 0));
	float4 h11 = tex2Dlod(TESR_SmokeHaze, float4(at + step, 0, 0));
	float4 w = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	float4 off = abs(float4(h00.a, h10.a, h01.a, h11.a) - surface) / (2.0 + 0.04 * surface);
	w *= 1.0 / (1.0 + off * off * off) + 0.0001;
	return (h00 * w.x + h10 * w.y + h01 * w.z + h11 * w.w) / dot(w, 1.0);
}

float4 CompositePS(VSOUT input, float2 vpos : VPOS) : COLOR0 {
	float4 color = tex2D(TESR_RenderedBuffer, input.uv);
	const float2 suv = (vpos + 0.5) * TESR_ReciprocalResolution.xy;
	float4 smokeBuffer = SmokeAt(suv);
	float4 haze = tex2D(TESR_SmokeHaze, suv);
	float tau = smokeBuffer.r + haze.r;
	if (tau < 0.0005) return color;
	if (SmokeInfo.w > 0.5 && SmokeInfo.y < 0.5 && tex2D(TESR_DepthBufferViewModel, input.uv).x > 0.0) return color;
	float transmittance = exp(-tau);
	if (SmokeInfo.y > 0.5) {
		color.rgb = lerp(color.rgb, float3(1.0, 0.05, 0.05), saturate(1.0 - exp(-4.0 * tau)));
		return color;
	}
	float viewDepth;
	float3 world = reconstructWorldPosition(input.uv, viewDepth).xyz;
	float3 camera = mul(float4(0.0, 0.0, 0.0, 1.0), TESR_InvViewTransform).xyz;
	float surface = length(world - camera);
	haze = HazeAt(suv, surface);
	float smokeDist = smokeBuffer.a / max(smokeBuffer.r, 0.000001);
	[branch] if (smokeBuffer.r >= 0.0005 && HazeInfo.y > 0.5) {
		float cut = CutAt(suv, surface);
		cut *= 1.0 - saturate(BulletMove.x) * (1.0 - smoothstep(60.0, 150.0, smokeDist));
		[branch] if (cut > 0.0) {
			float seen = lerp(exp(-smokeBuffer.r), 1.0, cut);
			smokeBuffer *= saturate(-log(max(seen, 0.000001)) / max(smokeBuffer.r, 0.000001));
		}
	}
	if (SmokeLook.w > 0.5) smokeBuffer *= saturate((surface - smokeDist) / max(4.0, 0.08 * smokeDist) + 0.5);
	float tauSmoke = smokeBuffer.r, tauHaze = haze.r;
	tau = tauSmoke + tauHaze;
	if (tau < 0.0005) return color;
	transmittance = exp(-tau);
	const float age = 0.35;
	float3 env = tex2Dlod(TESR_SmokeEnv, float4(0.5, 0.5, 0, 0)).rgb;
	float3 dir = normalize(world - camera);
	float cosView = dot(dir, LightDirection());
	float forward = 0.064 / pow(1.36 - 1.2 * cosView, 1.5);
	float outdoors = SmokeInfo.z;
	float3 ambient = lerp(env, 0.72 * TESR_SunAmbient.rgb, 0.5 * outdoors) * lerp(0.75, 0.8, outdoors);
	float3 direct = outdoors > 0.5 ? 0.5 * TESR_SunColor.rgb * (0.4 + 1.1 * forward) : 0.45 * env;
	float deepSmoke = 1.0 - 0.5 * SmokeParams.z * (1.0 - exp(-tauSmoke));
	float deepHaze = 1.0 - 0.5 * SmokeParams.z * (1.0 - exp(-tauHaze));
	float3 smokeLight = ambient * deepSmoke + direct * saturate(smokeBuffer.g / max(tauSmoke, 0.000001)) +
		SmokeFlashColor.rgb * (smokeBuffer.b / max(tauSmoke, 0.000001));
	float3 hazeLight = ambient * deepHaze + direct * saturate(haze.g / max(tauHaze, 0.000001)) +
		SmokeFlashColor.rgb * (haze.b / max(tauHaze, 0.000001));
	[branch] if (SmokeLook.y > 0.001 && smokeBuffer.r >= 0.0005) {
		float2 px = TESR_ReciprocalResolution.xy * 2.0;
		float hL = 1.0 - exp(-SmokeAtLod(suv - float2(px.x, 0.0)).r);
		float hR = 1.0 - exp(-SmokeAtLod(suv + float2(px.x, 0.0)).r);
		float hU = 1.0 - exp(-SmokeAtLod(suv - float2(0.0, px.y)).r);
		float hD = 1.0 - exp(-SmokeAtLod(suv + float2(0.0, px.y)).r);
		float3 bump = normalize(float3(hL - hR, hD - hU, 0.3));
		float3 lightView = mul((float3x3)TESR_InvViewTransform, LightDirection());
		float3 lightScreen = normalize(float3(lightView.x, lightView.y, 0.3 - lightView.z));
		float relief = (dot(bump, lightScreen) * 0.5 + 0.5) / max(lightScreen.z * 0.5 + 0.5, 0.2);
		smokeLight *= lerp(1.0, clamp(relief, 0.8, 1.25), SmokeLook.y);
	}
	float3 smoke = (smokeLight * tauSmoke + hazeLight * tauHaze) / tau * SmokeParams.y;
	float tint = SmokeLook.x;
	float thin = exp(-1.5 * tau);
	smoke *= lerp(1.0.xxx, float3(0.86, 0.93, 1.06), thin * tint) * lerp(1.0, 1.12 - 0.24 * age, tint);
	float3 through = exp(-tau * lerp(1.0.xxx, float3(0.93, 1.0, 1.08), tint));
	if (SmokeInfo.x < -0.5) { smoke = float3(0.0, 1.0, 0.0); through = exp(-tau).xxx; }
	color.rgb = color.rgb * through + smoke * (1.0 - through);
	return color;
}

technique VolumetricSmoke {
	pass P0 {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 CompositePS();
		ZEnable = FALSE; ZWriteEnable = FALSE; AlphaBlendEnable = FALSE; AlphaTestEnable = FALSE;
		CullMode = NONE; ColorWriteEnable = 15;
	}
}

technique VolumetricSmokeEnv {
	pass P0 {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 EnvPS();
		ZEnable = FALSE; ZWriteEnable = FALSE; AlphaTestEnable = FALSE; StencilEnable = FALSE; CullMode = NONE; ColorWriteEnable = 15;
		AlphaBlendEnable = TRUE; SrcBlend = SRCALPHA; DestBlend = INVSRCALPHA; BlendOp = ADD;
	}
}

technique VolumetricSmokeHaze {
	pass P0 {
		VertexShader = compile vs_3_0 FrameVS();
		PixelShader = compile ps_3_0 HazePS();
		ZEnable = FALSE; ZWriteEnable = FALSE; AlphaBlendEnable = FALSE; AlphaTestEnable = FALSE; StencilEnable = FALSE;
		CullMode = NONE; ColorWriteEnable = 15;
	}
}

technique VolumetricSmokeSegments {
	pass P0 {
		VertexShader = compile vs_3_0 SegmentVS();
		PixelShader = compile ps_3_0 SegmentPS();
		ZEnable = FALSE; ZWriteEnable = FALSE; AlphaTestEnable = FALSE; StencilEnable = FALSE; CullMode = NONE;
		AlphaBlendEnable = TRUE; SrcBlend = ONE; DestBlend = ONE; BlendOp = ADD; ColorWriteEnable = 15;
	}
}
