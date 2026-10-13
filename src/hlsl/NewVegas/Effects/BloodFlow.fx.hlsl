// UNOFFICIAL, optional (Shaders.BloodFlow, off by default): blood that runs like a liquid, dries, and pools under bodies.
// BloodFlow.cpp sends the game's blood splats and the bodies' pools as items. Two passes:
//  - BloodFlowProbe (ProbePS): for each item, into a small path texture, where its streams of blood go. Each stream
//    starts on the splat's lower part and is walked down the surface the depth buffer shows, step by step, the way
//    gravity pulls along that surface (straight down a wall, slantwise across a slope, round a rock). It stops where the
//    surface turns flat (the blood gathers there: on a ledge, at the foot of a wall) or comes to an edge with nothing
//    under it (the blood hangs there and falls in drops; the drops' landing place is found the same way). A pool finds
//    the slope of the ground round it, and trickles run off its downhill side. Once a path has been seen whole for a
//    few frames it is kept, so it does not move when the camera does.
//  - BloodFlow (BloodPS): a box round each item; every pixel in it finds its surface point from the depth buffer and
//    paints what reaches it: the streams (thin trails with a bead at the front, running over the first seconds and
//    slowing), the puddles they end in, the drop hanging at an edge, drops falling (drawn in the air, in front of what
//    is behind them) and the stain where they land; the pool, stretched downhill on a slope. Blended into the image.
// The blood's colour is the light at the pixel (worked out from the image) times the blood's own colour: a thin film
// is a dark red stain the surface shows through, a thick layer is the blood's own dark red. Fresh blood shines a little
// (the sun where it is not shaded, the lamps); dried blood is darker, browner and matte. Nothing glows.

float4 TESR_ReciprocalResolution;
float4 TESR_SunDirection;
float4 TESR_SunColor;
float4 TESR_SunAmbient;
float4 TESR_LightPosition[12];
float4 TESR_ShadowLightPosition[12];
float4 TESR_LightColor[24];
float4 TESR_BloodFlowFresh;	// rgb: fresh blood colour (linear); w: gloss
float4 TESR_BloodFlowDry;	// rgb: dried blood colour (linear); w: splat dry time (s)
float4 TESR_BloodFlowInfo;	// x: outdoors; y: debug view; z: pool dry time (s); w: drip speed
float4 TESR_BloodFlowPool;	// x: pool spread time (s); y: longest drip (game units); z: longest fall (game units); w: gravity (units/s^2)
float4 TESR_BloodFlowProbe;	// x: 1 / path texture rows; y: finest detail (units per pixel) a path is kept at

// (Declared in register order: the samplers bind by the order they are declared in.)
sampler2D TESR_DepthBuffer : register(s0) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_SourceBuffer : register(s1) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_NormalsBuffer : register(s2) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_PointShadowBuffer : register(s3) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = LINEAR; MINFILTER = LINEAR; MIPFILTER = NONE; };
sampler2D TESR_DepthBufferViewModel : register(s4) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BloodFlowPaths : register(s5) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };
sampler2D TESR_BloodFlowBounds : register(s6) = sampler_state { ADDRESSU = CLAMP; ADDRESSV = CLAMP; MAGFILTER = POINT; MINFILTER = POINT; MIPFILTER = NONE; };

#include "Includes/Helpers.hlsl"
#include "Includes/Depth.hlsl"
#include "Includes/Normals.hlsl"

static const float AssumedAlbedo = 0.3;	// a typical surface's brightest channel: the light at a pixel is about that over this
static const float Streams = 6.0;		// rows per item in the path texture (BloodFlowBoxes.h)
static const float PathColumns = 16.0;	// texels per row: 0 header, 1 end, 2-15 points along the path
static const float LockFrames = 3.0;	// a path seen whole this many frames running is kept
static const float WallFlatZ = 0.94;	// blood running down stops on anything flatter than about 20 degrees
static const float GroundFlatZ = 0.985;	// trickles off a pool run on down to about 10 degrees
// UNOFFICIAL 2026-10-13 (user: streaks did not run down terrain): a splat on the ground (flatter than WallFlatZ) runs downhill
// until the ground is about level, not until 20 degrees as on a wall: the Mojave's slopes are mostly gentler than that, so
// every ground splat stayed a round stain.
static const float SplatGroundFlatZ = 0.996;	// about 5 degrees
static const float UnderZ = -0.3;		// a surface facing down more than this: the blood drips off it

