#pragma once

#include <cfloat>
#include <vector>
#include "FrameTimeStats.h"

inline bool GpuProfileQueriesReady(HRESULT start, HRESULT end, HRESULT frequency) {
	return start == S_OK && end == S_OK && frequency == S_OK;
}

class GpuTimer {
public:
	explicit GpuTimer(const char* name, unsigned reportSamples = 120)
		: Name(name), ReportSamples(reportSamples) {}

	~GpuTimer() { ReleaseQueries(); }

	bool Begin(IDirect3DDevice9* device) {
		if (!device) return false;
#ifndef NVR_GPU_PROFILER_TEST
		if (!Enabled || InterfaceManager->IsActive(Menu::kMenuType_Loading)) return false;
#endif
		if (Active) return false;
		if (device != Device) {
			ReleaseQueries();
			Device = device;
			Unavailable = false;
		}
		if (Unavailable || (!QueriesCreated && !CreateQueries())) return false;

		CollectCompleted();
		if (Unavailable) return false;
		WarnIfNothingCompletes();
		for (auto& slot : Slots) {
			if (slot.Pending) continue;
			HRESULT hr = slot.Disjoint ? slot.Disjoint->Issue(D3DISSUE_BEGIN) : S_OK;
			if (SUCCEEDED(hr)) hr = slot.Start->Issue(D3DISSUE_END);
			if (FAILED(hr)) {
				Disable("could not issue the start timestamp", hr);
				return false;
			}
			Active = &slot;
			return true;
		}
		return false;
	}

	void End() {
		if (!Active) return;
		Slot* slot = Active;
		Active = nullptr;
		HRESULT hr = slot->End->Issue(D3DISSUE_END);
		if (SUCCEEDED(hr) && slot->Frequency) hr = slot->Frequency->Issue(D3DISSUE_END);
		if (SUCCEEDED(hr) && slot->Disjoint) hr = slot->Disjoint->Issue(D3DISSUE_END);
		if (FAILED(hr)) {
			Disable("could not issue the end timestamp", hr);
			return;
		}
		slot->Pending = true;
		Issued++;
	}

#ifdef NVR_GPU_PROFILER_TEST
	unsigned GetSampleCount() const { return TotalSamples; }
	unsigned GetRejectedCount() const { return Rejected; }
	bool UsingFallbackClock() const { return FrequencyBroken; }
	bool DisjointTrusted() const { return !DisjointBroken; }
#endif
	inline static bool Enabled = false;

private:
	struct Slot {
		IDirect3DQuery9* Start = nullptr;
		IDirect3DQuery9* End = nullptr;
		IDirect3DQuery9* Frequency = nullptr;
		IDirect3DQuery9* Disjoint = nullptr;
		bool Pending = false;
	};

	static constexpr unsigned RingSize = 12;
	static constexpr UINT64 FallbackFrequency = 1000000000ull;
	Slot Slots[RingSize];
	IDirect3DDevice9* Device = nullptr;
	Slot* Active = nullptr;
	const char* Name;
	unsigned ReportSamples;
	unsigned WindowSamples = 0;
	unsigned TotalSamples = 0;
	unsigned ZeroTickSamples = 0;
	unsigned Issued = 0;
	unsigned Rejected = 0;
	double SumMs = 0.0;
	double MinMs = DBL_MAX;
	double MaxMs = 0.0;
	bool Unavailable = false;
	bool QueriesCreated = false;
	bool DisjointBroken = false;
	bool FrequencyBroken = false;
	bool WarnedNothing = false;
	bool WarnedRejected = false;

	void Report(const char* what, HRESULT hr) {
#ifndef NVR_GPU_PROFILER_TEST
		static unsigned reported = 0;
		if (reported++ < 16)
			Logger::Log("GPU PROFILE '%s': %s (hr=%08lX)", Name, what, (unsigned long)hr);
#else
		(void)what; (void)hr;
#endif
	}

	static void Release(IDirect3DQuery9*& query) {
		if (query) query->Release();
		query = nullptr;
	}

