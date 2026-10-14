float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SunAmbient;
float4 TESR_LightPosition[12];
float4 TESR_ShadowLightPosition[12];
float4 TESR_LightColor[24];
float4 TESR_BloodFlowFresh;
float4 TESR_BloodFlowDry;
float4 TESR_BloodFlowInfo;
float4 TESR_BloodFlowPool;
float4 TESR_BloodFlowProbe;
float4 TESR_BloodFlowFlow;
float4 TESR_HorizonColor;
float4 TESR_BloodFlowMatch;
float4 TESR_BloodFlowDryRatio;
float4 TESR_BloodFlowBeads;
float4 TESR_BloodFlowShape;
float4x4 TESR_ViewProjectionTransform;

sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_PointShadowBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BloodFlowPaths : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BloodFlowBounds : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BloodStreaks : register(s7) < string ResourceName = "Effects\BloodRuns.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler3D TESR_NoiseVolume : register(s8) < string ResourceName = "Effects\NoiseVolume.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; ADDRESSW = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloodSplatter : register(s9) < string ResourceName = "decals\fxbloodsplatterdecals01.dds"; > = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloodSpatterSmall : register(s10) < string ResourceName = "Effects\BloodSpatterSmall.dds"; > = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };
sampler2D TESR_BloodDrops : register(s11) < string ResourceName = "Effects\BloodDrops.dds"; > = sampler_state { ADDRESSU = WRAP; ADDRESSV = WRAP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = LINEAR; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"

static const float AssumedAlbedo = 0.3;
static const float Streams = 6.0;
static const float PathColumns = 16.0;
static const float LockFrames = 3.0;
static const float WallFlatZ = 0.94;
static const float GroundFlatZ = 0.985;
static const float SplatGroundFlatZ = 0.996;
static const float UnderZ = -0.3;
static const float ValleyPull = 2.5;
static const float Meander = 0.35;
static const float StreakAcross = 0.022;
static const float StreakAlong = 0.025;
static const float FlowPeriod = 3.0;
static const float CreepUnits = 2.5;

struct VSIN {
	float4 position : POSITION;
	float4 a : TEXCOORD0;
	float4 b : TEXCOORD1;
	float4 c : TEXCOORD2;
	float4 d : TEXCOORD3;
};

struct VSOUT {
	float4 position : POSITION;
	float4 a : TEXCOORD0;
	float4 b : TEXCOORD1;
	float4 c : TEXCOORD2;
	float4 d : TEXCOORD3;
};

struct PSIN {
	float4 a : TEXCOORD0;
	float4 b : TEXCOORD1;
	float4 c : TEXCOORD2;
	float4 d : TEXCOORD3;
	float2 vpos : VPOS;
};

VSOUT BoxVS(VSIN IN) {
	VSOUT OUT;
	OUT.position = IN.position;
	OUT.a = IN.a;
	OUT.b = IN.b;
	OUT.c = IN.c;
	OUT.d = IN.d;
	return OUT;
}

float ItemSeed(float cz) { return fmod(floor(cz + 0.5), 4096.0) / 4096.0; }

float hash1(float n) {
	return frac(sin(n) * 43758.5453);
}

float hash2(float2 p) {
	return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float valueNoise(float2 p) {
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = hash2(i);
	float b = hash2(i + float2(1, 0));
	float c = hash2(i + float2(0, 1));
	float d = hash2(i + float2(1, 1));
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float fbm(float2 p) {
	return 0.6 * valueNoise(p) + 0.3 * valueNoise(p * 2.13 + 5.2) + 0.1 * valueNoise(p * 4.71 + 1.7);
}

float2 FlowNoise(float3 p) { return tex3Dlod(TESR_NoiseVolume, float4(p / 8.0, 0.0)).ra; }

float Splatter(float2 local, float seed, float footprint) {
	float pick = floor(frac(seed * 13.7) * 3.0);
	float angle = seed * 37.0;
	float2 r = float2(local.x * cos(angle) - local.y * sin(angle), local.x * sin(angle) + local.y * cos(angle));
	float2 uv = (float2(fmod(pick, 2.0), floor(pick * 0.5)) + 0.5 + clamp(r, -0.48, 0.48)) * 0.5;
	return tex2Dlod(TESR_BloodSplatter, float4(uv, 0.0, log2(max(footprint * 1024.0, 1.0)))).a * step(abs(r.x), 0.48) * step(abs(r.y), 0.48);
}

float DecalCover(float3 rel, float4 b, float4 c, float4 d, float mip) {
	if (TESR_BloodFlowProbe.w < 0.5 || dot(d.xyz, d.xyz) < 1e-12) return -1.0;
	float code = floor(floor(c.z + 0.5) / 4096.0);
	float quarter = fmod(code, 5.0);
	float ratio = exp2(floor(code / 20.0) / 31.5 - 1.0);
	float3 v = cross(normalize(b.xyz), d.xyz) * ratio * (fmod(floor(code / 10.0), 2.0) > 0.5 ? -1.0 : 1.0);
	float2 uv = float2(dot(rel, d.xyz), dot(rel, v)) + 0.5;
	if (any(uv < 0.0) || any(uv > 1.0)) return 0.0;
	uv = quarter < 3.5 ? uv * 0.5 + float2(fmod(quarter, 2.0), floor(quarter * 0.5)) * 0.5 : uv;
	if (fmod(floor(code / 5.0), 2.0) > 0.5) uv.x = 1.0 - uv.x;
	return tex2Dlod(TESR_BloodSplatter, float4(uv, 0.0, mip)).a;
}

float SmallSplatter(float2 local, float seed, float footprint) {
	float pick = floor(frac(seed * 29.3) * 16.0);
	float angle = seed * 53.0;
	float2 r = float2(local.x * cos(angle) - local.y * sin(angle), local.x * sin(angle) + local.y * cos(angle));
	float2 uv = (float2(fmod(pick, 4.0), floor(pick * 0.25)) + 0.5 + clamp(r, -0.48, 0.48)) * 0.25;
	return tex2Dlod(TESR_BloodSpatterSmall, float4(uv, 0.0, log2(max(footprint * 256.0, 1.0)))).a * step(abs(r.x), 0.48) * step(abs(r.y), 0.48);
}

float RaggedCore(float2 d, float r, float seed) {
	float2 way = d / max(length(d), 1e-4);
	float edge = r * (0.65 + 0.7 * valueNoise(way * 1.7 + seed * 23.0));
	return saturate(1.0 - length(d) / max(edge, 1e-3)) * 0.5;
}

sampler2D BloodFlowBoundsVS : register(vs_3_0, s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

float Shows(float T, float dT) { return saturate((T - 1.0 + TESR_BloodFlowFlow.z) / max(dT, 0.02) + 0.5); }

void DecodeSlot(float w, out float slot, out float pool, out float fresh) {
	float code = floor(w + 0.5);
	fresh = code >= 2000.0 ? 1.0 : 0.0;
	code -= fresh * 2000.0;
	pool = code >= 1000.0 ? 1.0 : 0.0;
	slot = code - pool * 1000.0;
}

float4 PathTexel(float column, float row) {
	return tex2Dlod(TESR_BloodFlowPaths, float4((column + 0.5) / PathColumns, (row + 0.5) * TESR_BloodFlowProbe.x, 0.0, 0.0));
}

float4 TESR_BloodFlowSplat;

float SegmentDistance(float2 p, float2 a, float2 b, out float t) {
	float2 ab = b - a;
	t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
	return length(p - a - ab * t);
}

float SplatCoreRadius(float a, float seed, float grow) {
	float c = (0.45 + 0.18 * hash1(seed * 7.13)) * (0.6 + 0.4 * grow);
	return c * (1.0 + 0.26 * (valueNoise(float2(a * 1.4 + seed * 11.0, seed * 3.0)) - 0.5) * 2.0 + 0.07 * sin(a * 6.0 + seed * 19.0) + 0.05 * sin(a * 13.0 + seed * 7.0));
}

float SplatArms(float seed) { return 5.0 + floor(hash1(seed * 9.17) * 7.0); }

void SplatArm(float i, float seed, float grow, out float2 dir, out float rootR, out float len, out float width) {
	float slant = 0.3 + 0.7 * hash1(seed * 3.71);
	float lean = hash1(seed * 5.93) * 6.2832;
	float2 leanDir = float2(cos(lean), sin(lean));
	float h1 = hash1(seed * 13.7 + i * 7.31), h2 = hash1(seed * 29.3 + i * 3.17), h3 = hash1(seed * 41.9 + i * 5.53), h4 = hash1(seed * 57.1 + i * 2.29);
	dir = float2(cos(h1 * 6.2832), sin(h1 * 6.2832));
	float2 bent = lerp(dir, leanDir, slant * 0.6 * h4);
	dir = dot(bent, bent) > 1e-4 ? normalize(bent) : dir;
	float along = dot(dir, leanDir) * slant;
	rootR = SplatCoreRadius(atan2(dir.y, dir.x), seed, grow) * 0.7;
	len = (rootR + (0.3 + 0.9 * h2 * h2) * TESR_BloodFlowSplat.y * (1.0 + along * 0.7)) * grow;
	width = 0.05 + 0.1 * h3;
}

struct Stream {
	float active;
	float2 start;
	float lengthMax;
	float tau;
	float delay;
	float width;
	float dropVolume;
};

Stream SplatStream(float seed, float k, float R, float dripMax, float speed) {
	Stream s;
	float s1 = hash1(seed * 91.7 + k * 13.1);
	float s2 = hash1(seed * 47.3 + k * 7.7);
	float s3 = hash1(seed * 23.9 + k * 3.3);
	s.active = (s3 >= 0.3 && dripMax > 0.0) ? 1.0 : 0.0;
	float x0 = (s1 * 2.0 - 1.0) * 0.6 * R;
	s.start = float2(x0, R * (0.35 + 0.35 * s2) * sqrt(saturate(1.0 - sqr(x0 / R))));
	s.lengthMax = dripMax * (0.3 + 0.7 * s3) * saturate(R / 6.0 + 0.3);
	s.tau = (1.5 + 3.5 * s2) / speed;
	s.delay = (0.2 + 1.2 * s1) / speed;
	s.width = clamp(R * (0.05 + 0.05 * s1), 0.35, 2.0) * TESR_BloodFlowMatch.z;
	s.dropVolume = (2.5 + 2.5 * s2) * s.width;
	return s;
}

Stream PoolStream(float seed, float k, float R, float slope, float spreadTime) {
	Stream s;
	float s1 = hash1(seed * 71.3 + k * 17.9);
	float s2 = hash1(seed * 33.1 + k * 5.3);
	float s3 = hash1(seed * 12.7 + k * 9.1);
	s.active = 1.0;
	s.start = float2(0.0, 0.0);
	s.lengthMax = R * (0.8 + 2.2 * saturate(slope * 2.0)) * (0.6 + 0.4 * s3);
	s.tau = spreadTime * (0.8 + 0.6 * s2);
	s.delay = spreadTime * (0.25 + 0.2 * s1);
	s.width = clamp(R * 0.06 * (0.8 + 0.4 * s1), 0.8, 2.5) * TESR_BloodFlowMatch.z;
	s.dropVolume = (2.5 + 2.5 * s2) * s.width;
	return s;
}

float StreamRun(Stream s, float lengthMax, float age) {
	return lengthMax * (1.0 - exp(-max(age - s.delay, 0.0) / s.tau));
}

float StreamTime(Stream s, float lengthMax, float run) {
	return s.delay - s.tau * log(max(1.0 - run / lengthMax, 1e-4));
}

float PoolShift(float slope) { return 0.35 * saturate(slope * 2.0); }
float PoolStretch(float slope) { return 1.0 + 0.9 * saturate(slope * 2.0); }

float3 CameraRight() { return float3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]); }
float3 CameraUp() { return float3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]); }
float3 CameraForward() { return float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]); }