struct VSIN {
	float4 position : POSITION;	// clip space (BloodFlow.cpp projects the box, or places the probe quad)
	float4 a : TEXCOORD0;		// xyz: the item's centre, eye-relative; w: slot + 1000 if a pool + 2000 if its rows are new
	float4 b : TEXCOORD1;		// xyz: a guess at the surface's normal; w: radius (splat) or largest radius (pool)
	float4 c : TEXCOORD2;		// x: age (s); y: fade; z: seed 0-4095 (a whole number); w: longest drip (0: none)
	float4 d : TEXCOORD3;		// unused
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

// The item's slot code (VSIN a.w): slot, pool, fresh rows.
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

// ---------------------------------------------------------------------------------------------------------------------
// Streams: where each starts, how far and how fast it runs. The same in both passes (the probe walks the path, the
// paint pass shows as much of it as has run by now).

struct Stream {
	float active;
	float2 start;		// splats: across and down the splat's surface from its centre
	float lengthMax;	// the most it runs, along the surface
	float tau;			// how fast (most of the way in about twice this, in seconds)
	float delay;		// seconds before it starts
	float width;		// half its width
	float dropVolume;	// how much run (in length of stream) makes one drop at an edge
};

Stream SplatStream(float seed, float k, float R, float dripMax, float speed) {
	Stream s;
	float s1 = hash1(seed * 91.7 + k * 13.1);
	float s2 = hash1(seed * 47.3 + k * 7.7);
	float s3 = hash1(seed * 23.9 + k * 3.3);
	s.active = (s3 >= 0.3 && dripMax > 0.0) ? 1.0 : 0.0;	// not every part of a splat runs
	float x0 = (s1 * 2.0 - 1.0) * 0.6 * R;
	s.start = float2(x0, R * (0.35 + 0.35 * s2) * sqrt(saturate(1.0 - sqr(x0 / R))));	// from near the splat's lower edge
	s.lengthMax = dripMax * (0.3 + 0.7 * s3) * saturate(R / 6.0 + 0.3);				// bigger splats run further
	s.tau = (1.5 + 3.5 * s2) / speed;
	s.delay = (0.2 + 1.2 * s1) / speed;
	s.width = clamp(R * (0.05 + 0.05 * s1), 0.35, 2.0);
	s.dropVolume = (2.5 + 2.5 * s2) * s.width;
	return s;
}

// A trickle running off a pool on a slope (k 0 or 1). slope: the ground's rise over run.
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
	s.width = clamp(R * 0.06 * (0.8 + 0.4 * s1), 0.8, 2.5);
	s.dropVolume = (2.5 + 2.5 * s2) * s.width;
	return s;
}

// How far a stream has run by `age` (it slows as it goes).
float StreamRun(Stream s, float lengthMax, float age) {
	return lengthMax * (1.0 - exp(-max(age - s.delay, 0.0) / s.tau));
}

// When a stream has run `run` (inverse of StreamRun).
float StreamTime(Stream s, float lengthMax, float run) {
	return s.delay - s.tau * log(max(1.0 - run / lengthMax, 1e-4));
}

// A pool's middle moves downhill on a slope and it stretches that way (slope: rise over run).
float PoolShift(float slope) { return 0.35 * saturate(slope * 2.0); }
float PoolStretch(float slope) { return 1.0 + 0.9 * saturate(slope * 2.0); }

// ---------------------------------------------------------------------------------------------------------------------
// The probe: what the depth buffer shows.

float3 CameraRight() { return float3(TESR_ViewTransform[0][0], TESR_ViewTransform[1][0], TESR_ViewTransform[2][0]); }
float3 CameraUp() { return float3(TESR_ViewTransform[0][1], TESR_ViewTransform[1][1], TESR_ViewTransform[2][1]); }
float3 CameraForward() { return float3(TESR_ViewTransform[0][2], TESR_ViewTransform[1][2], TESR_ViewTransform[2][2]); }

// An eye-relative point -> (u, v, view depth); the inverse of toWorld(uv) * depth.
float3 ProjectRel(float3 rel) {
	float z = dot(rel, CameraForward());
	float2 ndc = float2(dot(rel, CameraRight()) * TESR_ProjectionTransform[0][0], dot(rel, CameraUp()) * TESR_ProjectionTransform[1][1]) / max(z, 0.001);
	return float3(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y, z);
}

// What the depth buffer shows on the line of sight through Q (eye-relative): X, the surface there, and N its normal;
// along: how far that surface is behind Q along the line (negative: in front of it); tol: how far off still counts as
// on it (more for a coarser step, a far surface or one seen edge on). False when Q cannot be seen: off screen, behind
// the eye or behind the weapon.
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
	if (depth >= farZ * 0.98) { along = 1e6; return true; }		// sky: nothing behind it
	X = toWorld(s.xy) * depth;
	N = normalize(GetWorldNormalLod(s.xy));
	along = (depth - s.z) * dist / s.z;
	float pixel = s.z * 2.0 * TESR_ReciprocalResolution.y / TESR_ProjectionTransform[1][1];
	tol = (0.75 + 0.5 * stepSize + 2.0 * pixel) / max(abs(dot(N, Q / dist)), 0.2);
	return true;
}