	void ReleaseQueries() {
		Active = nullptr;
		for (auto& slot : Slots) {
			Release(slot.Start);
			Release(slot.End);
			Release(slot.Frequency);
			Release(slot.Disjoint);
			slot.Pending = false;
		}
		QueriesCreated = false;
	}

	bool CreateQueries() {
		for (auto& slot : Slots) {
			slot.Start = slot.End = slot.Frequency = slot.Disjoint = nullptr;
			HRESULT hr = Device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &slot.Start);
			if (SUCCEEDED(hr)) hr = Device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &slot.End);
			if (FAILED(hr)) {
#ifndef NVR_GPU_PROFILER_TEST
				static bool reported = false;
				if (!reported) {
					Logger::Log("GPU PROFILE unavailable: this D3D9 device does not support timestamp queries (hr=%08lX).", (unsigned long)hr);
					reported = true;
				}
#endif
				Disable(nullptr, hr);
				return false;
			}
			if (FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &slot.Frequency))) { slot.Frequency = nullptr; FrequencyBroken = true; }
			if (FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &slot.Disjoint))) { slot.Disjoint = nullptr; DisjointBroken = true; }
		}
		if (DisjointBroken || FrequencyBroken)
			Report(DisjointBroken && FrequencyBroken ? "TIMESTAMPDISJOINT and TIMESTAMPFREQ queries are missing; timing without a disjoint check, assuming a 1 GHz clock"
				: DisjointBroken ? "TIMESTAMPDISJOINT query is missing; timing without a disjoint check"
				: "TIMESTAMPFREQ query is missing; assuming a 1 GHz clock", S_OK);
		QueriesCreated = true;
		return true;
	}

	void Disable(const char* why, HRESULT hr) {
		if (why) Report(why, hr);
		ReleaseQueries();
		Unavailable = true;
	}

	void WarnIfNothingCompletes() {
		if (WarnedNothing || Issued < 480 || TotalSamples || Rejected) return;
		WarnedNothing = true;
		Report("480 timestamp pairs issued but none has completed (GetData keeps answering 'not ready')", S_FALSE);
	}

	void CollectCompleted() {
		for (auto& slot : Slots) {
			if (!slot.Pending) continue;

			BOOL disjoint = FALSE;
			if (slot.Disjoint && !DisjointBroken) {
				const HRESULT disjointReady = slot.Disjoint->GetData(&disjoint, sizeof(disjoint), 0);
				if (disjointReady == S_FALSE) continue;
				if (disjointReady != S_OK) {
					DisjointBroken = true;
					disjoint = FALSE;
					Report("TIMESTAMPDISJOINT GetData failed; timing without a disjoint check from now on", disjointReady);
				}
			}

			UINT64 start = 0, end = 0, frequency = FallbackFrequency;
			const HRESULT startReady = slot.Start->GetData(&start, sizeof(start), 0);
			const HRESULT endReady = slot.End->GetData(&end, sizeof(end), 0);
			HRESULT frequencyReady = S_OK;
			if (slot.Frequency && !FrequencyBroken) {
				frequency = 0;
				frequencyReady = slot.Frequency->GetData(&frequency, sizeof(frequency), 0);
				if (FAILED(frequencyReady)) {
					const HRESULT failure = frequencyReady;
					FrequencyBroken = true;
					frequency = FallbackFrequency;
					frequencyReady = S_OK;
					Report("TIMESTAMPFREQ GetData failed; assuming a 1 GHz clock from now on", failure);
				}
			}
			if (FAILED(startReady) || FAILED(endReady)) {
				Disable("timestamp GetData failed", FAILED(startReady) ? startReady : endReady);
				return;
			}
			if (!GpuProfileQueriesReady(startReady, endReady, frequencyReady)) continue;
			slot.Pending = false;
			if (disjoint || !frequency || end < start) {
				if (++Rejected == 50 && !TotalSamples && !WarnedRejected) {
					WarnedRejected = true;
#ifndef NVR_GPU_PROFILER_TEST
					Logger::Log("GPU PROFILE '%s': 50 results discarded and none used (disjoint=%d, frequency=%llu, start=%llu, end=%llu)",
						Name, (int)disjoint, (unsigned long long)frequency, (unsigned long long)start, (unsigned long long)end);
#endif
				}
				continue;
			}

			const double ms = (double)(end - start) * 1000.0 / (double)frequency;
			SumMs += ms;
			MinMs = MinMs < ms ? MinMs : ms;
			MaxMs = MaxMs > ms ? MaxMs : ms;
			WindowSamples++;
			TotalSamples++;
			if (end == start) ZeroTickSamples++;

			if (WindowSamples >= ReportSamples) {
#ifdef NVR_GPU_PROFILER_TEST
				std::printf("GPU PROFILE %-24s avg %.4f ms  min %.4f  max %.4f  (%u samples, %u zero)\n",
					Name, SumMs / WindowSamples, MinMs, MaxMs, WindowSamples, ZeroTickSamples);
#else
				Logger::Log("GPU PROFILE %-24s avg %.4f ms  min %.4f  max %.4f  (%u samples, %u zero)",
					Name, SumMs / WindowSamples, MinMs, MaxMs, WindowSamples, ZeroTickSamples);
#endif
				WindowSamples = 0;
				SumMs = 0.0;
				MinMs = DBL_MAX;
				MaxMs = 0.0;
				ZeroTickSamples = 0;
			}
		}
	}
};