float3 ProjectRel(float3 rel) {
	float z = dot(rel, CameraForward());
	float2 ndc = float2(dot(rel, CameraRight()) * TESR_ProjectionTransform[0][0], dot(rel, CameraUp()) * TESR_ProjectionTransform[1][1]) / max(z, 0.001);
	return float3(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y, z);
}

bool SeeSurface(float3 Q, float stepSize, out float3 X, out float3 N, out float along, out float tol) {
	float3 s = ProjectRel(Q);
	X = Q;
	N = float3(0.0, 0.0, 1.0);
	along = 0.0;
	tol = 1.0;
	if (s.z < nearZ * 2.0 || s.x < 0.002 || s.y < 0.002 || s.x > 0.998 || s.y > 0.998) return false;
	float4 uv = float4(s.xy, 0.0, 0.0);
	float viewmodel = tex2Dlod(TESR_DepthBufferViewModel, uv).x;
	if ((TESR_DepthConstants.z == 0 && viewmodel < 0.9) || (TESR_DepthConstants.z > 0 && viewmodel > 0.01)) return false;
	float depth = readDepthLod(s.xy);
	float dist = length(Q);
	if (depth >= farZ * 0.98) { along = 1e6; return true; }
	X = toWorld(s.xy) * depth;
	N = normalize(GetWorldNormalLod(s.xy));
	along = (depth - s.z) * dist / s.z;
	float pixel = s.z * 2.0 * TESR_ReciprocalResolution.y / TESR_ProjectionTransform[1][1];
	tol = (0.75 + 0.5 * stepSize + 2.0 * pixel) / max(abs(dot(N, Q / dist)), 0.2);
	return true;
}

float Relief(float3 Q, float3 pos, float3 N, float limit) {
	float3 s = ProjectRel(Q);
	if (s.z < nearZ * 2.0 || s.x < 0.002 || s.y < 0.002 || s.x > 0.998 || s.y > 0.998) return 0.0;
	float depth = readDepthLod(s.xy);
	if (depth >= farZ * 0.98) return -limit;
	return clamp(dot(toWorld(s.xy) * depth - pos, N), -limit, limit);
}

float4 RunDown(float3 S, float3 N, float target, float h, float flatZ, float wiggleSeed, float width, float side, inout float valid, out float end) {
	float3 pos = S;
	float arc = 0.0;
	float going = 0.0;
	end = 0.0;
	[loop] for (int i = 0; i < 48; i++) {
		if (arc >= target - 0.001) break;
		if (N.z > flatZ) { end = 1.0; break; }
		if (N.z < UnderZ) { end = 2.0; break; }
		float stepSize = min(h, target - arc);
		float3 across = normalize(cross(N, float3(0.0, 0.0, -1.0)));
		float3 d = cross(across, N);
		float reach = width * 1.5 + 0.75;
		float limit = reach * 2.0 + stepSize;
		float hLeft = Relief(pos + (d * 0.5 + across) * reach, pos, N, limit);
		float hRight = Relief(pos + (d * 0.5 - across) * reach, pos, N, limit);
		float hAhead = 0.0;
		float3 Xa, Na;
		float alongA, tolA;
		if (SeeSurface(pos + d * (stepSize + width), stepSize, Xa, Na, alongA, tolA) && Na.z <= flatZ) hAhead = clamp(dot(Xa - pos, N), -limit, limit);
		float lean = clamp((hRight - hLeft) / reach * ValleyPull, -1.2, 1.2);
		float raised = saturate((hAhead - 0.35 * stepSize - 0.3) / (0.5 * stepSize + 0.5));
		if (raised > 0.3 && going == 0.0) going = abs(hRight - hLeft) > 0.2 ? sign(hRight - hLeft) : side;
		if (raised < 0.05) going = 0.0;
		float wander = (valueNoise(float2(arc * 0.07 + wiggleSeed, wiggleSeed * 3.1)) - 0.5) * 2.0 * Meander;
		d = normalize(d + across * (lean + going * raised * 2.5 + wander));
		float3 next = pos + d * stepSize;
		float3 X, N2;
		float along, tol;
		if (!SeeSurface(next, stepSize, X, N2, along, tol)) { valid = 0.0; pos = next; arc += stepSize; continue; }
		if (abs(along) <= tol) { pos = X; N = N2; arc += stepSize; continue; }
		if (along > 0.0) {
			float sz = stepSize;
			[unroll] for (int b = 0; b < 2; b++) {
				sz *= 0.5;
				float3 Xm, Nm;
				float am, tm;
				if (SeeSurface(pos + d * sz, sz, Xm, Nm, am, tm) && abs(am) <= tm) { pos = Xm; arc += sz; }
			}
			float3 Xw, Nw;
			float aw, tw;
			if (SeeSurface(pos - float3(0.0, 0.0, h), h, Xw, Nw, aw, tw) && abs(aw) <= tw && Nw.z > UnderZ && Nw.z < flatZ) {
				pos = Xw;
				N = Nw;
				arc += h;
				continue;
			}
			end = 2.0;
			break;
		}
		if (N2.z > 0.6 && X.z - next.z >= -tol && X.z - next.z <= stepSize + tol) {
			pos = float3(next.xy, X.z);
			arc += stepSize;
			end = 1.0;
			break;
		}
		valid = 0.0;
		pos = next;
		arc += stepSize;
	}
	return float4(pos, arc);
}

float FallFrom(float3 E, float maxFall, inout float valid) {
	float stepSize = max(maxFall / 24.0, 2.0);
	[loop] for (int j = 1; j <= 24; j++) {
		float s = j * stepSize;
		if (s > maxFall + 0.01) break;
		float3 F = E - float3(0.0, 0.0, s);
		float3 X, N;
		float along, tol;
		if (!SeeSurface(F, stepSize, X, N, along, tol)) { valid = 0.0; continue; }
		if (along > tol) continue;
		if (N.z > 0.5 && X.z >= F.z - tol && X.z <= F.z + stepSize + tol) return max(E.z - X.z, 0.0);
		valid = 0.0;
	}
	return -1.0;
}