// Runs blood from S (eye-relative, on the surface, normal N) down the surface for up to `target` units, the way gravity
// pulls along it, in steps of h. Returns where it got to (xyz) and how far it ran (w). end: 0 it ran the whole way,
// 1 it stopped on something flat (it gathers there), 2 it came to an edge with nothing under it (it drips off there).
// valid becomes 0 if part of the way could not be seen (the path then carries on along the last surface seen).
float4 RunDown(float3 S, float3 N, float target, float h, float flatZ, float wiggleSeed, inout float valid, out float end) {
	float3 pos = S;
	float arc = 0.0;
	end = 0.0;
	[loop] for (int i = 0; i < 48; i++) {
		if (arc >= target - 0.001) break;
		if (N.z > flatZ) { end = 1.0; break; }
		if (N.z < UnderZ) { end = 2.0; break; }
		// Gravity (0, 0, -1) along the surface, with a slow sideways wander of its own.
		float3 d = normalize(float3(N.x * N.z, N.y * N.z, N.z * N.z - 1.0));
		float3 side = normalize(cross(N, d));
		d = normalize(d + side * (valueNoise(float2(arc * 0.07 + wiggleSeed, wiggleSeed * 3.1)) - 0.5) * 0.6);
		float stepSize = min(h, target - arc);
		float3 next = pos + d * stepSize;
		float3 X, N2;
		float along, tol;
		if (!SeeSurface(next, stepSize, X, N2, along, tol)) { valid = 0.0; pos = next; arc += stepSize; continue; }
		if (abs(along) <= tol) { pos = X; N = N2; arc += stepSize; continue; }
		if (along > 0.0) {
			// Nothing under the next step: an edge. Find the brink by halving the step twice.
			float sz = stepSize;
			[unroll] for (int b = 0; b < 2; b++) {
				sz *= 0.5;
				float3 Xm, Nm;
				float am, tm;
				if (SeeSurface(pos + d * sz, sz, Xm, Nm, am, tm) && abs(am) <= tm) { pos = Xm; arc += sz; }
			}
			// Round the edge: if there is a surface right under the brink (the face below a slope's edge, not the
			// underside of an overhang), the blood goes on down it.
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
			// Into something standing out under it (a ledge, the floor): the blood stops on its top.
			pos = float3(next.xy, X.z);
			arc += stepSize;
			end = 1.0;
			break;
		}
		// Hidden behind something: carry on along the same surface.
		valid = 0.0;
		pos = next;
		arc += stepSize;
	}
	return float4(pos, arc);
}

// Falls straight down from E (eye-relative) until something under it is seen. Returns how far, or -1 if nothing within
// maxFall.
float FallFrom(float3 E, float maxFall, inout float valid) {
	float stepSize = max(maxFall / 24.0, 2.0);
	[loop] for (int j = 1; j <= 24; j++) {
		float s = j * stepSize;
		if (s > maxFall + 0.01) break;
		float3 F = E - float3(0.0, 0.0, s);
		float3 X, N;
		float along, tol;
		if (!SeeSurface(F, stepSize, X, N, along, tol)) { valid = 0.0; continue; }
		if (along > tol) continue;										// still in the air
		if (N.z > 0.5 && X.z >= F.z - tol && X.z <= F.z + stepSize + tol) return max(E.z - X.z, 0.0);	// the ground
		valid = 0.0;													// something in front hides it
	}
	return -1.0;
}

