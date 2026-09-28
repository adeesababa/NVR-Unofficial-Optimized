#pragma once

#include <cfloat>

inline bool GpuProfileQueriesReady(HRESULT start, HRESULT end, HRESULT frequency) {
	return start == S_OK && end == S_OK && frequency == S_OK;
}

// Asynchronous D3D9 GPU timestamps. GetData is deliberately never called with
// D3DGETDATA_FLUSH: profiling must not serialize the CPU and GPU or change the
// workload we are trying to measure.
class GpuTimer {
public:
	explicit GpuTimer(const char* name, unsigned reportSamples = 120)
		: Name(name), ReportSamples(reportSamples) {}

	~GpuTimer() { ReleaseQueries(); }

	bool Begin(IDirect3DDevice9* device) {
		if (!device) return false;
#ifndef NVR_GPU_PROFILER_TEST
		// The diagnostic is armed explicitly after loading. F10 toggles it from
		// ShaderManager; no query objects are created during initial loading.
		if (!Enabled || InterfaceManager->IsActive(Menu::kMenuType_Loading)) return false;
#endif
		if (Active) return false;
		if (device != Device) {
			ReleaseQueries();
			Device = device;
			Unavailable = false;
		}
		if (Unavailable || (!Slots[0].Disjoint && !CreateQueries())) return false;

		CollectCompleted();
		// Collection can release the entire ring after a device/query error.
		if (Unavailable) return false;
		for (auto& slot : Slots) {
			if (slot.Pending) continue;
			if (FAILED(slot.Disjoint->Issue(D3DISSUE_BEGIN)) ||
				FAILED(slot.Start->Issue(D3DISSUE_END))) {
				Disable();
				return false;
			}
			Active = &slot;
			return true;
		}
		return false; // GPU is more than the query-ring depth behind; skip a sample.
	}

	void End() {
		if (!Active) return;
		Slot* slot = Active;
		Active = nullptr;
		if (FAILED(slot->End->Issue(D3DISSUE_END)) ||
			FAILED(slot->Frequency->Issue(D3DISSUE_END)) ||
			FAILED(slot->Disjoint->Issue(D3DISSUE_END))) {
			Disable();
			return;
		}
		slot->Pending = true;
	}

#ifdef NVR_GPU_PROFILER_TEST
	unsigned GetSampleCount() const { return TotalSamples; }
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
	Slot Slots[RingSize];
	IDirect3DDevice9* Device = nullptr; // weak: owned by the renderer
	Slot* Active = nullptr;
	const char* Name;
	unsigned ReportSamples;
	unsigned WindowSamples = 0;
	unsigned TotalSamples = 0;
	unsigned ZeroTickSamples = 0;
	double SumMs = 0.0;
	double MinMs = DBL_MAX;
	double MaxMs = 0.0;
	bool Unavailable = false;

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
	}

	bool CreateQueries() {
		for (auto& slot : Slots) {
			if (FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &slot.Start)) ||
				FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &slot.End)) ||
				FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &slot.Frequency)) ||
				FAILED(Device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &slot.Disjoint))) {
#ifndef NVR_GPU_PROFILER_TEST
				static bool reported = false;
				if (!reported) {
					Logger::Log("GPU PROFILE unavailable: this D3D9 device does not support timestamp queries.");
					reported = true;
				}
#endif
				Disable();
				return false;
			}
		}
		return true;
	}

	void Disable() {
		ReleaseQueries();
		Unavailable = true;
	}

	void CollectCompleted() {
		for (auto& slot : Slots) {
			if (!slot.Pending) continue;

			BOOL disjoint = TRUE;
			const HRESULT disjointReady = slot.Disjoint->GetData(&disjoint, sizeof(disjoint), 0);
			if (disjointReady == S_FALSE) continue;
			if (disjointReady != S_OK) {
				Disable();
				return;
			}

			UINT64 start = 0, end = 0, frequency = 0;
			const HRESULT startReady = slot.Start->GetData(&start, sizeof(start), 0);
			const HRESULT endReady = slot.End->GetData(&end, sizeof(end), 0);
			const HRESULT frequencyReady = slot.Frequency->GetData(&frequency, sizeof(frequency), 0);
			if (FAILED(startReady) || FAILED(endReady) || FAILED(frequencyReady)) {
				Disable();
				return;
			}
			if (!GpuProfileQueriesReady(startReady, endReady, frequencyReady)) continue;
			slot.Pending = false;
			if (disjoint || !frequency || end < start) continue;

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

// CPU wall-clock counterpart, reported in the same log format and toggled by the same F10
// switch. Comparing CPU submit time and the frame interval against the GPU buckets shows
// whether a scene is CPU- or GPU-bound, which decides which kind of optimisation can help.
class CpuTimer {
public:
	explicit CpuTimer(const char* name, unsigned reportSamples = 120)
		: Name(name), ReportSamples(reportSamples) {}

	void Add(double ms) {
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

	// Interval between successive calls, e.g. once per frame. Gaps longer than a second (loading,
	// menus, profiling just enabled) are dropped rather than skewing the average.
	void Tick() {
		const double now = NowMs();
		if (LastTick > 0.0 && now - LastTick < 1000.0) Add(now - LastTick);
		LastTick = now;
	}

private:
	const char* Name;
	unsigned ReportSamples;
	unsigned WindowSamples = 0;
	double SumMs = 0.0;
	double MinMs = DBL_MAX;
	double MaxMs = 0.0;
	double LastTick = 0.0;
};

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