float4 ProbePS(PSIN IN) : COLOR0 {
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	float column = floor(IN.vpos.x);
	float row = floor(IN.vpos.y);
	float k = row - slot * Streams;
	float4 previous = PathTexel(column, row);
	float4 previousHead = PathTexel(0.0, row);
	if (fresh < 0.5 && previousHead.x >= LockFrames) return previous;
	float seenBefore = fresh > 0.5 ? 0.0 : previousHead.x;

	float3 C = IN.a.xyz;
	float R = IN.b.w;
	float seed = ItemSeed(IN.c.z);
	float3 cs = ProjectRel(C);
	float pixel = cs.z * 2.0 * TESR_ReciprocalResolution.y / TESR_ProjectionTransform[1][1];
	float valid = pixel <= TESR_BloodFlowProbe.y ? 1.0 : 0.0;

	if (pool > 0.5 && k > Streams - 1.5) {
		float3 groundGuess = float3(0.0, 0.0, 1.0);
		float3 sumN = 0.0;
		float3 sumX = 0.0;
		float n = 0.0;
		[loop] for (int round = 0; round < 2; round++) {
			sumN = 0.0;
			sumX = 0.0;
			n = 0.0;
			[loop] for (int i = 0; i < 12; i++) {
				float angle = i * 0.5236;
				float2 off = float2(cos(angle), sin(angle)) * R * 1.2;
				float3 Q = C + float3(off, -(groundGuess.x * off.x + groundGuess.y * off.y) / groundGuess.z);
				float3 X, N;
				float along, tol;
				if (!SeeSurface(Q, 2.0, X, N, along, tol)) continue;
				if (N.z < 0.5 || abs(X.z - Q.z) > R * 0.6 + 8.0 || length(X.xy - Q.xy) > 6.0 + R * 0.6) continue;
				if (round > 0 && dot(N, groundGuess) < 0.97) continue;
				sumN += N;
				sumX += X;
				n += 1.0;
			}
			if (n > 0.0) groundGuess = normalize(sumN);
			groundGuess.z = max(groundGuess.z, 0.5);
		}
		if (n < 5.0) valid = 0.0;
		float3 groundN = n > 0.0 ? normalize(sumN) : float3(0.0, 0.0, 1.0);
		float3 middleSeen = n > 0.0 ? sumX / n : C;
		float heightAtCentre = middleSeen.z - (groundN.x * (C.x - middleSeen.x) + groundN.y * (C.y - middleSeen.y)) / max(groundN.z, 0.3);
		if (column < 0.5) return float4(valid > 0.5 ? seenBefore + 1.0 : 0.0, n, 0.0, -2.0);
		return float4(groundN, heightAtCentre - C.z);
	}

	Stream s;
	float3 S, N;
	float flatZ;
	float none = 0.0;
	if (pool > 0.5) {
		float groundRow = slot * Streams + Streams - 1.0;
		float4 groundHead = PathTexel(0.0, groundRow);
		float4 ground = PathTexel(1.0, groundRow);
		bool known = fresh < 0.5 && groundHead.y >= 5.0;
		bool groundKept = known && groundHead.x >= LockFrames;
		float slope = known ? length(ground.xy) / max(ground.z, 0.3) : 0.0;
		s = PoolStream(seed, k, R, slope, TESR_BloodFlowPool.x);
		flatZ = GroundFlatZ;
		if (k > 1.5) none = 1.0;
		else if (!known || slope < 0.12) none = groundKept ? 1.0 : 2.0;
		float2 downhill = slope > 0.0 ? normalize(ground.xy) : float2(1.0, 0.0);
		float turn = (k < 0.5 ? -0.5 : 0.5) + (hash1(seed * 19.3 + k) - 0.5) * 0.4;
		float2 way = float2(downhill.x * cos(turn) - downhill.y * sin(turn), downhill.x * sin(turn) + downhill.y * cos(turn));
		float2 offset = downhill * R * PoolShift(slope) + way * R * 0.9;
		S = C + float3(offset, ground.w - (ground.x * offset.x + ground.y * offset.y) / max(ground.z, 0.3));
		N = known ? ground.xyz : float3(0.0, 0.0, 1.0);
		if (!groundKept) valid = 0.0;
	}
	else {
		s = SplatStream(seed, k, R, IN.c.w, TESR_BloodFlowInfo.w);
		flatZ = WallFlatZ;
		float3 X0, N0;
		float along0, tol0;
		bool seen0 = SeeSurface(C, 2.0, X0, N0, along0, tol0) && abs(along0) <= R * 0.5 + 4.0;
		if (!seen0) valid = 0.0;
		float3 plane = seen0 ? N0 : normalize(IN.b.xyz);
		if (plane.z > WallFlatZ) flatZ = SplatGroundFlatZ;
		if (s.active < 0.5) none = 1.0;
		else if (plane.z > flatZ) none = seen0 ? 1.0 : 2.0;
		float3 down = float3(plane.x * plane.z, plane.y * plane.z, plane.z * plane.z - 1.0);
		down = length(down) > 0.01 ? normalize(down) : float3(0.0, 0.0, -1.0);
		float3 across = normalize(cross(plane, down));
		float3 base = seen0 ? X0 : C;
		[branch] if (TESR_BloodFlowSplat.x > 0.0 && k < SplatArms(seed)) {
			float2 dir;
			float rootR, len, width;
			SplatArm(k, seed, 1.0, dir, rootR, len, width);
			s.start = dir * (len + width * 0.5) * R;
			float thick = saturate((width - 0.05) / 0.1);
			if (hash1(seed * 57.1 + k * 2.29) > 0.35 + 0.6 * thick) none = 1.0;
			s.lengthMax *= lerp(0.5, 1.4, thick) * TESR_BloodFlowSplat.w;
		}
		else {
		if (hash1(seed * 7.31 + k * 3.17) > saturate(R / 10.0) + 0.2) none = 1.0;
		[branch] if (dot(IN.d.xyz, IN.d.xyz) > 1e-12 && TESR_BloodFlowProbe.w > 0.5) {
			float bestScore = -1.0;
			float bestCover = 0.0;
			[loop] for (int i = 0; i < 16; i++) {
				float2 cand = float2((hash1(seed * 53.1 + k * 11.3 + i * 1.71) * 2.0 - 1.0) * 0.8 * R,
					(hash1(seed * 29.7 + k * 5.9 + i * 2.37) * 1.1 - 0.25) * R);
				float cover = DecalCover(base - C + across * cand.x + down * cand.y, IN.b, IN.c, IN.d, 5.0);
				float score = cover + 0.3 * cand.y / R;
				if (cover > 0.35 && score > bestScore) { bestScore = score; bestCover = cover; s.start = cand; }
			}
			if (bestCover < 0.45) none = 1.0;
			s.lengthMax *= lerp(0.35, 1.4, saturate((bestCover - 0.45) / 0.5));
		}
		}
		S = base + across * s.start.x + down * s.start.y;
		N = plane;
	}
	if (none > 0.5) {
		if (column < 0.5) return float4(none < 1.5 ? LockFrames : 0.0, 0.0, 0.0, -1.0);
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	float3 X, N1;
	float along, tol;
	if (SeeSurface(S, 2.0, X, N1, along, tol) && abs(along) <= tol * 2.0) { S = X; N = N1; }
	else valid = 0.0;

	float h = max(s.lengthMax / 40.0, 0.6);
	float target = column < 1.5 ? s.lengthMax : (column - 2.0) * s.lengthMax / 13.0;
	float end;
	float side = s.start.x + (k - 0.5) * 0.001 < 0.0 ? -1.0 : 1.0;
	float4 reached = RunDown(S, N, target, h, flatZ, seed * 61.0 + k * 7.0, s.width, side, valid, end);
	if (column > 1.5) return float4(reached.xyz - C, reached.w);
	if (column > 0.5) return float4(reached.xyz - C, s.lengthMax);
	float fall = 0.0;
	if (end > 1.5) fall = FallFrom(reached.xyz, TESR_BloodFlowPool.z, valid);
	return float4(valid > 0.5 ? seenBefore + 1.0 : 0.0, reached.w, fall, end);
}

float4 BoundsPS(PSIN IN) : COLOR0 {
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	float column = floor(IN.vpos.x);
	float R = IN.b.w;
	float seed = ItemSeed(IN.c.z);
	float firstRow = slot * Streams;
	float3 lo, hi;
	float slope = 0.0;
	float count = Streams;
	if (pool > 0.5) {
		float4 groundHead = PathTexel(0.0, firstRow + Streams - 1.0);
		float4 ground = PathTexel(1.0, firstRow + Streams - 1.0);
		bool known = groundHead.y >= 5.0;
		float3 gN = known ? ground.xyz : float3(0.0, 0.0, 1.0);
		slope = length(gN.xy) / max(gN.z, 0.3);
		float reach = R * (1.45 * PoolStretch(slope) + PoolShift(slope)) + 2.0;
		float gz = known ? ground.w : 0.0;
		lo = float3(-reach, -reach, gz - reach * slope - 8.0 - 0.45 * reach);
		hi = float3(reach, reach, gz + reach * slope + 5.0 + 0.25 * reach);
		count = 2.0;
	}
	else {
		lo = -(R * 1.1 + 1.0);
		hi = R * 1.1 + 1.0;
	}
	[loop] for (float k = 0.0; k < count; k += 1.0) {
		float row = firstRow + k;
		float4 head = PathTexel(0.0, row);
		if (head.w < -0.5) continue;
		float4 last = PathTexel(1.0, row);
		float3 first = PathTexel(2.0, row).xyz;
		Stream s;
		if (pool > 0.5) s = PoolStream(seed, k, R, slope, TESR_BloodFlowPool.x);
		else s = SplatStream(seed, k, R, IN.c.w, TESR_BloodFlowInfo.w);
		float3 chord = last.xyz - first;
		float stray = 0.5 * sqrt(max(head.y * head.y - dot(chord, chord), 0.0));
		float grow = stray + s.width * 4.0 + 3.0 + sqrt(max(last.w - head.y, 0.0) * s.width);
		lo = min(lo, min(first, last.xyz) - grow);
		hi = max(hi, max(first, last.xyz) + grow);
		if (head.w > 1.5) {
			float3 L = last.xyz - float3(0.0, 0.0, head.z > 0.0 ? head.z : TESR_BloodFlowPool.z);
			float spread = s.width * 10.0 + 3.0;
			lo = min(lo, L - spread);
			hi = max(hi, float3(L.xy + spread, L.z + 3.0));
		}
	}
	return column < 0.5 ? float4(lo, 1.0) : float4(hi, 1.0);
}

struct Paint {
	float cover;
	float thick;
	float3 bend;
	float air;
	float debug;
};

void Add(inout Paint p, float c, float thick, float3 bend, float debug) {
	if (c > p.cover) {
		p.thick = thick;
		p.bend = bend;
		p.debug = debug;
		p.cover = c;
	}
}

float RaySegment(float3 ro, float3 rd, float3 a, float3 b, out float t) {
	float3 ab = b - a;
	float3 w0 = ro - a;
	float bb = dot(rd, ab);
	float cc = dot(ab, ab);
	float dd = dot(rd, w0);
	float ee = dot(ab, w0);
	float denom = cc - bb * bb;
	float u = denom > 1e-6 ? saturate((ee - bb * dd) / denom) : 0.0;
	t = dot(a + ab * u - ro, rd);
	return length(ro + rd * t - (a + ab * u));
}

struct Nearest {
	float row;
	float best;
	float arc;
	float side;
	float score;
};

void StreamState(float row, inout Stream s, float age, out float ground, out float shown, out float excess, out float lengthMax) {
	float4 head = PathTexel(0.0, row);
	float4 last = PathTexel(1.0, row);
	lengthMax = max(last.w, 0.01);
	float amount = clamp(lengthMax / max(s.lengthMax, 0.01), 0.35, 1.4);
	s.width *= pow(amount, 0.45);
	s.delay *= pow(amount, -0.7);
	s.dropVolume *= pow(amount, 0.45);
	float3 chord = last.xyz - PathTexel(2.0, row).xyz;
	ground = 1.0 - smoothstep(0.45, 0.75, abs(chord.z) / max(length(chord), 1e-3));
	s.tau *= lerp(1.0, 3.0, ground);
	s.delay *= lerp(1.0, 2.0, ground);
	s.width = lerp(s.width, min(s.width * lerp(1.8, 3.2, saturate((amount - 0.35) / 1.05)), 5.0), ground);
	float run = StreamRun(s, lengthMax, age);
	shown = min(run, head.y);
	excess = max(run - head.y, 0.0);
}

float3 StreamTip(float row, float shown, float lengthMax) {
	float f = min(shown / (lengthMax / 13.0), 13.0);
	float4 P0 = PathTexel(2.0 + floor(f), row);
	float4 P1 = PathTexel(2.0 + min(floor(f) + 1.0, 13.0), row);
	return lerp(P0.xyz, P1.xyz, saturate((shown - P0.w) / max(P1.w - P0.w, 1e-4)));
}

void FindPath(float row, Stream s, float3 pp, float3 n, float aa, float age, float seed, inout Nearest nr, inout Paint p) {
	float4 head = PathTexel(0.0, row);
	if (head.w < -0.5) return;
	float ground, shown, excess, lengthMax;
	StreamState(row, s, age, ground, shown, excess, lengthMax);
	if (shown + excess < 0.05) return;
	float3 first = PathTexel(2.0, row).xyz;
	float3 chord = PathTexel(1.0, row).xyz - first;
	float chordLength2 = dot(chord, chord);
	float3 toLine = pp - (first + chord * saturate(dot(pp - first, chord) / max(chordLength2, 1e-4)));
	float stray = 0.5 * sqrt(max(head.y * head.y - chordLength2, 0.0));
	if (length(toLine) > stray + s.width * 3.0 + 2.0 + sqrt(excess * s.width) + aa) return;
	float segments = min(ceil(shown * 13.0 / lengthMax - 0.001), 13.0);
	float best = 1e9;
	float bestArc = 0.0;
	float bestSide = 1.0;
	float4 A = PathTexel(2.0, row);
	[loop] for (float j = 0.0; j < segments; j += 1.0) {
		float4 B = PathTexel(j + 3.0, row);
		float3 ab = B.xyz - A.xyz;
		float l2 = dot(ab, ab);
		float most = saturate((shown - A.w) / max(B.w - A.w, 1e-4));
		float t = l2 > 1e-6 ? min(saturate(dot(pp - A.xyz, ab) / l2), most) : 0.0;
		float3 off = pp - (A.xyz + ab * t);
		float dist = length(off);
		if (dist < best) { best = dist; bestArc = lerp(A.w, B.w, t); bestSide = dot(cross(ab, off), n); }
		A = B;
	}
	if (best - s.width * 1.5 < nr.score) {
		nr.row = row;
		nr.best = best;
		nr.arc = bestArc;
		nr.side = bestSide;
		nr.score = best - s.width * 1.5;
	}
	[branch] if (excess > 0.0) {
		float3 toTip = pp - StreamTip(row, shown, lengthMax);
		[branch] if (head.w < 1.5) {
			float rp = s.width * 1.2 + sqrt(excess * s.width * lerp(0.9, 0.3, ground));
			float rag = FlowNoise(float3(toTip.xy / max(rp, 0.5) * 1.3, seed * 23.0 + row)).x - 0.5;
			float T = 1.0 - dot(toTip.xy, toTip.xy) / (rp * rp) + rag * (0.5 + 0.5 * TESR_BloodFlowFlow.y) * lerp(0.6, 0.9, ground);
			[branch] if (TESR_BloodFlowProbe.w > 0.5)
				T = Splatter(toTip.xy / (rp * 2.6), seed + row * 0.137, aa / (rp * 2.6)) * 1.25 - 0.1 + RaggedCore(toTip.xy, rp * 0.5, seed + row * 0.37);
			T += clamp(-toTip.z / max(rp * 0.25, 0.4), -1.5, 1.0) * 0.45;
			Add(p, Shows(T, aa * 2.0 / rp) * step(abs(toTip.z), rp + 2.0), lerp(0.55 + 0.35 * saturate(T), 0.3, ground), float3(toTip.xy / rp, 0.0) * saturate(1.0 - 3.0 * T) * 0.4 * (1.0 - ground), 2.0);
		}
		else {
			float rh = s.width * (1.0 + 0.9 * pow(frac(excess / s.dropVolume), 0.333));
			float3 off = toTip + float3(0.0, 0.0, rh * 0.5);
			Add(p, saturate(0.5 - (length(off) - rh) / aa), 0.9, off / rh, 3.0);
		}
	}
}

void PaintStream(Nearest nr, Stream s, float3 pp, float3 n, float aa, float age, float seed, inout Paint p) {
	[branch] if (nr.row < 0.0) return;
	float row = nr.row;
	float ground, shown, excess, lengthMax;
	StreamState(row, s, age, ground, shown, excess, lengthMax);
	float3 way = PathTexel(1.0, row).xyz - PathTexel(2.0, row).xyz;
	way *= rsqrt(max(dot(way, way), 1e-6));
	float along = saturate(nr.arc / max(shown, 0.01));
	float back = nr.arc < shown - 0.01 ? shown - nr.arc : -dot(pp - StreamTip(row, shown, lengthMax), way);
	float bead = saturate(1.0 - back / (3.0 * s.width)) * (excess <= 0.0 ? 1.0 : 0.4);
	float w = max(s.width * lerp(0.6, 1.0, along) * lerp(1.0, 0.85 + 0.15 * along, ground) * (1.0 + lerp(0.45, 0.6, ground) * bead), 0.05);
	float3 side = normalize(cross(n, way));
	float u = (nr.side < 0.0 ? -nr.best : nr.best) / w;
	float2 dn = FlowNoise(float3(u * 0.6, nr.arc * 0.3 - TESR_BloodFlowFlow.w * TESR_BloodFlowFlow.x * 0.1, seed * 37.0 + row * 0.37)) - 0.5;
	float phase = frac(TESR_BloodFlowFlow.w / FlowPeriod + seed);
	float2 drift = float2(dn.y * TESR_BloodFlowFlow.y * StreakAcross * 0.5,
		-CreepUnits * FlowPeriod * StreakAlong * TESR_BloodFlowFlow.x * max(1.0 + 1.6 * TESR_BloodFlowFlow.y * dn.x, 0.1) * lerp(1.0, 0.4, ground));
	float4 st = float4(u * 0.5 * StreakAcross + frac(seed * 7.3 + row * 0.29), nr.arc * StreakAlong, 0.0, max(log2(aa * StreakAlong * 1024.0), 0.0));
	float4 t = lerp(tex2Dlod(TESR_BloodStreaks, st + float4(drift * phase, 0.0, 0.0)),
		tex2Dlod(TESR_BloodStreaks, st + float4(drift * frac(phase + 0.5) + 0.5, 0.0, 0.0)), abs(1.0 - 2.0 * phase)).gbar;
	float T = 1.0 - u * u * (1.0 - 0.6 * TESR_BloodFlowFlow.y * dn.y) + (t.a - 0.3) * 0.5 + dn.x * TESR_BloodFlowFlow.y * 0.7 * (1.0 + bead);
	Add(p, Shows(T, aa * 2.0 / w), saturate(T) * lerp(lerp(0.35, 0.9, bead), lerp(0.4, 0.75, bead), ground),
		(side * (u * 0.6 + (t.x * 2.0 - 1.0) * 0.5 * t.a) + way * (saturate(-back / w) * 0.5 - (t.y * 2.0 - 1.0) * 0.5 * t.a)) * lerp(1.0, 0.5, ground), 1.0);
	[branch] if (ground > 0.01) {
		float soak = s.width * 2.6 + 0.6;
		Add(p, saturate(1.0 - nr.best / soak) * 0.3 * ground * (1.0 - 0.5 * along), 0.0, 0.0, 1.0);
	}
}

void PaintDrops(float row, Stream s, float3 pp, float3 ro, float3 rd, float sceneDist, float aa, float age, float seed, inout Paint p) {
	float4 head = PathTexel(0.0, row);
	if (head.w < 1.5) return;
	float4 last = PathTexel(1.0, row);
	float lengthMax = max(last.w, 0.01);
	float excess = StreamRun(s, lengthMax, age) - head.y;
	if (excess < s.dropVolume) return;
	float3 E = last.xyz;
	float fallLength = head.z > 0.0 ? head.z : TESR_BloodFlowPool.z;
	float tColumn;
	float nearColumn = RaySegment(ro, rd, E - float3(0.0, 0.0, fallLength), E, tColumn) - s.width * 1.5 - aa * tColumn / sceneDist;
	float nearStain = length(pp - E + float3(0.0, 0.0, fallLength)) - s.width * 10.0;
	if (nearColumn > 0.0 && nearStain > 0.0) return;
	float g = TESR_BloodFlowPool.w;
	float fallTime = sqrt(2.0 * fallLength / g);
	float fallen = floor(excess / s.dropVolume);
	float radius = s.width * 1.15;
	[loop] for (float m = 0.0; m < 2.0; m += 1.0) {
		float since = age - StreamTime(s, lengthMax, head.y + (fallen - m) * s.dropVolume);
		if (fallen - m < 1.0 || since < 0.0 || since > fallTime) continue;
		float3 bottom = E - float3(0.0, 0.0, 0.5 * g * since * since + radius);
		float t;
		float dist = RaySegment(ro, rd, bottom, bottom + float3(0.0, 0.0, min(g * since * 0.02, 8.0)), t);
		if (t > 0.0 && t < sceneDist - 0.5) p.air = max(p.air, saturate(0.5 - (dist - radius) / max(aa * t / sceneDist, 0.02)));
	}
	float landed = floor(max(StreamRun(s, lengthMax, age - fallTime) - head.y, 0.0) / s.dropVolume);
	[branch] if (head.z > 0.0 && landed >= 1.0) {
		float3 off = pp - E + float3(0.0, 0.0, head.z);
		float rl = s.width * (1.2 + 0.9 * sqrt(landed));
		float c;
		[branch] if (TESR_BloodFlowProbe.w > 0.5) {
			float T = SmallSplatter(off.xy / (rl * 3.2), seed + row * 0.29, aa / (rl * 3.2)) * 1.25 - 0.1 + saturate(1.0 - length(off.xy) / (rl * 0.35)) * 0.5;
			c = Shows(T, aa * 2.0 / rl);
		}
		else {
			float edge = rl * (0.75 + 0.5 * valueNoise(float2(atan2(off.y, off.x) * 2.0 + seed * 40.0, row)));
			c = saturate(0.5 - (length(off) - edge) / aa);
			float cellSize = s.width * 1.5;
			float2 inCell = frac(off.xy / cellSize) - 0.5;
			float spot = hash2(floor(off.xy / cellSize) + seed * 13.0) > 0.8 ? saturate(0.5 - (length(inCell) * cellSize - s.width * 0.35) / aa) : 0.0;
			c = max(c, spot * step(length(off), rl * 2.2) * saturate(landed / 3.0));
		}
		Add(p, c * step(abs(off.z), 3.0), 0.55, float3(0.0, 0.0, 0.0), 4.0);
	}
}

float Glint(float3 N, float3 V, float3 L, float a2) {
	float3 H = normalize(L + V);
	float nh = saturate(dot(N, H));
	float lh = saturate(dot(L, H));
	float d = nh * nh * (a2 - 1.0) + 1.0;
	return a2 / (3.14159 * d * d) * (0.02 + 0.98 * pow(1.0 - lh, 5.0)) * 0.25 / max(lh * lh, 0.1) * saturate(dot(N, L));
}

float LampGlint(float3 N, float3 V, float3 P, float4 lamp, float3 color, float a2) {
	if (lamp.w <= 0.0) return 0.0;
	float3 toLamp = lamp.xyz - P;
	float s = saturate(dot(toLamp, toLamp) / (lamp.w * lamp.w));
	if (s >= 1.0) return 0.0;
	float atten = (1.0 - s) * (1.0 - s) / (1.0 + 5.0 * s);
	return Glint(N, V, normalize(toLamp), a2) * atten * saturate(luma(color));
}

float4 BloodPS(PSIN IN) : COLOR0 {
	float2 uv = (IN.vpos + 0.5) * TESR_ReciprocalResolution.xy;
	float viewmodelDepth = tex2D(TESR_DepthBufferViewModel, uv).x;
	if ((TESR_DepthConstants.z == 0 && viewmodelDepth < 0.9) || (TESR_DepthConstants.z > 0 && viewmodelDepth > 0.01)) discard;
	float depth = min(readDepth(uv), farZ * 0.98);
	bool sky = depth >= farZ * 0.979;

	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	float3 rel = toWorld(uv) * depth;
	float sceneDist = length(rel);
	float3 rd = rel / sceneDist;
	float3 pp = sky ? float3(1e5, 1e5, 1e5) : rel - IN.a.xyz;
	float seed = ItemSeed(IN.c.z);
	float aa = max(depth * TESR_ReciprocalResolution.y * 2.0 / TESR_ProjectionTransform[1][1], 0.05);
	float firstRow = slot * Streams;

	float boundsV = (firstRow + Streams * 0.5) * TESR_BloodFlowProbe.x;
	float4 lo = tex2Dlod(TESR_BloodFlowBounds, float4(0.25, boundsV, 0.0, 0.0));
	float4 hi = tex2Dlod(TESR_BloodFlowBounds, float4(0.75, boundsV, 0.0, 0.0));
	[branch] if (lo.w > 0.5) {
		float3 margin = aa + 0.5;
		bool inside = all(pp >= lo.xyz - margin) && all(pp <= hi.xyz + margin);
		float3 inverse = 1.0 / (abs(rd) > 1e-6 ? rd : 1e-6);
		float3 t0 = (lo.xyz - margin + IN.a.xyz) * inverse;
		float3 t1 = (hi.xyz + margin + IN.a.xyz) * inverse;
		float3 tMin = min(t0, t1);
		float3 tMax = max(t0, t1);
		float tNear = max(max(tMin.x, tMin.y), tMin.z);
		float tFar = min(min(tMax.x, tMax.y), tMax.z);
		if (!inside && !(tNear <= tFar && tFar > 0.0 && tNear < sceneDist)) discard;
	}

	Paint p;
	p.cover = 0.0;
	p.thick = 0.0;
	p.bend = 0.0;
	p.air = 0.0;
	p.debug = 0.0;
	float rim = 0.0;

	[branch] if (pool > 0.5) {
		float4 groundHead = PathTexel(0.0, firstRow + Streams - 1.0);
		float4 ground = PathTexel(1.0, firstRow + Streams - 1.0);
		bool known = fresh < 0.5 && groundHead.y >= 5.0;
		float3 gN = known ? ground.xyz : float3(0.0, 0.0, 1.0);
		float slope = length(gN.xy) / max(gN.z, 0.3);
		float2 downhill = slope > 1e-3 ? normalize(gN.xy) : float2(1.0, 0.0);
		float R = IN.b.w;
		float grow = sqrt(saturate(1.0 - exp(-IN.c.x * 3.0 / TESR_BloodFlowPool.x)));
		float radius = R * grow;
		float2 q = pp.xy - downhill * R * PoolShift(slope) * grow;
		float alongQ = dot(q, downhill);
		float2 shaped = float2(alongQ / (alongQ > 0.0 ? PoolStretch(slope) : 1.0), dot(q, float2(-downhill.y, downhill.x)));
		float fromCentre = length(shaped);
		float groundZ = (known ? ground.w : 0.0) - (gN.x * pp.x + gN.y * pp.y) / max(gN.z, 0.3);
		bool onGround = fromCentre < R * 1.45 && pp.z < groundZ + 5.0 + 0.15 * fromCentre && pp.z > groundZ - (8.0 + 0.3 * fromCentre);
		[branch] if (onGround && radius > 0.05 && normalize(GetWorldNormalLod(uv)).z > 0.6) {
			float angle = seed * 6.2832;
			float2 lobeWay = float2(cos(angle), sin(angle));
			float dMain = fromCentre - radius;
			float dLobe = length(shaped - lobeWay * radius * 0.55) - radius * 0.6;
			float kk = radius * 0.3 + 0.01;
			float mixLobe = saturate(0.5 + 0.5 * (dLobe - dMain) / kk);
			float dist = lerp(dLobe, dMain, mixLobe) - kk * mixLobe * (1.0 - mixLobe);
			dist += (fbm(pp.xy * 0.09 + seed * 17.0) - 0.5) * radius * 0.55 * (0.5 + 0.5 * TESR_BloodFlowFlow.y);
			dist += clamp(pp.z - groundZ, -radius * 0.4, radius * 0.4) * 0.8;
			float T = 0.25 - dist / max(radius, 0.5) * 0.9;
			[branch] if (TESR_BloodFlowProbe.w > 0.5)
				T = Splatter(shaped / (radius * 2.2), seed, aa / (radius * 2.2)) * 1.25 - 0.1 + RaggedCore(shaped, radius * 0.55, seed)
					- clamp(pp.z - groundZ, -radius * 0.4, radius * 0.4) * 0.72 / max(radius, 0.5);
			float cover = Shows(T, aa * 0.9 / max(radius, 0.5));
			float inward = (T - 1.0 + TESR_BloodFlowFlow.z) * max(radius, 0.5) / 0.9;
			rim = saturate(1.0 - inward / max(radius * 0.12, 0.8)) * cover;
			float deep = saturate(inward / max(radius * 0.5, 0.5));
			float2 outward = length(pp.xy) > 0.001 ? normalize(pp.xy) : float2(0.0, 0.0);
			Add(p, cover, 0.35 + 0.65 * deep, float3(outward * rim * rim * 0.3, 0.0), 2.0);
		}
		Nearest nr;
		nr.row = -1.0; nr.best = 1e9; nr.arc = 0.0; nr.side = 1.0; nr.score = 1e9;
		[loop] for (float k = 0.0; k < 2.0; k += 1.0)
			FindPath(firstRow + k, PoolStream(seed, k, IN.b.w, slope, TESR_BloodFlowPool.x), pp, float3(gN.xy, max(gN.z, 0.3)), aa, IN.c.x, seed, nr, p);
		PaintStream(nr, PoolStream(seed, nr.row - firstRow, IN.b.w, slope, TESR_BloodFlowPool.x), pp, float3(gN.xy, max(gN.z, 0.3)), aa, IN.c.x, seed, p);
		[loop] for (float k2 = 0.0; k2 < 2.0; k2 += 1.0)
			PaintDrops(firstRow + k2, PoolStream(seed, k2, IN.b.w, slope, TESR_BloodFlowPool.x), pp, -IN.a.xyz, rd, sceneDist, aa, IN.c.x, seed, p);
	}
	else {
		Nearest nr;
		nr.row = -1.0; nr.best = 1e9; nr.arc = 0.0; nr.side = 1.0; nr.score = 1e9;
		[loop] for (float k = 0.0; k < Streams; k += 1.0)
			FindPath(firstRow + k, SplatStream(seed, k, IN.b.w, IN.c.w, TESR_BloodFlowInfo.w), pp, IN.b.xyz, aa, IN.c.x, seed, nr, p);
		PaintStream(nr, SplatStream(seed, nr.row - firstRow, IN.b.w, IN.c.w, TESR_BloodFlowInfo.w), pp, IN.b.xyz, aa, IN.c.x, seed, p);
		[loop] for (float k2 = 0.0; k2 < Streams; k2 += 1.0)
			PaintDrops(firstRow + k2, SplatStream(seed, k2, IN.b.w, IN.c.w, TESR_BloodFlowInfo.w), pp, -IN.a.xyz, rd, sceneDist, aa, IN.c.x, seed, p);
	}

	float4 scene = tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0, 0.0));
	float splat = 0.0;
	if (pool < 0.5) {
		float inside = 1.0 - smoothstep(0.7, 1.05, length(pp) / IN.b.w);
		float redness = saturate((scene.r - max(scene.g, scene.b)) / max(scene.r, 0.02) * 1.6 - 0.35);
		splat = inside * redness;
		p.cover *= 1.0 - 0.6 * splat * step(p.thick, 0.5);
	}

	if (TESR_BloodFlowInfo.y > 0.5) {
		if (p.air > 0.5) return float4(1, 1, 1, 0.9);
		if (p.cover > 0.5) {
			float3 c = p.debug < 1.5 ? float3(1, 0, 0) : p.debug < 2.5 ? float3(1, 0.9, 0) : p.debug < 3.5 ? float3(0, 0.3, 1) : float3(0, 1, 1);
			return float4(c, 0.85);
		}
		if (pp.x < 9e4 && frac(length(pp) / 10.0) < 0.06) return float4(1, 1, 1, 0.6);
		return float4(0, 0.6, 0, 0.35);
	}
	float amount = saturate(max(max(p.cover, splat), p.air));
	if (amount * IN.c.y < 0.004) discard;

	float3 sceneLin = linearize(max(scene.rgb, 0.0));
	float lit = 0.0;
	if (TESR_BloodFlowInfo.x > 0.5) {
		float sunAmbient = max(luma(linearize(TESR_SunAmbient.rgb)), 0.001);
		float4 shadows = tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0.0, 0.0));
		lit = saturate(pow(abs((shadows.r + shadows.g) / sunAmbient), 5.0));
	}
	float irradiance = luma(linearize(saturate(TESR_SunAmbient.rgb)))
		+ lit * luma(linearize(saturate(TESR_SunColor.rgb))) * saturate(dot(normalize(GetWorldNormalLod(uv)), normalize(TESR_SunDirection.xyz)));
	float light = min(max(max(sceneLin.r, max(sceneLin.g, sceneLin.b)) / AssumedAlbedo, irradiance), 2.5);
	float age = IN.c.x;
	float dryTime = pool > 0.5 ? TESR_BloodFlowInfo.z : TESR_BloodFlowDry.w;
	float dry = smoothstep(0.0, 1.0, age / (dryTime * lerp(0.5, 1.6, p.thick)));
	float3 freshColour = TESR_BloodFlowFresh.rgb;
	float3 dryColour = TESR_BloodFlowDry.rgb;
	[branch] if (TESR_BloodFlowMatch.w > 0.5 && IN.d.w > 0.5) {
		float t = floor(IN.d.w + 0.5);
		float r = floor(t / 65536.0);
		r += (t - r * 65536.0 >= 65536.0) ? 1.0 : ((t - r * 65536.0 < 0.0) ? -1.0 : 0.0);
		float gb = t - r * 65536.0;
		float g = floor(gb / 256.0);
		g += (gb - g * 256.0 >= 256.0) ? 1.0 : ((gb - g * 256.0 < 0.0) ? -1.0 : 0.0);
		freshColour = linearize(saturate(float3(r, g, gb - g * 256.0) / 255.0 * TESR_BloodFlowMatch.x)) * TESR_BloodFlowMatch.y;
		dryColour = freshColour * float3(0.72, 0.8, 0.8) + freshColour.r * float3(0.0, 0.1, 0.05);
	}
	float3 albedo = lerp(freshColour, dryColour, dry);
	albedo *= lerp(1.0, 0.8 + 0.4 * valueNoise((pp.xy + pp.z) * 0.35 + seed * 31.0), dry);
	float3 through = exp(-(1.2 + log(max(albedo.r, 0.001) / max(albedo, 0.001))) * (0.25 + 2.0 * p.thick));
	float3 blood = (sceneLin * through + light * albedo * lerp(1.0, 0.6, saturate(p.thick * 1.3 - 0.3)) * (1.0 - through)) * (1.0 - 0.35 * dry * rim);
	blood += light * albedo * (pow(saturate(dot(rd, normalize(TESR_SunDirection.xyz))), 4.0) * lit * (1.0 - dry) * (1.0 - saturate(p.thick)) * 0.8);
	float3 colour = sceneLin;
	colour = lerp(colour, colour * lerp(1.0, float3(0.5, 0.62, 0.62), dry), splat);
	colour = lerp(colour, blood, p.cover);

	float wet = max(p.cover, splat * 0.12);
	float gloss = TESR_BloodFlowFresh.w * (1.0 - dry) * wet * lerp(0.5, 1.0, p.thick);
	[branch] if (gloss > 0.001) {
		float3 V = -rd;
		float3 Nb = normalize(normalize(GetWorldNormalLod(uv)) + p.bend * 0.8);
		float rough = lerp(0.12, 0.45, dry);
		float a2 = rough * rough * rough * rough;
		float glints = 0.0;
		if (TESR_BloodFlowInfo.x > 0.5) glints += Glint(Nb, V, normalize(TESR_SunDirection.xyz), a2) * lit * saturate(luma(linearize(TESR_SunColor.rgb)));
		float3 P = TESR_CameraPosition.xyz + rd * sceneDist;
		[loop] for (int i = 0; i < 12; i++) {
			glints += LampGlint(Nb, V, P, TESR_ShadowLightPosition[i], TESR_LightColor[i].rgb, a2);
			glints += LampGlint(Nb, V, P, TESR_LightPosition[i], TESR_LightColor[i + 12].rgb, a2);
		}
		float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(Nb, V)), 5.0);
		float3 surroundings = TESR_BloodFlowInfo.x > 0.5 ? linearize(saturate(TESR_HorizonColor.rgb)) : light * 0.1;
		colour += (min(glints, 1.0) * 0.4 * min(light, 1.2) + fresnel * surroundings * (1.0 - rough) * 0.5) * gloss;
	}
	float3 drop = light * (freshColour * 0.7 + float3(0.02, 0.004, 0.004) * TESR_BloodFlowFresh.w);
	colour = lerp(colour, drop, p.air);
	float ceiling = max(max(sceneLin.r, max(sceneLin.g, sceneLin.b)), 1.0);
	colour = min(colour, ceiling);

	float alpha = amount * IN.c.y;
	float3 full = (colour - sceneLin * (1.0 - amount)) / max(amount, 0.004);
	full = max(full, 0.0);
	full *= ceiling / max(max(full.r, max(full.g, full.b)), ceiling);
	return float4(delinearize(full), alpha);
}

