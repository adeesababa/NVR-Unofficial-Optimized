#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

struct FrameTimeSummary {
	unsigned frames = 0;
	double avgMs = 0, p50Ms = 0, p95Ms = 0, p99Ms = 0, p999Ms = 0, maxMs = 0;
	double low1Fps = 0, low01Fps = 0;
};

inline double FrameSpikeThresholdMs(double typicalMs) { const double ratio = typicalMs * 2.5, floor = typicalMs + 8.0; return ratio > floor ? ratio : floor; }

inline FrameTimeSummary SummarizeFrameTimes(std::vector<double>& ms)
{
	FrameTimeSummary s;
	if (ms.empty()) return s;
	std::sort(ms.begin(), ms.end());
	const size_t n = ms.size();
	s.frames = (unsigned)n;
	double sum = 0;
	for (double v : ms) sum += v;
	s.avgMs = sum / n;
	auto rank = [&](double q) { size_t r = (size_t)std::ceil(q * n); if (r < 1) r = 1; if (r > n) r = n; return ms[r - 1]; };
	s.p50Ms = rank(0.50);
	s.p95Ms = rank(0.95);
	s.p99Ms = rank(0.99);
	s.p999Ms = rank(0.999);
	s.maxMs = ms.back();
	auto lowFps = [&](double fraction) {
		size_t count = (size_t)std::ceil(fraction * n);
		if (count < 1) count = 1;
		if (count > n) count = n;
		double worst = 0;
		for (size_t i = n - count; i < n; i++) worst += ms[i];
		return worst > 0 ? 1000.0 / (worst / count) : 0.0;
	};
	s.low1Fps = lowFps(0.01);
	s.low01Fps = lowFps(0.001);
	return s;
}

inline unsigned CountFrameSpikes(const std::vector<double>& ms, double typicalMs)
{
	const double threshold = FrameSpikeThresholdMs(typicalMs);
	unsigned count = 0;
	for (double v : ms) if (v > threshold) count++;
	return count;
}