class GpuProfileScope {
public:
	GpuProfileScope(GpuTimer& timer, IDirect3DDevice9* device) : Timer(timer), Active(timer.Begin(device)) {}
	~GpuProfileScope() { if (Active) Timer.End(); }

private:
	GpuTimer& Timer;
	bool Active;
};

class CpuTimer {
public:
	explicit CpuTimer(const char* name, unsigned reportSamples = 120)
		: Name(name), ReportSamples(reportSamples) { Registry().push_back(this); }

	static std::vector<CpuTimer*>& Registry() { static std::vector<CpuTimer*> timers; return timers; }
	static unsigned& FrameStamp() { static unsigned stamp = 0; return stamp; }

	const char* GetName() const { return Name; }
	double LastMs() const { return Last; }
	bool RanRecently() const { return LastStamp + 1 >= FrameStamp(); }

	void Add(double ms) {
		Last = ms;
		LastStamp = FrameStamp();
		SumMs += ms;
		MinMs = MinMs < ms ? MinMs : ms;
		MaxMs = MaxMs > ms ? MaxMs : ms;
		if (++WindowSamples < ReportSamples) return;
#ifndef NVR_GPU_PROFILER_TEST
		Logger::Log("CPU PROFILE %-24s avg %.4f ms  min %.4f  max %.4f  (%u samples)",
			Name, SumMs / WindowSamples, MinMs, MaxMs, WindowSamples);
#endif
		WindowSamples = 0;
		SumMs = 0.0;
		MinMs = DBL_MAX;
		MaxMs = 0.0;
	}

	static double NowMs() {
		static LARGE_INTEGER frequency = {};
		if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return (double)now.QuadPart * 1000.0 / (double)frequency.QuadPart;
	}

	double Tick() {
		const double now = NowMs();
		double interval = 0.0;
		if (LastTick > 0.0 && now - LastTick < 1000.0) { interval = now - LastTick; FrameStamp()++; Add(interval); }
		LastTick = now;
		return interval;
	}

private:
	const char* Name;
	unsigned ReportSamples;
	unsigned WindowSamples = 0;
	double SumMs = 0.0;
	double MinMs = DBL_MAX;
	double MaxMs = 0.0;
	double LastTick = 0.0;
	double Last = 0.0;
	unsigned LastStamp = 0;
};

class FrameTimeMonitor {
public:
	static constexpr unsigned WindowFrames = 1200;