VSOUT CutBox(VSIN IN, float limit) {
	VSOUT OUT;
	OUT.a = IN.a;
	OUT.b = IN.b;
	OUT.c = IN.c;
	OUT.d = IN.d;
	float3 corner = IN.position.xyz;
	float margin = 1.5 + 0.004 * length(IN.a.xyz);
	if (TESR_BloodFlowShape.w > 0.5) {
		float slot, pool, fresh;
		DecodeSlot(IN.a.w, slot, pool, fresh);
		float boundsV = (slot * Streams + Streams * 0.5) * TESR_BloodFlowProbe.x;
		float4 lo = tex2Dlod(BloodFlowBoundsVS, float4(0.25, boundsV, 0.0, 0.0));
		float4 hi = tex2Dlod(BloodFlowBoundsVS, float4(0.75, boundsV, 0.0, 0.0));
		if (lo.w > 0.5 && hi.w > 0.5) corner = clamp(corner, lo.xyz - margin, hi.xyz + margin);
	}
	corner = clamp(corner, -limit - margin, limit + margin);
	float3 rel = IN.a.xyz + corner;
	float face = floor(IN.position.w + 0.5);
	float axis = floor(face * 0.5);
	float side = face - axis * 2.0 > 0.5 ? 1.0 : -1.0;
	float plane = axis < 0.5 ? rel.x : (axis < 1.5 ? rel.y : rel.z);
	OUT.position = side * plane > 0.0 ? mul(float4(rel + TESR_BloodFlowShape.xyz, 1.0), TESR_ViewProjectionTransform) : float4(0.0, 0.0, -2.0, 1.0);
	return OUT;
}

