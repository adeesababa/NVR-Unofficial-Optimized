#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ClothWalk {

	constexpr int kRing = 16;
	constexpr double kPi = 3.14159265358979323846;

	struct Ring {
		bool ok = false;
		double c[2] = {};
		double p[kRing][3] = {};
		double a[kRing][3] = {};
		double L[kRing] = {};
		bool present[kRing] = {};
		std::vector<float> u;
		std::vector<float> swing;
	};

	inline bool BuildRing(const float* pos, uint32_t n, const float* weight, const uint8_t* label, const float center[2], float hipZ, Ring& r) {
		r = Ring();
		r.c[0] = center[0]; r.c[1] = center[1];
		r.u.resize(n);
		std::vector<int> sector(n);
		std::vector<double> ud(n);
		for (uint32_t v = 0; v < n; v++) {
			const double ang = std::atan2((double)pos[v * 3 + 1] - r.c[1], (double)pos[v * 3] - r.c[0]);
			double u = std::fmod(ang / (2.0 * kPi) * kRing - 0.5, (double)kRing); if (u < 0.0) u += kRing;
			r.u[v] = (float)u; ud[v] = u;
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
		for (int i = 0; i < kRing; i++) {
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
		double zt[kRing]; bool hasT[kRing] = {};
		for (int i = 0; i < kRing; i++) {
			zt[i] = -1e30;
			for (uint32_t v = 0; v < n; v++) if (sector[v] == i && weight[v] > 0.05f) { zt[i] = (std::max)(zt[i], (double)pos[v * 3 + 2]); hasT[i] = true; }
		}
		bool anyT = false;
		for (int i = 0; i < kRing; i++) anyT = anyT || hasT[i];
		for (int i = 0; i < kRing; i++) {
			if (!anyT) { zt[i] = hipZ; continue; }
			if (hasT[i]) continue;
			int a = -1, b = -1, da = 99, db = 99;
			for (int j = 0; j < kRing; j++) {
				if (!hasT[j]) continue;
				const int up = ((j - i) % kRing + kRing) % kRing, down = ((i - j) % kRing + kRing) % kRing;
				if (up < da) { da = up; a = j; }
				if (down < db) { db = down; b = j; }
			}
			zt[i] = zt[b] + (zt[a] - zt[b]) * ((double)db / (std::max)(da + db, 1));
		}
		r.swing.resize(n);
		for (uint32_t v = 0; v < n; v++) {
			const double i0 = std::floor(ud[v]), f = ud[v] - i0;
			const int a = (int)i0 % kRing, b = (a + 1) % kRing;
			const double zr = z[a] * (1.0 - f) + z[b] * f;
			const double ztu = zt[a] * (1.0 - f) + zt[b] * f;
			double share = (ztu - pos[v * 3 + 2]) / (std::max)(ztu - zr, 1.0);
			share = share < 0.0 ? 0.0 : (share > 1.2 ? 1.2 : share);
			if (share <= 1.0) share = share * share * (2.0 - share);
			double wf = (double)weight[v] / 0.2; wf = wf < 0.0 ? 0.0 : (wf > 1.0 ? 1.0 : wf); wf = wf * wf * (3.0 - 2.0 * wf);
			r.swing[v] = (float)(share * wf);
		}
		r.ok = true;
		return true;
	}

	struct Params {
		double spring = 1.4, damping = 0.2, drag = 1.0, legMargin = 1.5, maxSwing = 25.0;
		double dragCap = 180.0, maxAngle = 35.0;
		double hemSmooth = 1.0;
		double stretch = 1.03, neighbour = 0.5;
		int iterations = 2;
	};
	struct Xf { double r[3][3]; double t[3]; };
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

	inline void Step(State& s, const Ring& r, const Xf& pelvis, const double legs[6][4], double dt, const Params& prm, double out[kRing][3]) {
		double tNow[kRing][3], A[kRing][3];
		for (int i = 0; i < kRing; i++) { Apply(pelvis, r.p[i], tNow[i]); Apply(pelvis, r.a[i], A[i]); }
		if (!s.valid || dt <= 0.0 || dt > 0.25) {
			for (int i = 0; i < kRing; i++) for (int k = 0; k < 3; k++) { s.x[i][k] = tNow[i][k]; s.v[i][k] = 0.0; s.tPrev[i][k] = tNow[i][k]; out[i][k] = 0.0; }
			s.valid = true;
			return;
		}
		const double w0 = 2.0 * kPi * prm.spring, c = 2.0 * prm.damping * w0;
		const double cmax = std::cos(prm.maxAngle * kPi / 180.0), smax = std::sin(prm.maxAngle * kPi / 180.0);
		double vt[kRing][3], vair[kRing][3], rest[kRing];
		for (int i = 0; i < kRing; i++) {
			for (int k = 0; k < 3; k++) vt[i][k] = (tNow[i][k] - s.tPrev[i][k]) / dt;
			const double vs = std::sqrt(vt[i][0] * vt[i][0] + vt[i][1] * vt[i][1] + vt[i][2] * vt[i][2]);
			const double sat = 1.0 / std::sqrt(1.0 + (vs / prm.dragCap) * (vs / prm.dragCap));
			for (int k = 0; k < 3; k++) vair[i][k] = vt[i][k] * sat;
			const int j = (i + 1) % kRing;
			const double e[3] = { tNow[i][0] - tNow[j][0], tNow[i][1] - tNow[j][1], tNow[i][2] - tNow[j][2] };
			rest[i] = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
		}
		const int n = (std::min)((int)std::ceil(dt * 120.0), 8);
		const double h = dt / n;
		static const int segs[4][2] = { { 0, 1 }, { 1, 2 }, { 3, 4 }, { 4, 5 } };
		for (int st = 0; st < n; st++) {
			const double f = (double)(st + 1) / n;
			double xOld[kRing][3], ts[kRing][3];
			for (int i = 0; i < kRing; i++)
				for (int k = 0; k < 3; k++) {
					ts[i][k] = s.tPrev[i][k] + (tNow[i][k] - s.tPrev[i][k]) * f;
					s.v[i][k] += h * (w0 * w0 * (ts[i][k] - s.x[i][k]) - c * (s.v[i][k] - vt[i][k]) - c * prm.drag * vair[i][k]);
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
				for (int i = 0; i < kRing; i++) {
					const double e[3] = { s.x[i][0] - A[i][0], s.x[i][1] - A[i][1], s.x[i][2] - A[i][2] };
					const double rr[3] = { ts[i][0] - A[i][0], ts[i][1] - A[i][1], ts[i][2] - A[i][2] };
					const double le = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]), lr = std::sqrt(rr[0] * rr[0] + rr[1] * rr[1] + rr[2] * rr[2]);
					if (le < 1e-6 || lr < 1e-6) continue;
					const double rh[3] = { rr[0] / lr, rr[1] / lr, rr[2] / lr };
					const double ca = (e[0] * rh[0] + e[1] * rh[1] + e[2] * rh[2]) / le;
					if (ca < cmax) {
						const double pp[3] = { e[0] / le - ca * rh[0], e[1] / le - ca * rh[1], e[2] / le - ca * rh[2] };
						const double pl = std::sqrt(pp[0] * pp[0] + pp[1] * pp[1] + pp[2] * pp[2]);
						if (pl > 1e-6) for (int q = 0; q < 3; q++) s.x[i][q] = A[i][q] + le * (cmax * rh[q] + smax * pp[q] / pl);
					}
				}
			}
			for (int i = 0; i < kRing; i++) for (int k = 0; k < 3; k++) s.v[i][k] = (s.x[i][k] - xOld[i][k]) / h;
		}
		for (int i = 0; i < kRing; i++) {
			for (int k = 0; k < 3; k++) { s.tPrev[i][k] = tNow[i][k]; out[i][k] = s.x[i][k] - tNow[i][k]; }
			const double l = std::sqrt(out[i][0] * out[i][0] + out[i][1] * out[i][1] + out[i][2] * out[i][2]);
			if (l > 1e-6 && prm.maxSwing > 0.0) { const double m = prm.maxSwing * std::tanh(l / prm.maxSwing) / l; for (int k = 0; k < 3; k++) out[i][k] *= m; }
		}
		if (prm.hemSmooth > 0.0) {
			double mean[3] = {}, A[3] = {}, B[3] = {}, cs[kRing], sn[kRing];
			for (int i = 0; i < kRing; i++) {
				cs[i] = std::cos(2.0 * kPi * i / kRing); sn[i] = std::sin(2.0 * kPi * i / kRing);
				for (int k = 0; k < 3; k++) { mean[k] += out[i][k] / kRing; A[k] += out[i][k] * cs[i]; B[k] += out[i][k] * sn[i]; }
			}
			for (int i = 0; i < kRing; i++)
				for (int k = 0; k < 3; k++) {
					const double low = mean[k] + (cs[i] * A[k] + sn[i] * B[k]) / kRing;
					out[i][k] += (low - out[i][k]) * prm.hemSmooth;
				}
		}
	}
}
