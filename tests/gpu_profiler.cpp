#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cfloat>

// Exercise the real collector against deterministic asynchronous query results.
namespace ProfilerTest {
enum { D3DQUERYTYPE_TIMESTAMP, D3DQUERYTYPE_TIMESTAMPFREQ, D3DQUERYTYPE_TIMESTAMPDISJOINT };
enum { D3DISSUE_BEGIN = 2, D3DISSUE_END = 1 };
static HRESULT TimestampStatus = S_OK;
static HRESULT DisjointStatus = S_OK;
static int LiveQueries = 0;
struct IDirect3DQuery9 {
	int Type;
	UINT64 Value;
	HRESULT Issue(DWORD) { return S_OK; }
	HRESULT GetData(void* data, DWORD, DWORD flags) {
		assert(flags == 0);
		HRESULT status = Type == D3DQUERYTYPE_TIMESTAMP ? TimestampStatus :
			Type == D3DQUERYTYPE_TIMESTAMPDISJOINT ? DisjointStatus : S_OK;
		if (status != S_OK) return status;
		if (Type == D3DQUERYTYPE_TIMESTAMPDISJOINT) *static_cast<BOOL*>(data) = FALSE;
		else *static_cast<UINT64*>(data) = Value;
		return S_OK;
	}
	void Release() { --LiveQueries; delete this; }
};
struct IDirect3DDevice9 {
	UINT64 Tick = 100;
	HRESULT CreateQuery(int type, IDirect3DQuery9** query) {
		*query = new IDirect3DQuery9{type, type == D3DQUERYTYPE_TIMESTAMPFREQ ? 1000000 : Tick++};
		++LiveQueries;
		return S_OK;
	}
};
#define NVR_GPU_PROFILER_TEST
#include "../src/core/GpuProfiler.h"
}

void CheckGpuProfiler() {
	using namespace ProfilerTest;
	IDirect3DDevice9 device;
	{
		GpuTimer timer("regression");
		assert(timer.Begin(&device)); timer.End();
		TimestampStatus = S_FALSE;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 0);
		TimestampStatus = S_OK;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 2);
		DisjointStatus = E_FAIL;
		assert(!timer.Begin(&device)); // used to dereference a released query
		assert(LiveQueries == 0);
		assert(!timer.Begin(&device));
	}
	DisjointStatus = S_OK;
	{
		GpuTimer timer("ring");
		TimestampStatus = S_FALSE;
		for (int i = 0; i < 12; ++i) { assert(timer.Begin(&device)); timer.End(); }
		assert(!timer.Begin(&device));
		TimestampStatus = S_OK;
		assert(timer.Begin(&device)); timer.End();
		assert(timer.GetSampleCount() == 12);
		TimestampStatus = E_FAIL;
		assert(!timer.Begin(&device));
		assert(LiveQueries == 0);
	}
	TimestampStatus = S_OK;
	std::puts("PASS: actual GPU collector: pending results, ring saturation/recovery, query failure and released-pointer guard.");
}