VSOUT PaintVS(VSIN IN) { return CutBox(IN, 1e9); }
VSOUT BeadsVS(VSIN IN) { return CutBox(IN, IN.b.w * 1.1 + 1.0); }

technique BloodFlow
{
	pass
	{
		VertexShader = compile vs_3_0 PaintVS();
		PixelShader = compile ps_3_0 BloodPS();
		AlphaBlendEnable = true;
		SrcBlend = SrcAlpha;
		DestBlend = InvSrcAlpha;
		BlendOp = Add;
		SeparateAlphaBlendEnable = false;
		AlphaTestEnable = false;
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		CullMode = None;
		ScissorTestEnable = false;
		ColorWriteEnable = 7;
	}
}

float4 BeadsPS(PSIN IN) : COLOR0 {
	float2 uv = (IN.vpos + 0.5) * TESR_ReciprocalResolution.xy;
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	if (pool > 0.5 || TESR_BloodFlowBeads.x <= 0.0 || TESR_BloodFlowProbe.w < 0.5 || dot(IN.d.xyz, IN.d.xyz) < 1e-12) discard;
	float viewmodelDepth = tex2D(TESR_DepthBufferViewModel, uv).x;
	if ((TESR_DepthConstants.z == 0 && viewmodelDepth < 0.9) || (TESR_DepthConstants.z > 0 && viewmodelDepth > 0.01)) discard;
	float depth = readDepth(uv);
	if (depth >= farZ * 0.979) discard;
	float3 rel = toWorld(uv) * depth;
	float3 pp = rel - IN.a.xyz;
	float3 n = normalize(IN.b.xyz);
	if (abs(dot(pp, n)) > IN.b.w * 0.5 + 4.0) discard;
	float code = floor(floor(IN.c.z + 0.5) / 4096.0);
	float quarter = fmod(code, 5.0);
	float ratio = exp2(floor(code / 20.0) / 31.5 - 1.0);
	float3 vAxis = cross(n, IN.d.xyz) * ratio * (fmod(floor(code / 10.0), 2.0) > 0.5 ? -1.0 : 1.0);
	float2 local = float2(dot(pp, IN.d.xyz), dot(pp, vAxis)) + 0.5;
	if (any(local < 0.0) || any(local > 1.0)) discard;
	float2 atlas = quarter < 3.5 ? local * 0.5 + float2(fmod(quarter, 2.0), floor(quarter * 0.5)) * 0.5 : local;
	bool mirrored = fmod(floor(code / 5.0), 2.0) > 0.5;
	if (mirrored) atlas.x = 1.0 - atlas.x;
	float width = 1.0 / length(IN.d.xyz);
	float aa = max(depth * TESR_ReciprocalResolution.y * 2.0 / TESR_ProjectionTransform[1][1], 0.05);
	float blood = tex2Dlod(TESR_BloodSplatter, float4(atlas, 0.0, log2(max(aa / width * 1024.0, 1.0)))).a;
	if (blood < 0.25) discard;
	float tiles = max(width / max(TESR_BloodFlowBeads.y, 1.0), 1.0);
	float4 drop = tex2Dlod(TESR_BloodDrops, float4(local * tiles, 0.0, log2(max(aa / width * tiles * 1024.0, 1.0))));
	float wet = 1.0 - smoothstep(0.0, TESR_BloodFlowDry.w * 0.8, IN.c.x);
	float show = saturate((wet * 1.1 - drop.a) * 6.0) * saturate(blood * 1.6 - 0.4);
	float h = drop.r * show;
	if (h < 0.01) discard;
	float2 slope = (drop.gb * 2.0 - 1.0) * show;
	float3 N = normalize(GetWorldNormalLod(uv));
	float3 T = normalize(IN.d.xyz) * (mirrored ? -1.0 : 1.0);
	float3 B = normalize(vAxis);
	float3 Nb = normalize(N + (T * slope.x - B * slope.y) * 1.5);
	float3 sceneLin = linearize(max(tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0, 0.0)).rgb, 0.0));
	float light = min(max(sceneLin.r, max(sceneLin.g, sceneLin.b)) / AssumedAlbedo, 2.5);
	float3 V = -normalize(rel);
	float a2 = 0.06 * 0.06 * 0.06 * 0.06;
	float glints = 0.0;
	if (TESR_BloodFlowInfo.x > 0.5) {
		float sunAmbient = max(luma(linearize(TESR_SunAmbient.rgb)), 0.001);
		float4 shadows = tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0.0, 0.0));
		float lit = saturate(pow(abs((shadows.r + shadows.g) / sunAmbient), 5.0));
		glints += Glint(Nb, V, normalize(TESR_SunDirection.xyz), a2) * lit * saturate(luma(linearize(TESR_SunColor.rgb)));
	}
	float3 P = TESR_CameraPosition.xyz + rel;
	[loop] for (int i = 0; i < 12; i++) {
		if (TESR_ShadowLightPosition[i].w <= 0.0 && TESR_LightPosition[i].w <= 0.0) break;
		glints += LampGlint(Nb, V, P, TESR_ShadowLightPosition[i], TESR_LightColor[i].rgb, a2);
		glints += LampGlint(Nb, V, P, TESR_LightPosition[i], TESR_LightColor[i + 12].rgb, a2);
	}
	float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(Nb, V)), 5.0);
	float3 surroundings = TESR_BloodFlowInfo.x > 0.5 ? linearize(saturate(TESR_HorizonColor.rgb)) : light * 0.1;
	float strength = TESR_BloodFlowBeads.x * IN.c.y;
	float3 shine = (min(glints, 1.0) * 0.45 * min(light, 1.2) + fresnel * surroundings * 0.4) * h * strength;
	float darken = 1.0 - 0.3 * h * saturate(strength);
	float3 under = sceneLin * darken;
	return float4(max(delinearize(under + shine) - delinearize(under), 0.0), darken);
}

