#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <vector>

// Exercise the real collector against deterministic asynchronous query results.
namespace ProfilerTest {
enum { D3DQUERYTYPE_TIMESTAMP, D3DQUERYTYPE_TIMESTAMPFREQ, D3DQUERYTYPE_TIMESTAMPDISJOINT };
enum { D3DISSUE_BEGIN = 2, D3DISSUE_END = 1 };
static HRESULT TimestampStatus = S_OK;
static HRESULT DisjointStatus = S_OK;
static HRESULT FrequencyStatus = S_OK;
static BOOL DisjointValue = FALSE;
static UINT64 FrequencyValue = 1000000;
static bool CannotCreate[3] = {};           // per query type: CreateQuery fails, as on a layer without it
static int LiveQueries = 0;
struct IDirect3DQuery9 {
	int Type;
	UINT64 Value;
	HRESULT Issue(DWORD) { return S_OK; }
	HRESULT GetData(void* data, DWORD, DWORD flags) {
		assert(flags == 0);
		HRESULT status = Type == D3DQUERYTYPE_TIMESTAMP ? TimestampStatus :
			Type == D3DQUERYTYPE_TIMESTAMPDISJOINT ? DisjointStatus : FrequencyStatus;
		if (status != S_OK) return status;
		if (Type == D3DQUERYTYPE_TIMESTAMPDISJOINT) *static_cast<BOOL*>(data) = DisjointValue;
		else if (Type == D3DQUERYTYPE_TIMESTAMPFREQ) *static_cast<UINT64*>(data) = FrequencyValue;
		else *static_cast<UINT64*>(data) = Value;
		return S_OK;
	}
	void Release() { --LiveQueries; delete this; }
};
struct IDirect3DDevice9 {
	UINT64 Tick = 100;
	HRESULT CreateQuery(int type, IDirect3DQuery9** query) {
		*query = nullptr;
		if (CannotCreate[type]) return E_FAIL; // D3DERR_NOTAVAILABLE on a real device
		*query = new IDirect3DQuery9{type, Tick++};
		++LiveQueries;
		return S_OK;
	}
};
#define NVR_GPU_PROFILER_TEST
#include "../src/core/GpuProfiler.h"
}

static void ResetMock() {
	using namespace ProfilerTest;
	TimestampStatus = DisjointStatus = FrequencyStatus = S_OK;
	DisjointValue = FALSE;
	FrequencyValue = 1000000;
	CannotCreate[0] = CannotCreate[1] = CannotCreate[2] = false;
}

void CheckGpuProfiler() {
	using namespace ProfilerTest;
	IDirect3DDevice9 device;
	ResetMock();
	{
		GpuTimer timer("regression");
		assert(timer.Begin(&device)); timer.End();
		TimestampStatus = S_FALSE;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 0);
		TimestampStatus = S_OK;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 2);
		// A failing DISJOINT query no longer kills the timer (a D3D9 layer may not implement it):
		// timing continues without it. It used to switch the whole timer off.
		DisjointStatus = E_FAIL;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.Begin(&device)); timer.End();
		assert(!timer.DisjointTrusted());
		assert(timer.GetSampleCount() >= 3);
	}
	assert(LiveQueries == 0);

	ResetMock();
	{
		GpuTimer timer("ring");
		TimestampStatus = S_FALSE;
		for (int i = 0; i < 12; ++i) { assert(timer.Begin(&device)); timer.End(); }
		assert(!timer.Begin(&device));
		TimestampStatus = S_OK;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 12);
		TimestampStatus = E_FAIL; // a failing TIMESTAMP query is fatal for the timer, and frees its queries
		assert(!timer.Begin(&device));
		assert(LiveQueries == 0);
		assert(!timer.Begin(&device));
	}

	// A layer with no TIMESTAMPFREQ / TIMESTAMPDISJOINT queries at all still gets timings.
	ResetMock();
	CannotCreate[D3DQUERYTYPE_TIMESTAMPFREQ] = CannotCreate[D3DQUERYTYPE_TIMESTAMPDISJOINT] = true;
	{
		GpuTimer timer("no optional queries");
		for (int i = 0; i < 5; ++i) { assert(timer.Begin(&device)); timer.End(); }
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() >= 5);
		assert(timer.UsingFallbackClock() && !timer.DisjointTrusted());
	}
	assert(LiveQueries == 0);

	// The two timestamp queries are mandatory: without them the timer is unavailable and holds nothing.
	ResetMock();
	CannotCreate[D3DQUERYTYPE_TIMESTAMP] = true;
	{
		GpuTimer timer("no timestamps");
		assert(!timer.Begin(&device));
		assert(!timer.Begin(&device));
	}
	assert(LiveQueries == 0);

	// A frequency query that starts failing switches to the 1 GHz fallback instead of dropping samples.
	ResetMock();
	{
		GpuTimer timer("frequency fails");
		assert(timer.Begin(&device)); timer.End();
		FrequencyStatus = E_FAIL;
		for (int i = 0; i < 4; ++i) { assert(timer.Begin(&device)); timer.End(); }
		assert(timer.UsingFallbackClock());
		assert(timer.GetSampleCount() >= 3);
	}
	assert(LiveQueries == 0);

	// A disjoint result of TRUE, or a zero frequency, discards the sample and counts it as rejected.
	ResetMock();
	DisjointValue = TRUE;
	{
		GpuTimer timer("disjoint true");
		for (int i = 0; i < 6; ++i) { assert(timer.Begin(&device)); timer.End(); }
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 0 && timer.GetRejectedCount() > 0);
	}
	ResetMock();
	FrequencyValue = 0;
	{
		GpuTimer timer("zero frequency");
		for (int i = 0; i < 6; ++i) { assert(timer.Begin(&device)); timer.End(); }
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 0 && timer.GetRejectedCount() > 0);
	}
	assert(LiveQueries == 0);
	ResetMock();
	std::puts("PASS: actual GPU collector: pending results, ring saturation/recovery, fatal timestamp failure, optional disjoint/frequency queries (missing, failing, rejected).");
}