	void Add(double ms, bool nearTransition) {
		All.push_back(ms);
		if (!nearTransition) Steady.push_back(ms);

		if (Typical > 0.0 && ms > FrameSpikeThresholdMs(Typical)) ReportSpike(ms, nearTransition);
		const double sample = Typical > 0.0 && ms > Typical * 2.0 ? Typical * 2.0 : ms;
		Typical = Typical > 0.0 ? Typical * 0.98 + sample * 0.02 : ms;

		if (All.size() >= WindowFrames) Report();
	}

	inline static void (*SpikeDetail)(char* buffer, size_t size) = nullptr;

	void Flush() {
		if (All.size() >= 200) Report();
		All.clear();
		Steady.clear();
		Typical = 0.0;
	}

private:
	std::vector<double> All, Steady;
	double Typical = 0.0;
	unsigned SpikesLogged = 0;

	void ReportSpike(double ms, bool nearTransition) {
#ifndef NVR_GPU_PROFILER_TEST
		if (SpikesLogged >= 60) return;
		SpikesLogged++;
		char slow[256] = {};
		size_t used = 0;
		for (CpuTimer* timer : CpuTimer::Registry()) {
			if (!timer->RanRecently() || timer->LastMs() < 0.75 || !strcmp(timer->GetName(), "Frame interval (CPU)")) continue;
			const int written = _snprintf_s(slow + used, sizeof(slow) - used, _TRUNCATE, "%s%s %.2f ms", used ? ", " : "", timer->GetName(), timer->LastMs());
			if (written < 0) break;
			used += written;
		}
		char detail[512] = {};
		if (SpikeDetail) SpikeDetail(detail, sizeof(detail));
		Logger::Log("FRAME SPIKE %.1f ms (typical %.1f ms) %s | NVR CPU timers over 0.75 ms in it: %s%s",
			ms, Typical, nearTransition ? "near a cell change or loading" : "during steady play", used ? slow : "none", detail);
#else
		(void)ms; (void)nearTransition;
#endif
	}

	void Report() {
#ifndef NVR_GPU_PROFILER_TEST
		std::vector<double> all = All;
		const FrameTimeSummary s = SummarizeFrameTimes(all);
		const unsigned spikes = CountFrameSpikes(all, s.p50Ms);
		Logger::Log("FRAME TIMES %u frames: avg %.2f ms (%.1f fps) | p50 %.2f  p95 %.2f  p99 %.2f  p99.9 %.2f  max %.2f ms | 1%% low %.1f fps  0.1%% low %.1f fps | spikes %u",
			s.frames, s.avgMs, s.avgMs > 0 ? 1000.0 / s.avgMs : 0.0, s.p50Ms, s.p95Ms, s.p99Ms, s.p999Ms, s.maxMs, s.low1Fps, s.low01Fps, spikes);
		if (Steady.size() >= 100 && Steady.size() < All.size()) {
			std::vector<double> steady = Steady;
			const FrameTimeSummary t = SummarizeFrameTimes(steady);
			Logger::Log("  steady play only (%u frames, %u within 3 s of a cell change or loading left out): p99 %.2f ms  1%% low %.1f fps  0.1%% low %.1f fps  max %.2f ms | spikes %u",
				t.frames, s.frames - t.frames, t.p99Ms, t.low1Fps, t.low01Fps, t.maxMs, CountFrameSpikes(steady, t.p50Ms));
		}
#endif
		All.clear();
		Steady.clear();
	}
};

inline FrameTimeMonitor& TheFrameTimeMonitor() { static FrameTimeMonitor monitor; return monitor; }

class CpuProfileScope {
public:
	explicit CpuProfileScope(CpuTimer& timer) : Timer(timer), Active(GpuTimer::Enabled),
		Start(Active ? CpuTimer::NowMs() : 0.0) {}
	~CpuProfileScope() { if (Active) Timer.Add(CpuTimer::NowMs() - Start); }

private:
	CpuTimer& Timer;
	bool Active;
	double Start;
};