technique BloodBeads
{
	pass
	{
		VertexShader = compile vs_3_0 BeadsVS();
		PixelShader = compile ps_3_0 BeadsPS();
		AlphaBlendEnable = true;
		SrcBlend = One;
		DestBlend = SrcAlpha;
		BlendOp = Add;
		SeparateAlphaBlendEnable = false;
		AlphaTestEnable = false;
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		CullMode = None;
		ScissorTestEnable = false;
		ColorWriteEnable = 7;
	}
}

technique BloodFlowBounds
{
	pass
	{
		VertexShader = compile vs_3_0 BoxVS();
		PixelShader = compile ps_3_0 BoundsPS();
		AlphaBlendEnable = false;
		AlphaTestEnable = false;
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		CullMode = None;
		ScissorTestEnable = false;
		ColorWriteEnable = 15;
	}
}

technique BloodFlowProbe
{
	pass
	{
		VertexShader = compile vs_3_0 BoxVS();
		PixelShader = compile ps_3_0 ProbePS();
		AlphaBlendEnable = false;
		AlphaTestEnable = false;
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		CullMode = None;
		ScissorTestEnable = false;
		ColorWriteEnable = 15;
	}
}

float SplatThickness(float2 q, float seed, float grow, float run, float armDrip[6]) {
	float r = length(q);
	float a = atan2(q.y, q.x);
	float slant = 0.3 + 0.7 * hash1(seed * 3.71);
	float lean = hash1(seed * 5.93) * 6.2832;
	float2 leanDir = float2(cos(lean), sin(lean));
	float coreR = SplatCoreRadius(a, seed, grow);
	float T = (coreR - r) / max(coreR, 0.05) * 1.6;
	float reach = TESR_BloodFlowSplat.y;
	[branch] if (T >= 0.9) return T;

	float arms = SplatArms(seed);
	[loop] for (float i = 0.0; i < 12.0; i += 1.0) {
		if (i >= arms) break;
		float h1 = hash1(seed * 13.7 + i * 7.31), h3 = hash1(seed * 41.9 + i * 5.53), h4 = hash1(seed * 57.1 + i * 2.29);
		float2 dir;
		float rootR, len, width;
		SplatArm(i, seed, grow, dir, rootR, len, width);
		float t;
		float d = SegmentDistance(q, dir * rootR, dir * len, t);
		float w = width * (1.0 - 0.7 * t) + 0.02 * sin(t * 11.0 + h1 * 40.0);
		T = max(T, (w - d) / max(width, 0.02) * 1.3);
		float2 tip = dir * (len + width * 0.5);
		float dropR = width * (1.0 + 0.5 * h3);
		T = max(T, (dropR - length(q - tip)) / max(dropR, 0.02) * 1.2);
		float dripLen = run * (0.15 + 0.9 * h4 * h4) * (0.6 + 0.6 * width / 0.15) * (i < Streams ? armDrip[(int)i] : 1.0);
		[branch] if (dripLen > 0.01) {
			float dw = dropR * 0.45;
			float dd = SegmentDistance(q, tip, tip + float2(0.0, dripLen), t);
			T = max(T, (dw * (1.0 - 0.25 * t) - dd) / max(dw, 0.01) * 1.1);
			T = max(T, (dw * 1.35 - length(q - tip - float2(0.0, dripLen))) / max(dw, 0.01) * 1.1);
		}
	}

	float lines = 20.0 + floor(hash1(seed * 17.9) * 40.0 * TESR_BloodFlowSplat.z);
	float fan = lerp(2.4, 0.8, slant);
	float bunches = 2.0 + floor(hash1(seed * 23.3) * 3.0);
	[loop] for (float j = 0.0; j < 60.0; j += 1.0) {
		if (j >= lines) break;
		float g1 = hash1(seed * 61.3 + j * 3.91), g2 = hash1(seed * 73.7 + j * 1.77), g3 = hash1(seed * 89.1 + j * 6.07);
		float g4 = hash1(seed * 37.7 + j * 4.43);
		float b = floor(g4 * bunches);
		float bAng = lean + (hash1(seed * 45.1 + b * 7.7) - 0.5) * 2.0 * fan;
		float bSpread = 0.12 + 0.3 * hash1(seed * 19.9 + b * 3.3);
		float loner = step(0.82, g4 * 0.6 + g2 * 0.4);
		float ang = loner > 0.5 ? g1 * 6.2832 : bAng + (g1 - 0.5) * 2.0 * bSpread;
		float2 dir = float2(cos(ang), sin(ang));
		float toward = 0.5 + 0.5 * dot(dir, leanDir);
		float lenBound = (1.0 + (0.2 + 2.2 * g3 * g3) * reach * lerp(0.5, 1.3, toward) * (1.0 - 0.6 * loner)) * grow;
		float across = abs(dot(q, float2(-dir.y, dir.x))), along = dot(q, dir);
		bool nearLine = along > 0.0 && along < lenBound + 0.03 && across < 0.03;
		float2 tip = dir * lenBound;
		bool nearDrips = run > 0.0 && q.x > min(0.0, tip.x) - 0.03 && q.x < max(0.0, tip.x) + 0.03 && q.y > min(0.0, tip.y) - 0.03;
		[branch] if (!nearLine && !nearDrips) continue;
		float start = SplatCoreRadius(ang, seed, grow) * (0.75 + 0.35 * g2);
		float len = (start + (0.2 + 2.2 * g3 * g3) * reach * lerp(0.5, 1.3, toward) * (1.0 - 0.6 * loner)) * grow;
		float t;
		float d = SegmentDistance(q, dir * start, dir * len, t);
		float w = (0.008 + 0.014 * g2) * (1.0 - 0.6 * t);
		float dashes = t > 0.45 ? step(0.45, frac(t * (6.0 + 10.0 * g1) + g3)) : 1.0;
		T = max(T, dashes > 0.5 ? (w - d) / max(w, 0.004) * 0.9 : -1.0);
		[branch] if (run > 0.0) {
			[unroll] for (int m = 0; m < 2; m++) {
				float g4 = hash1(seed * 101.3 + j * 2.53 + m * 7.1);
				[branch] if (g4 < 0.75 - 0.35 * m) {
					float2 from = dir * lerp(start, len, 0.2 + 0.7 * hash1(seed * 113.9 + j * 4.37 + m * 3.3));
					float dl = run * reach * (0.08 + 0.55 * g4 * g4);
					float dw = (0.008 + 0.014 * g2) * 0.9;
					float tt;
					float dd = SegmentDistance(q, from, from + float2(0.0, dl), tt);
					T = max(T, (dw * (1.0 - 0.3 * tt) - dd) / max(dw, 0.004) * 0.9);
					T = max(T, (dw * 1.5 - length(q - from - float2(0.0, dl))) / max(dw, 0.004) * 0.9);
				}
			}
		}
	}

	[branch] if (run > 0.0 && q.y > 0.0) {
		float cellW = 0.13;
		float cell = floor(q.x / cellW);
		[unroll] for (int k = -1; k <= 1; k++) {
			float c = cell + k;
			float e1 = hash1(seed * 97.3 + c * 1.37), e2 = hash1(seed * 31.1 + c * 2.71), e3 = hash1(seed * 53.9 + c * 0.93);
			float x = (c + 0.25 + 0.5 * e1) * cellW;
			float edge = 1.0 - saturate(abs(x) / max(SplatCoreRadius(1.5708, seed, grow), 0.05));
			[branch] if (e2 < 0.85 * sqrt(edge)) {
				float top = SplatCoreRadius(atan2(1.0, x), seed, grow) * sqrt(saturate(1.0 - x * x / 0.36)) * 0.85;
				float len = run * (0.2 + 2.2 * e3 * e3) * (0.4 + 0.6 * edge) * reach;
				float w = (0.025 + 0.045 * e2) * (0.6 + 0.6 * edge);
				float t;
				float dd = SegmentDistance(q, float2(x, top - 0.1), float2(x + 0.02 * (e1 - 0.5), top + len), t);
				T = max(T, (w * (1.0 - 0.2 * t) - dd) / max(w, 0.01) * 1.2);
				T = max(T, (w * 1.4 - length(q - float2(x, top + len))) / max(w, 0.01) * 1.2);
			}
		}
	}

	[branch] if (TESR_BloodFlowSplat.z > 0.0 && r > coreR) {
		float bins = 48.0;
		float2 cell = float2(floor((a / 6.2832 + 0.5) * bins), floor(r * 7.0));
		float hc = hash2(cell + seed * 17.0);
		float side = 0.5 + 0.5 * dot(q / max(r, 1e-4), leanDir);
		[branch] if (hc < 0.25 * TESR_BloodFlowSplat.z * (0.25 + 1.1 * side * side) && r < (0.6 + 1.8 * reach * side) * grow) {
			float ca = ((cell.x + 0.2 + 0.6 * hash2(cell + 3.1)) / bins - 0.5) * 6.2832;
			float cr = (cell.y + 0.2 + 0.6 * hash2(cell + 7.7)) / 7.0;
			float2 c = float2(cos(ca), sin(ca)) * cr;
			float2 o = normalize(c);
			float2 rel = q - c;
			float2 e = float2(dot(rel, o) / 1.8, dot(rel, float2(-o.y, o.x)));
			float rm = 0.01 + 0.025 * hash2(cell + 11.3);
			T = max(T, (rm - length(e)) / rm * 0.8);
		}
	}
	return T;
}

