#pragma once

// UNOFFICIAL 2026-10-20 cloth round 9 (user: 'can our cloth have some sort of faux physics simulation when we walk? right now it still
// kinda just follows our player'): a small simulation of a worn coat's hem, per wearer, on the CPU. No game types here, so
// tests\cloth_walk_test.cpp runs the same code against the offline prototype (bench\deep-roadmap\evidence\cloth3\cloth_walk_check.py,
// which this mirrors line by line; evidence\cloth3-LOG.md C3-15).
//
// Why the coat 'follows': the garment's own skin weights glue the skirt to the leg bones (the Combat Ranger coat at z 45-60 is 88 %
// thigh bones, at z 15-30 78 % calf bones), so it moves like trousers; the old walking part only added one offset for the whole coat.
// ClothWind.h makes the coat HANG FROM THE HIPS (each coat vertex blended from its skinned place towards the pelvis bone's alone, by its
// cloth weight) and moves it by this ring:
//  - 16 points around the wearer's upright axis (one per 22.5 degrees), each at the coat's loosest vertices in that direction (missing
//    directions -- an open front -- are filled in from their neighbours); each hangs from a point halfway in at hip height;
//  - every frame each point follows its spot on the hips-hung coat through a damped spring (Spring Hz, Damping; Drag: how much of the
//    damping acts on the speed through the air, so the hem trails behind when walking), in steps of at most 1/120 s, then is pushed out
//    of the CURRENT legs (thigh and calf capsules, the garment's measured radius + LegMargin), keeps half-way to its rest distance from
//    the next point, and may not hang further than 3 % past its length from its hip point;
//  - the displacement of each point from its spot (at most MaxSwing units) is what the shader adds, by angle and cloth weight.
// So the knee kicks the hem forward at every step, the calf pushes it back, the hips' sway swings it, it trails, and it swings past
// and settles after a stop.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ClothWalk {

	constexpr int kRing = 16;
	constexpr double kPi = 3.14159265358979323846;

	struct Ring {
		bool ok = false;
		double c[2] = {};					// the wearer's upright axis (bind pose, between the hip joints)
		double p[kRing][3] = {};			// the hem points (bind pose)
		double a[kRing][3] = {};			// what each hangs from (halfway in, at hip height)
		double L[kRing] = {};				// their lengths
		bool present[kRing] = {};
		std::vector<float> u;				// per vertex: its place around the ring (point i at u = i; 0..16)
	};

	// pos: xyz per vertex (bind pose); weight / label: ClothWeights.h's (label 2 = cloth)
	inline bool BuildRing(const float* pos, uint32_t n, const float* weight, const uint8_t* label, const float center[2], float hipZ, Ring& r) {
		r = Ring();
		r.c[0] = center[0]; r.c[1] = center[1];
		r.u.resize(n);
		std::vector<int> sector(n);
		for (uint32_t v = 0; v < n; v++) {
			const double ang = std::atan2((double)pos[v * 3 + 1] - r.c[1], (double)pos[v * 3] - r.c[0]);
			double u = std::fmod(ang / (2.0 * kPi) * kRing - 0.5, (double)kRing); if (u < 0.0) u += kRing;
			r.u[v] = (float)u;
			double s = std::fmod(ang / (2.0 * kPi) * kRing, (double)kRing); if (s < 0.0) s += kRing;
			sector[v] = (int)std::floor(s) % kRing;
		}
		double rad[kRing] = {}, z[kRing] = {};
		for (int i = 0; i < kRing; i++) {
			double mx = -1.0; int count = 0;
			for (uint32_t v = 0; v < n; v++) if (sector[v] == i && label[v] == 2 && weight[v] > 0.3f) { count++; mx = (std::max)(mx, (double)weight[v]); }
			if (count < 3) continue;
			double q[3] = {}; int m = 0;
			for (uint32_t v = 0; v < n; v++)
				if (sector[v] == i && label[v] == 2 && weight[v] > 0.3f && weight[v] >= 0.95 * mx) { for (int k = 0; k < 3; k++) q[k] += pos[v * 3 + k]; m++; }
			for (double& x : q) x /= m;
			rad[i] = std::hypot(q[0] - r.c[0], q[1] - r.c[1]); z[i] = q[2]; r.present[i] = true;
		}
		bool any = false;
		for (int i = 0; i < kRing; i++) any = any || r.present[i];
		if (!any) return false;
		for (int i = 0; i < kRing; i++) {	// missing directions (an open front): between the nearest present ones
			if (r.present[i]) continue;
			int a = -1, b = -1, da = 99, db = 99;
			for (int j = 0; j < kRing; j++) {
				if (!r.present[j]) continue;
				const int up = ((j - i) % kRing + kRing) % kRing, down = ((i - j) % kRing + kRing) % kRing;
				if (up < da) { da = up; a = j; }
				if (down < db) { db = down; b = j; }
			}
			const double t = (double)db / (std::max)(da + db, 1);
			rad[i] = rad[b] + (rad[a] - rad[b]) * t; z[i] = z[b] + (z[a] - z[b]) * t;
		}
		for (int i = 0; i < kRing; i++) {
			const double th = (i + 0.5) * 2.0 * kPi / kRing;
			r.p[i][0] = r.c[0] + rad[i] * std::cos(th); r.p[i][1] = r.c[1] + rad[i] * std::sin(th); r.p[i][2] = z[i];
			r.a[i][0] = r.c[0] + 0.5 * rad[i] * std::cos(th); r.a[i][1] = r.c[1] + 0.5 * rad[i] * std::sin(th); r.a[i][2] = hipZ;
			const double d[3] = { r.p[i][0] - r.a[i][0], r.p[i][1] - r.a[i][1], r.p[i][2] - r.a[i][2] };
			r.L[i] = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		}
		r.ok = true;
		return true;
	}

	struct Params {
		double spring = 1.4, damping = 0.2, drag = 1.0, legMargin = 1.5, maxSwing = 30.0;
		double stretch = 1.03, neighbour = 0.5;
		int iterations = 2;
	};
	struct Xf { double r[3][3]; double t[3]; };	// world = r q + t (r includes the scale)
	inline void Apply(const Xf& x, const double* q, double* o) {
		for (int i = 0; i < 3; i++) o[i] = x.r[i][0] * q[0] + x.r[i][1] * q[1] + x.r[i][2] * q[2] + x.t[i];
	}
	struct State {
		bool valid = false;
		double x[kRing][3] = {}, v[kRing][3] = {}, tPrev[kRing][3] = {};
	};

	inline void SegPush(double* x, const double* a, const double* b, double ra, double rb, double margin) {
		const double ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const double l2 = (std::max)(ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2], 1e-6);
		double t = ((x[0] - a[0]) * ab[0] + (x[1] - a[1]) * ab[1] + (x[2] - a[2]) * ab[2]) / l2;
		t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
		const double q[3] = { a[0] + ab[0] * t, a[1] + ab[1] * t, a[2] + ab[2] * t };
		const double v[3] = { x[0] - q[0], x[1] - q[1], x[2] - q[2] };
		const double d = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		const double R = ra + (rb - ra) * t + margin;
		if (d < R && d > 1e-6) for (int k = 0; k < 3; k++) x[k] = q[k] + v[k] / d * R;
	}

	// legs: L hip, knee, ankle, R hip, knee, ankle (world xyz, measured radius). out: each point's displacement (world).
	inline void Step(State& s, const Ring& r, const Xf& pelvis, const double legs[6][4], double dt, const Params& prm, double out[kRing][3]) {
		double tNow[kRing][3], A[kRing][3];
		for (int i = 0; i < kRing; i++) { Apply(pelvis, r.p[i], tNow[i]); Apply(pelvis, r.a[i], A[i]); }
		if (!s.valid || dt <= 0.0 || dt > 0.25) {
			for (int i = 0; i < kRing; i++) for (int k = 0; k < 3; k++) { s.x[i][k] = tNow[i][k]; s.v[i][k] = 0.0; s.tPrev[i][k] = tNow[i][k]; out[i][k] = 0.0; }
			s.valid = true;
			return;
		}
		const double w0 = 2.0 * kPi * prm.spring, c = 2.0 * prm.damping * w0;
		double vt[kRing][3], rest[kRing];
		for (int i = 0; i < kRing; i++) {
			for (int k = 0; k < 3; k++) vt[i][k] = (tNow[i][k] - s.tPrev[i][k]) / dt;
			const int j = (i + 1) % kRing;
			const double e[3] = { tNow[i][0] - tNow[j][0], tNow[i][1] - tNow[j][1], tNow[i][2] - tNow[j][2] };
			rest[i] = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
		}
		const int n = (std::min)((int)std::ceil(dt * 120.0), 8);
		const double h = dt / n;
		static const int segs[4][2] = { { 0, 1 }, { 1, 2 }, { 3, 4 }, { 4, 5 } };
		for (int st = 0; st < n; st++) {
			const double f = (double)(st + 1) / n;
			double xOld[kRing][3];
			for (int i = 0; i < kRing; i++)
				for (int k = 0; k < 3; k++) {
					const double ts = s.tPrev[i][k] + (tNow[i][k] - s.tPrev[i][k]) * f;
					s.v[i][k] += h * (w0 * w0 * (ts - s.x[i][k]) - c * (s.v[i][k] - (1.0 - prm.drag) * vt[i][k]));
					xOld[i][k] = s.x[i][k];
					s.x[i][k] += s.v[i][k] * h;
				}
			for (int it = 0; it < prm.iterations; it++) {
				for (int i = 0; i < kRing; i++)
					for (const auto& sg : segs) SegPush(s.x[i], legs[sg[0]], legs[sg[1]], legs[sg[0]][3], legs[sg[1]][3], prm.legMargin);
				for (int i = 0; i < kRing; i++) {
					const int j = (i + 1) % kRing;
					const double e[3] = { s.x[j][0] - s.x[i][0], s.x[j][1] - s.x[i][1], s.x[j][2] - s.x[i][2] };
					const double l = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
					if (l > 1e-6) {
						const double k = (l - rest[i]) / l * 0.5 * prm.neighbour;
						for (int q = 0; q < 3; q++) { s.x[i][q] += k * e[q]; s.x[j][q] -= k * e[q]; }
					}
				}
				for (int i = 0; i < kRing; i++) {
					const double e[3] = { s.x[i][0] - A[i][0], s.x[i][1] - A[i][1], s.x[i][2] - A[i][2] };
					const double l = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
					if (l > prm.stretch * r.L[i]) for (int q = 0; q < 3; q++) s.x[i][q] = A[i][q] + e[q] * (prm.stretch * r.L[i] / l);
				}
			}
			for (int i = 0; i < kRing; i++) for (int k = 0; k < 3; k++) s.v[i][k] = (s.x[i][k] - xOld[i][k]) / h;
		}
		for (int i = 0; i < kRing; i++) {
			for (int k = 0; k < 3; k++) { s.tPrev[i][k] = tNow[i][k]; out[i][k] = s.x[i][k] - tNow[i][k]; }
			const double l = std::sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1] + out[i][2] * out[i][2]);
			if (l > prm.maxSwing) for (int k = 0; k < 3; k++) out[i][k] *= prm.maxSwing / l;
		}
	}
}