// The probe pass: one texel per (column, row) of the path texture, for the item whose quad covers it.
//  Splat stream rows, and the trickle rows (0, 1) of a pool:
//   column 0: (frames seen whole, how far it runs before it stops, how far drops fall from its end (-1: no ground seen),
//              end: -1 no stream, 0 runs its length, 1 gathers on the flat, 2 drips off an edge)
//   column 1: (where it stops, item-relative; longest run)
//   columns 2-15: points along the path (item-relative xyz, distance along it), evenly spaced over the longest run
//  A pool's last row (the ground round it): column 0 (frames seen whole, points seen, 0, -2), column 1 (the ground's
//   normal, its height under the centre relative to the centre).
float4 ProbePS(PSIN IN) : COLOR0 {
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	float column = floor(IN.vpos.x);
	float row = floor(IN.vpos.y);
	float k = row - slot * Streams;
	float4 previous = PathTexel(column, row);
	float4 previousHead = PathTexel(0.0, row);
	if (fresh < 0.5 && previousHead.x >= LockFrames) return previous;	// kept
	float seenBefore = fresh > 0.5 ? 0.0 : previousHead.x;

	float3 C = IN.a.xyz;
	float R = IN.b.w;
	float seed = floor(IN.c.z + 0.5) / 4096.0;
	// Too far away to see the surface's shape well: worked out every frame, never kept.
	float3 cs = ProjectRel(C);
	float pixel = cs.z * 2.0 * TESR_ReciprocalResolution.y / TESR_ProjectionTransform[1][1];
	float valid = pixel <= TESR_BloodFlowProbe.y ? 1.0 : 0.0;

	if (pool > 0.5 && k > Streams - 1.5) {
		// The ground round the pool: a ring of points just outside it, where the ground (not the body) shows. Twice: the
		// second time the points follow the slope the first found, and only ground facing about the same way counts
		// (not an arm or a leg).
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
	float none = 0.0;			// no stream: 1 for good (kept at once), 2 for now (worked out again next frame)
	if (pool > 0.5) {
		// A trickle off the pool's downhill side, once the ground round it is known (last frame's) and slopes.
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
		// The surface at the splat's centre: its slope decides where the streams start.
		float3 X0, N0;
		float along0, tol0;
		bool seen0 = SeeSurface(C, 2.0, X0, N0, along0, tol0) && abs(along0) <= R * 0.5 + 4.0;
		if (!seen0) valid = 0.0;
		float3 plane = seen0 ? N0 : normalize(IN.b.xyz);
		if (plane.z > WallFlatZ) flatZ = SplatGroundFlatZ;	// on the ground: downhill until about level
		if (s.active < 0.5) none = 1.0;
		else if (plane.z > flatZ) none = seen0 ? 1.0 : 2.0;	// on a floor: it stays where it is
		float3 down = float3(plane.x * plane.z, plane.y * plane.z, plane.z * plane.z - 1.0);
		down = length(down) > 0.01 ? normalize(down) : float3(0.0, 0.0, -1.0);
		float3 across = normalize(cross(plane, down));
		S = (seen0 ? X0 : C) + across * s.start.x + down * s.start.y;
		N = plane;
	}
	if (none > 0.5) {
		if (column < 0.5) return float4(none < 1.5 ? LockFrames : 0.0, 0.0, 0.0, -1.0);
		return float4(0.0, 0.0, 0.0, 0.0);
	}
	// Onto the surface.
	float3 X, N1;
	float along, tol;
	if (SeeSurface(S, 2.0, X, N1, along, tol) && abs(along) <= tol * 2.0) { S = X; N = N1; }
	else valid = 0.0;

	float h = max(s.lengthMax / 40.0, 0.6);
	float target = column < 1.5 ? s.lengthMax : (column - 2.0) * s.lengthMax / 13.0;
	float end;
	float4 reached = RunDown(S, N, target, h, flatZ, seed * 61.0 + k * 7.0, valid, end);
	if (column > 1.5) return float4(reached.xyz - C, reached.w);
	if (column > 0.5) return float4(reached.xyz - C, s.lengthMax);
	float fall = 0.0;
	if (end > 1.5) fall = FallFrom(reached.xyz, TESR_BloodFlowPool.z, valid);	// (not ?: : HLSL works out both sides)
	return float4(valid > 0.5 ? seenBefore + 1.0 : 0.0, reached.w, fall, end);
}

// ---------------------------------------------------------------------------------------------------------------------
// The bounds pass (BoundsPS): for each item (row = its slot, from the path texture just written), the box round all
// its blood can reach, item-relative: column 0 (lowest corner, 1), column 1 (highest corner, 1). The paint pass drops
// every pixel outside it at once (the box drawn round an item has to allow for any path; this one is the real paths).

float4 BoundsPS(PSIN IN) : COLOR0 {
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	float column = floor(IN.vpos.x);
	float R = IN.b.w;
	float seed = floor(IN.c.z + 0.5) / 4096.0;
	float firstRow = slot * Streams;
	float3 lo, hi;
	float slope = 0.0;
	float count = Streams;
	if (pool > 0.5) {
		// The pool, moved and stretched downhill (see BloodPS), on the ground's plane.
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
		// The game's splat itself (it dries too).
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
		// The path strays at most this far from the line from its start to its end (see PaintPath); its puddle at most
		// this big.
		float3 chord = last.xyz - first;
		float stray = 0.5 * sqrt(max(head.y * head.y - dot(chord, chord), 0.0));
		float grow = stray + s.width * 4.0 + 3.0 + sqrt(max(last.w - head.y, 0.0) * s.width);
		lo = min(lo, min(first, last.xyz) - grow);
		hi = max(hi, max(first, last.xyz) + grow);
		if (head.w > 1.5) {
			// Drops falling from its end, and their stain.
			float3 L = last.xyz - float3(0.0, 0.0, head.z > 0.0 ? head.z : TESR_BloodFlowPool.z);
			float spread = s.width * 10.0 + 3.0;
			lo = min(lo, L - spread);
			hi = max(hi, float3(L.xy + spread, L.z + 3.0));
		}
	}
	return column < 0.5 ? float4(lo, 1.0) : float4(hi, 1.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// The paint pass.

// What blood a pixel gets: the strongest thing that reaches it decides how thick it is and how it leans.
struct Paint {
	float cover;	// 0-1 how much of the pixel's surface it covers
	float thick;	// 0 a thin film (a stain the surface shows through) - 1 a thick layer (pool, drop)
	float3 bend;	// how its surface leans (rounded trails and drops), added to the surface's normal
	float air;		// 0-1 a falling drop in front of the surface
	float debug;	// 1 trail, 2 puddle, 3 edge drop, 4 landing stain
};

void Add(inout Paint p, float c, float thick, float3 bend, float debug) {
	if (c > p.cover) {
		p.thick = thick;
		p.bend = bend;
		p.debug = debug;
		p.cover = c;
	}
}

// The closest distance between the eye ray (ro + rd t, rd unit length) and the segment a-b; t: where along the ray.
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

// One stream (path texture row `row`) on the surface: its trail, the bead at its front, and the puddle it ends in or the
// drop hanging at the edge it reached. pp: the pixel's surface point, item-relative.
void PaintPath(float row, Stream s, float3 pp, float aa, float age, float seed, inout Paint p) {
	float4 head = PathTexel(0.0, row);
	if (head.w < -0.5) return;
	float4 last = PathTexel(1.0, row);
	float lengthMax = max(last.w, 0.01);
	float3 first = PathTexel(2.0, row).xyz;
	float3 chord = last.xyz - first;
	float chordLength2 = dot(chord, chord);
	// UNOFFICIAL 2026-10-13 (user screenshot: on the ground the streams were stiff even bars with a round shiny cap at the end,
	// "like mushrooms", and did not seem to flow): how much this path runs over the ground rather than down a wall (a wall's
	// path drops about its whole length; one down a 10 degree slope only a sixth of it). On the ground: thinner, tapering to
	// a point, ragged and wandering a little, soaking a faint stain into the dirt round it, three times slower, and no cap:
	// what keeps coming ends as a small flat smear. Walls as before.
	float ground = 1.0 - smoothstep(0.45, 0.75, abs(chord.z) / max(sqrt(chordLength2), 1e-3));
	s.tau *= lerp(1.0, 3.0, ground);
	s.delay *= lerp(1.0, 2.0, ground);
	s.width = lerp(s.width, min(s.width, 1.0) * 0.7, ground);
	float run = StreamRun(s, lengthMax, age);
	float shown = min(run, head.y);
	float excess = max(run - head.y, 0.0);
	if (run < 0.05) return;
	// Is the pixel near the path at all? A path of length L from its start to its end strays at most
	// sqrt(L^2 - chord^2) / 2 from the straight line between them.
	float3 toLine = pp - (first + chord * saturate(dot(pp - first, chord) / max(chordLength2, 1e-4)));
	float stray = 0.5 * sqrt(max(head.y * head.y - chordLength2, 0.0));
	if (length(toLine) > stray + s.width * 3.0 + 2.0 + sqrt(excess * s.width) + aa) return;
	// The points are spaced `spacing` apart along the path (until it stops): the closest point on the part run so far.
	float spacing = lengthMax / 13.0;
	float segments = min(ceil(shown / spacing - 0.001), 13.0);
	float best = 1e9;
	float bestArc = 0.0;
	float3 toBest = 0.0;
	float4 A = PathTexel(2.0, row);
	[loop] for (float j = 0.0; j < segments; j += 1.0) {
		float4 B = PathTexel(j + 3.0, row);
		float3 ab = B.xyz - A.xyz;
		float l2 = dot(ab, ab);
		float most = saturate((shown - A.w) / max(B.w - A.w, 1e-4));
		float t = l2 > 1e-6 ? min(saturate(dot(pp - A.xyz, ab) / l2), most) : 0.0;
		float3 off = pp - (A.xyz + ab * t);
		float dist = length(off);
		if (dist < best) { best = dist; bestArc = lerp(A.w, B.w, t); toBest = off; }
		A = B;
	}
	// The front: `shown` along the path.
	float f = min(shown / spacing, 13.0);
	float4 P0 = PathTexel(2.0 + floor(f), row);
	float4 P1 = PathTexel(2.0 + min(floor(f) + 1.0, 13.0), row);
	float3 tip = lerp(P0.xyz, P1.xyz, saturate((shown - P0.w) / max(P1.w - P0.w, 1e-4)));
	// On the ground the trail wanders a little either side of the path (it follows the dirt's small bumps) and its edge is
	// ragged: the distance measured to the wandering line (sideways to the way it runs at its front), the width varied along it.
	float lumpy = valueNoise(float2(bestArc * 0.09 + seed * 37.0, row * 1.7));
	[branch] if (ground > 0.01) {
		float3 side = cross(P1.xyz - P0.xyz, float3(0.0, 0.0, 1.0));
		side *= rsqrt(max(dot(side, side), 1e-6));
		toBest -= side * ((lumpy - 0.5) * 2.2 * ground * saturate(bestArc / 6.0));
		best = length(toBest);
	}
	// The trail: thinner as it goes, a thin film (it leaves only a coat behind the bead). On the ground: to a fine point,
	// with a ragged edge, and a faint stain soaked into the dirt round it.
	float along = saturate(bestArc / max(shown, 0.01));
	float rag = lerp(1.0, 0.65 + 0.7 * frac(lumpy * 7.31 + bestArc * 0.23), ground);
	float w = s.width * lerp(1.0, 0.65, along) * lerp(1.0, (1.0 - 0.75 * along) * rag, ground);
	Add(p, saturate(0.5 - (best - w) / aa) * lerp(0.95, 0.75, along), lerp(0.3, 0.2, ground), toBest / max(w, 0.01) * lerp(0.6, 0.3, ground), 1.0);
	[branch] if (ground > 0.01) {
		float soak = w * 2.6 + 0.6;
		Add(p, saturate(1.0 - best / soak) * 0.3 * ground * (1.0 - 0.5 * along), 0.0, 0.0, 1.0);
	}
	float3 toTip = pp - tip;
	[branch] if (excess <= 0.0) {
		// The bead at the front, fattening as it runs (on the ground only a little: it soaks in as it goes).
		float bead = w * lerp(1.3 + 0.5 * saturate(shown / lengthMax), 1.15, ground);
		Add(p, saturate(0.5 - (length(toTip) - bead) / aa), lerp(0.8, 0.45, ground), toTip / bead * lerp(1.0, 0.5, ground), 1.0);
	}
	else if (head.w < 1.5) {
		// Gathered where it stopped: a puddle as big as what kept coming. UNOFFICIAL 2026-10-13: on the ground it soaks in
		// instead: a smaller, flat, matte smear (the round shiny cap read as a mushroom on the end of every trail).
		float rp = s.width * 1.2 + sqrt(excess * s.width * lerp(0.9, 0.3, ground));
		float edge = rp * (0.8 + lerp(0.35, 0.5, ground) * valueNoise(float2(atan2(toTip.y, toTip.x) * lerp(1.6, 2.6, ground) + seed * 23.0, row * 3.0)));
		float d = length(toTip) - edge;
		Add(p, saturate(0.5 - d / aa), lerp(0.6 + 0.3 * saturate(-d / max(rp * 0.5, 0.5)), 0.3, ground), float3(toTip.xy / max(rp, 0.1), 0.0) * saturate(1.0 + d / max(rp * 0.3, 0.3)) * 0.4 * (1.0 - ground), 2.0);
	}
	else {
		// Hanging at the edge, swelling until it lets go.
		float rh = s.width * (1.0 + 0.9 * pow(frac(excess / s.dropVolume), 0.333));
		float3 off = toTip + float3(0.0, 0.0, rh * 0.5);
		Add(p, saturate(0.5 - (length(off) - rh) / aa), 0.9, off / rh, 3.0);
	}
}

// One stream's drops, if it runs off an edge: one leaves each time `dropVolume` more has run, falling as anything
// falls (drawn on the eye ray ro + rd t in front of the surface at sceneDist), and the stain where they land.
void PaintDrops(float row, Stream s, float3 pp, float3 ro, float3 rd, float sceneDist, float aa, float age, float seed, inout Paint p) {
	float4 head = PathTexel(0.0, row);
	if (head.w < 1.5) return;
	float4 last = PathTexel(1.0, row);
	float lengthMax = max(last.w, 0.01);
	float excess = StreamRun(s, lengthMax, age) - head.y;
	if (excess < s.dropVolume) return;
	float3 E = last.xyz;		// the edge
	float fallLength = head.z > 0.0 ? head.z : TESR_BloodFlowPool.z;
	// Nowhere near the column the drops fall down, nor the stain under it: nothing to do.
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
		float dist = RaySegment(ro, rd, bottom, bottom + float3(0.0, 0.0, min(g * since * 0.02, 8.0)), t);	// a little stretched by its speed
		if (t > 0.0 && t < sceneDist - 0.5) p.air = max(p.air, saturate(0.5 - (dist - radius) / max(aa * t / sceneDist, 0.02)));
	}
	// Where they land: a stain that grows with each drop, with a few spatters round it.
	float landed = floor(max(StreamRun(s, lengthMax, age - fallTime) - head.y, 0.0) / s.dropVolume);
	[branch] if (head.z > 0.0 && landed >= 1.0) {
		float3 off = pp - E + float3(0.0, 0.0, head.z);
		float rl = s.width * (1.2 + 0.9 * sqrt(landed));
		float edge = rl * (0.75 + 0.5 * valueNoise(float2(atan2(off.y, off.x) * 2.0 + seed * 40.0, row)));
		float c = saturate(0.5 - (length(off) - edge) / aa);
		float cellSize = s.width * 1.5;
		float2 inCell = frac(off.xy / cellSize) - 0.5;
		float spot = hash2(floor(off.xy / cellSize) + seed * 13.0) > 0.8 ? saturate(0.5 - (length(inCell) * cellSize - s.width * 0.35) / aa) : 0.0;
		spot *= step(length(off), rl * 2.2) * saturate(landed / 3.0);
		Add(p, max(c, spot) * step(abs(off.z), 3.0), 0.55, float3(0.0, 0.0, 0.0), 4.0);
	}
}

// A highlight's shape, 1 at its peak (Blinn-Phong): its brightness is set by the caller.
float Lobe(float3 N, float3 V, float3 L, float exponent) {
	float3 H = normalize(L + V);
	return pow(saturate(dot(N, H)), exponent) * saturate(dot(N, L) * 4.0);
}

// A lamp's share of the highlight: lamp.xyz in world space, lamp.w its radius (0: no lamp). The fall-off is WetWorld's.
float LampLobe(float3 N, float3 V, float3 P, float4 lamp, float3 color, float exponent) {
	if (lamp.w <= 0.0) return 0.0;
	float3 toLamp = lamp.xyz - P;
	float s = saturate(dot(toLamp, toLamp) / (lamp.w * lamp.w));
	if (s >= 1.0) return 0.0;		// out of its reach
	float atten = (1.0 - s) * (1.0 - s) / (1.0 + 5.0 * s);
	return Lobe(N, V, normalize(toLamp), exponent) * atten * saturate(luma(color));
}

float4 BloodPS(PSIN IN) : COLOR0 {
	float2 uv = (IN.vpos + 0.5) * TESR_ReciprocalResolution.xy;
	float viewmodelDepth = tex2D(TESR_DepthBufferViewModel, uv).x;
	if ((TESR_DepthConstants.z == 0 && viewmodelDepth < 0.9) || (TESR_DepthConstants.z > 0 && viewmodelDepth > 0.01)) discard;
	float depth = min(readDepth(uv), farZ * 0.98);
	bool sky = depth >= farZ * 0.979;

	// (Kept to what the streams need: the rest is worked out after them, so the shader fits in its registers.)
	float slot, pool, fresh;
	DecodeSlot(IN.a.w, slot, pool, fresh);
	float3 rel = toWorld(uv) * depth;		// the surface, eye-relative
	float sceneDist = length(rel);
	float3 rd = rel / sceneDist;
	// The surface, item-relative (none on the sky); the eye is at -IN.a.xyz.
	float3 pp = sky ? float3(1e5, 1e5, 1e5) : rel - IN.a.xyz;
	// Rounded: values passed from the vertices differ a little from pixel to pixel, and the hashes would turn that into
	// noise (drips flickering on and off from row to row).
	float seed = floor(IN.c.z + 0.5) / 4096.0;
	// The size of a pixel on the surface here (for smooth edges).
	float aa = max(depth * TESR_ReciprocalResolution.y * 2.0 / TESR_ProjectionTransform[1][1], 0.05);
	float firstRow = slot * Streams;

	// Outside the box round everything this item's blood reaches (the bounds pass), and the eye ray not crossing it
	// either (a drop could be falling in front of the surface): nothing here.
	float boundsV = (firstRow + Streams * 0.5) * TESR_BloodFlowProbe.x;	// its row of the bounds texture (one per slot)
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
		// The ground round the pool (from the probe): on a slope the pool moves downhill and stretches that way.
		float4 groundHead = PathTexel(0.0, firstRow + Streams - 1.0);
		float4 ground = PathTexel(1.0, firstRow + Streams - 1.0);
		bool known = fresh < 0.5 && groundHead.y >= 5.0;
		float3 gN = known ? ground.xyz : float3(0.0, 0.0, 1.0);
		float slope = length(gN.xy) / max(gN.z, 0.3);
		float2 downhill = slope > 1e-3 ? normalize(gN.xy) : float2(1.0, 0.0);
		float R = IN.b.w;
		float grow = sqrt(saturate(1.0 - exp(-IN.c.x * 3.0 / TESR_BloodFlowPool.x)));	// most of the way by the spread time
		float radius = R * grow;
		float2 q = pp.xy - downhill * R * PoolShift(slope) * grow;
		float alongQ = dot(q, downhill);
		float2 shaped = float2(alongQ / (alongQ > 0.0 ? PoolStretch(slope) : 1.0), dot(q, float2(-downhill.y, downhill.x)));
		float fromCentre = length(shaped);
		// On the ground near the plane the probe found (a little more leeway further out).
		float groundZ = (known ? ground.w : 0.0) - (gN.x * pp.x + gN.y * pp.y) / max(gN.z, 0.3);
		bool onGround = fromCentre < R * 1.45 && pp.z < groundZ + 5.0 + 0.15 * fromCentre && pp.z > groundZ - (8.0 + 0.3 * fromCentre);
		[branch] if (onGround && radius > 0.05 && normalize(GetWorldNormalLod(uv)).z > 0.6) {
			// A main body and a lobe off one side, joined smoothly, with a ragged edge from noise fixed to the ground.
			float angle = seed * 6.2832;
			float2 lobeWay = float2(cos(angle), sin(angle));
			float dMain = fromCentre - radius;
			float dLobe = length(shaped - lobeWay * radius * 0.55) - radius * 0.6;
			float kk = radius * 0.3 + 0.01;
			float mixLobe = saturate(0.5 + 0.5 * (dLobe - dMain) / kk);
			float dist = lerp(dLobe, dMain, mixLobe) - kk * mixLobe * (1.0 - mixLobe);
			dist += (fbm(pp.xy * 0.09 + seed * 17.0) - 0.5) * radius * 0.55;
			float cover = saturate(0.5 - dist / aa);
			rim = saturate(1.0 + dist / max(radius * 0.12, 0.8)) * cover;
			float deep = saturate(-dist / max(radius * 0.5, 0.5));
			// The edge bulges (a meniscus): its normal leans outward there.
			float2 outward = length(pp.xy) > 0.001 ? normalize(pp.xy) : float2(0.0, 0.0);
			Add(p, cover, 0.35 + 0.65 * deep, float3(outward * rim * rim * 0.3, 0.0), 2.0);
		}
		// Trickles off the downhill side.
		[loop] for (float k = 0.0; k < 2.0; k += 1.0)
			PaintPath(firstRow + k, PoolStream(seed, k, IN.b.w, slope, TESR_BloodFlowPool.x), pp, aa, IN.c.x, seed, p);
		[loop] for (float k2 = 0.0; k2 < 2.0; k2 += 1.0)
			PaintDrops(firstRow + k2, PoolStream(seed, k2, IN.b.w, slope, TESR_BloodFlowPool.x), pp, -IN.a.xyz, rd, sceneDist, aa, IN.c.x, seed, p);
	}
	else {
		[loop] for (float k = 0.0; k < Streams; k += 1.0)
			PaintPath(firstRow + k, SplatStream(seed, k, IN.b.w, IN.c.w, TESR_BloodFlowInfo.w), pp, aa, IN.c.x, seed, p);
		[loop] for (float k2 = 0.0; k2 < Streams; k2 += 1.0)
			PaintDrops(firstRow + k2, SplatStream(seed, k2, IN.b.w, IN.c.w, TESR_BloodFlowInfo.w), pp, -IN.a.xyz, rd, sceneDist, aa, IN.c.x, seed, p);
	}

	float4 scene = tex2Dlod(TESR_SourceBuffer, float4(uv, 0.0, 0.0));
	float splat = 0.0;		// the game's own splat under it (found by its red)
	if (pool < 0.5) {
		// The game's splat: inside its radius, wherever the image is red.
		float inside = 1.0 - smoothstep(0.7, 1.05, length(pp) / IN.b.w);
		float redness = saturate((scene.r - max(scene.g, scene.b)) / max(scene.r, 0.02) * 1.6 - 0.35);
		splat = inside * redness;
		p.cover *= 1.0 - 0.6 * splat * step(p.thick, 0.5);	// where a trail starts inside the splat, the splat shows
	}

	if (TESR_BloodFlowInfo.y > 0.5) {
		// Debug view. Red: trails; yellow: puddles and pools; blue: a drop at an edge; cyan: where drops land; white:
		// falling drops; green: the rest of the area it looks at; white rings every 10 units round the item's centre.
		if (p.air > 0.5) return float4(1, 1, 1, 0.9);
		if (p.cover > 0.5) {
			float3 c = p.debug < 1.5 ? float3(1, 0, 0) : p.debug < 2.5 ? float3(1, 0.9, 0) : p.debug < 3.5 ? float3(0, 0.3, 1) : float3(0, 1, 1);
			return float4(c, 0.85);
		}
		if (!sky && frac(length(pp) / 10.0) < 0.06) return float4(1, 1, 1, 0.6);
		return float4(0, 0.6, 0, 0.35);
	}
	float amount = saturate(max(max(p.cover, splat), p.air));
	if (amount * IN.c.y < 0.004) discard;

	float3 sceneLin = linearize(max(scene.rgb, 0.0));
	// The light here, from the brightest channel (the game's red splat is dark only in green and blue); capped, so a
	// lamp-lit or white surface cannot make the blood glow.
	float light = min(max(sceneLin.r, max(sceneLin.g, sceneLin.b)) / AssumedAlbedo, 2.5);
	float age = IN.c.x;
	float dryTime = pool > 0.5 ? TESR_BloodFlowInfo.z : TESR_BloodFlowDry.w;
	// Drying: a thin film dries much sooner than a thick layer.
	float dry = smoothstep(0.0, 1.0, age / (dryTime * lerp(0.5, 1.6, p.thick)));
	float3 albedo = lerp(TESR_BloodFlowFresh.rgb, TESR_BloodFlowDry.rgb, dry);
	// Dried blood is uneven: thicker and thinner patches, fixed to the surface.
	albedo *= lerp(1.0, 0.8 + 0.4 * valueNoise((pp.xy + pp.z) * 0.35 + seed * 31.0), dry);
	// A thin film is a stain: the surface shows through, darkened and reddened. A thick layer is the blood's own colour,
	// darker where deep, with a dark ring at a dried pool's edge.
	float3 film = sceneLin * saturate(albedo / AssumedAlbedo);
	float3 layer = light * albedo * lerp(1.0, 0.6, saturate(p.thick * 1.3 - 0.3)) * (1.0 - 0.35 * dry * rim);
	float3 blood = lerp(film, layer, saturate(p.thick * 1.4 - 0.2));
	float3 colour = sceneLin;
	colour = lerp(colour, colour * lerp(1.0, float3(0.5, 0.62, 0.62), dry), splat);	// the game's splat dries too: darker, browner
	colour = lerp(colour, blood, p.cover);

	// Fresh blood shines a little, thick blood most: the sun (outdoors, where it is not shaded) and the lamps, as
	// directions only, at most about the light measured here (no glare, no glow); a faint colourless sheen at low angles.
	float wet = max(p.cover, splat * 0.5);
	float gloss = TESR_BloodFlowFresh.w * (1.0 - dry) * wet * lerp(0.35, 1.0, p.thick);
	[branch] if (gloss > 0.001) {
		float3 V = -rd;
		float3 Nb = normalize(normalize(GetWorldNormalLod(uv)) + p.bend * 0.8);
		float exponent = lerp(300.0, 60.0, dry);
		float lobes = 0.0;
		if (TESR_BloodFlowInfo.x > 0.5) {
			float sunAmbient = max(luma(linearize(TESR_SunAmbient.rgb)), 0.001);
			float4 shadows = tex2Dlod(TESR_PointShadowBuffer, float4(uv, 0.0, 0.0));
			float lit = saturate(pow(abs((shadows.r + shadows.g) / sunAmbient), 5.0));
			lobes += Lobe(Nb, V, normalize(TESR_SunDirection.xyz), exponent) * lit * saturate(luma(linearize(TESR_SunColor.rgb)));
		}
		float3 P = TESR_CameraPosition.xyz + rd * sceneDist;
		[loop] for (int i = 0; i < 12; i++) {
			lobes += LampLobe(Nb, V, P, TESR_ShadowLightPosition[i], TESR_LightColor[i].rgb, exponent);
			lobes += LampLobe(Nb, V, P, TESR_LightPosition[i], TESR_LightColor[i + 12].rgb, exponent);
		}
		float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(dot(Nb, V)), 5.0);
		colour += (min(lobes, 1.0) * 0.55 + 0.05 * fresnel) * light * gloss;
	}
	// A falling drop: a dark red bead with a faint glint, in front of whatever is behind it (lit about as that is).
	float3 drop = light * (TESR_BloodFlowFresh.rgb * 0.7 + float3(0.02, 0.004, 0.004) * TESR_BloodFlowFresh.w);
	colour = lerp(colour, drop, p.air);
	colour = min(colour, 1.0);

	// Blended over the image by `alpha`: the colour is given as what the pixel would be at full strength, so where the
	// image is still what this read, the result is exactly `colour`.
	float alpha = amount * IN.c.y;
	float3 full = (colour - sceneLin * (1.0 - amount)) / max(amount, 0.004);
	return float4(delinearize(max(full, 0.0)), alpha);
}

technique BloodFlow
{
	pass
	{
		VertexShader = compile vs_3_0 BoxVS();
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