VSOUT SplatVS(VSIN IN) {
	VSOUT OUT;
	OUT.a = IN.a; OUT.b = IN.b; OUT.c = IN.c; OUT.d = IN.d;
	float limit = IN.b.w * (1.2 + 2.4 * TESR_BloodFlowSplat.y + 2.4 * TESR_BloodFlowSplat.w) + 2.0;
	float3 corner = clamp(IN.position.xyz, -limit, limit);
	float3 rel = IN.a.xyz + corner;
	float face = floor(IN.position.w + 0.5);
	float axis = floor(face * 0.5);
	float side = face - axis * 2.0 > 0.5 ? 1.0 : -1.0;
	float plane = axis < 0.5 ? rel.x : (axis < 1.5 ? rel.y : rel.z);
	OUT.position = side * plane > 0.0 ? mul(float4(rel + TESR_BloodFlowShape.xyz, 1.0), TESR_ViewProjectionTransform) : float4(0.0, 0.0, -2.0, 1.0);
	return OUT;
}

float4 SplatPS(PSIN IN) : COLOR0 {
	float2 uv = (IN.vpos + 0.5) * TESR_ReciprocalResolution.xy;
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	if (pool > 0.5 || TESR_BloodFlowSplat.x <= 0.0) discard;
	float viewmodelDepth = tex2D(TESR_DepthBufferViewModel, uv).x;
	if ((TESR_DepthConstants.z == 0 && viewmodelDepth < 0.9) || (TESR_DepthConstants.z > 0 && viewmodelDepth > 0.01)) discard;
	float depth = readDepth(uv);
	if (depth >= farZ * 0.979) discard;
	float3 rel = toWorld(uv) * depth;
	float3 pp = rel - IN.a.xyz;
	float R = max(IN.b.w, 1.0);
	float3 n = normalize(IN.b.xyz);
	float3 N = normalize(GetWorldNormalLod(uv));
	if (abs(dot(pp, n)) > R * 0.6 + 4.0 || dot(N, n) < 0.3) discard;
	float3 down = float3(n.x * n.z, n.y * n.z, n.z * n.z - 1.0);
	float steep = length(down);
	down = steep > 0.05 ? down / steep : float3(1.0, 0.0, 0.0);
	float3 across = normalize(cross(n, down));
	float2 q = float2(dot(pp, across), dot(pp, down)) / R;
	float seed = ItemSeed(IN.c.z);
	float age = IN.c.x;
	float grow = smoothstep(0.0, 1.0, saturate(age / 0.12));
	float run = TESR_BloodFlowSplat.w * saturate(steep * 3.0 - 0.15) * (1.0 - exp(-max(age - 0.3, 0.0) / 4.0));
	float aa = max(depth * TESR_ReciprocalResolution.y * 2.0 / TESR_ProjectionTransform[1][1], 0.05) / R;
	float armDrip[6];
	[unroll] for (int ai = 0; ai < 6; ai++) armDrip[ai] = PathTexel(0.0, slot * Streams + ai).w < -0.5 ? 1.0 : 0.0;
	float T = SplatThickness(q, seed, grow, run, armDrip);
	float cover = saturate(T / max(aa * 2.0, 0.02) + 0.5) * saturate(T * 4.0 + 0.6);
	if (cover < 0.004) discard;
	float thick = saturate(T * 0.6);

	float3 sceneLin = linearize(max(tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0, 0.0)).rgb, 0.0));
	float lit = 0.0;
	if (TESR_BloodFlowInfo.x > 0.5) {
		float sunAmbient = max(luma(linearize(TESR_SunAmbient.rgb)), 0.001);
		float4 shadows = tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0.0, 0.0));
		lit = saturate(pow(abs((shadows.r + shadows.g) / sunAmbient), 5.0));
	}
	float irradiance = luma(linearize(saturate(TESR_SunAmbient.rgb))) + lit * luma(linearize(saturate(TESR_SunColor.rgb))) * saturate(dot(N, normalize(TESR_SunDirection.xyz)));
	float light = min(max(max(sceneLin.r, max(sceneLin.g, sceneLin.b)) / AssumedAlbedo, irradiance), 2.5);
	float dry = smoothstep(0.0, 1.0, age / (TESR_BloodFlowDry.w * lerp(0.5, 1.6, thick)));
	float3 freshColour = TESR_BloodFlowFresh.rgb;
	float3 dryColour = TESR_BloodFlowDry.rgb;
	[branch] if (TESR_BloodFlowMatch.w > 0.5 && IN.d.w > 0.5) {
		float tc = floor(IN.d.w + 0.5);
		float cr = floor(tc / 65536.0);
		cr += (tc - cr * 65536.0 >= 65536.0) ? 1.0 : ((tc - cr * 65536.0 < 0.0) ? -1.0 : 0.0);
		float gb = tc - cr * 65536.0;
		float cg = floor(gb / 256.0);
		cg += (gb - cg * 256.0 >= 256.0) ? 1.0 : ((gb - cg * 256.0 < 0.0) ? -1.0 : 0.0);
		freshColour = linearize(saturate(float3(cr, cg, gb - cg * 256.0) / 255.0 * TESR_BloodFlowMatch.x)) * TESR_BloodFlowMatch.y;
		dryColour = freshColour * float3(0.72, 0.8, 0.8) + freshColour.r * float3(0.0, 0.1, 0.05);
	}
	float3 albedo = lerp(freshColour, dryColour, dry) * lerp(1.0, 0.8 + 0.4 * valueNoise(q * R * 0.35 + seed * 31.0), dry);
	float3 through = exp(-(1.2 + log(max(albedo.r, 0.001) / max(albedo, 0.001))) * (0.25 + 2.0 * thick));
	float3 colour = sceneLin * through + light * albedo * lerp(1.0, 0.6, saturate(thick * 1.3 - 0.3)) * (1.0 - through);
	float gloss = TESR_BloodFlowFresh.w * (1.0 - dry) * lerp(0.5, 1.0, thick);
	[branch] if (gloss > 0.001) {
		float3 V = -normalize(rel);
		float3 Nb = normalize(N + (across * q.x + down * q.y) * saturate(1.0 - T) * 0.15);
		float a2 = pow(lerp(0.1, 0.4, dry), 4.0);
		float glints = 0.0;
		if (TESR_BloodFlowInfo.x > 0.5) glints += Glint(Nb, V, normalize(TESR_SunDirection.xyz), a2) * lit * saturate(luma(linearize(TESR_SunColor.rgb)));
		float3 P = TESR_CameraPosition.xyz + rel;
		[loop] for (int i = 0; i < 12; i++) {
			if (TESR_ShadowLightPosition[i].w <= 0.0 && TESR_LightPosition[i].w <= 0.0) break;
			glints += LampGlint(Nb, V, P, TESR_ShadowLightPosition[i], TESR_LightColor[i].rgb, a2);
			glints += LampGlint(Nb, V, P, TESR_LightPosition[i], TESR_LightColor[i + 12].rgb, a2);
		}
		float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(Nb, V)), 5.0);
		float3 surroundings = TESR_BloodFlowInfo.x > 0.5 ? linearize(saturate(TESR_HorizonColor.rgb)) : light * 0.1;
		colour += (min(glints, 1.0) * 0.4 * min(light, 1.2) + fresnel * surroundings * 0.4) * gloss;
	}
	float ceiling = max(max(sceneLin.r, max(sceneLin.g, sceneLin.b)), 1.0);
	colour = min(colour, ceiling);
	float alpha = cover * IN.c.y * saturate(TESR_BloodFlowSplat.x);
	return float4(delinearize(max(colour, 0.0)), alpha);
}

technique BloodSplat
{
	pass
	{
		VertexShader = compile vs_3_0 SplatVS();
		PixelShader = compile ps_3_0 SplatPS();
		AlphaBlendEnable = true;
		SrcBlend = SrcAlpha;
		DestBlend = InvSrcAlpha;
		BlendOp = Add;
		SeparateAlphaBlendEnable = false;
		AlphaTestEnable = false;
		ZEnable = false;
		ZWriteEnable = false;
		StencilEnable = false;
		CullMode = None;
		ScissorTestEnable = false;
		ColorWriteEnable = 7;
	}
}
