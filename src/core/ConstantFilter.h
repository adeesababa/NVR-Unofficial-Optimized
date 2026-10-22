#pragma once

#include "LinearPreDecode.h"

namespace ConstantFilter {

	typedef HRESULT(STDMETHODCALLTYPE* SetConstantsFn)(IDirect3DDevice9*, UINT, const float*, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
	typedef HRESULT(STDMETHODCALLTYPE* DeviceFn)(IDirect3DDevice9*);
	typedef HRESULT(STDMETHODCALLTYPE* EndStateBlockFn)(IDirect3DDevice9*, IDirect3DStateBlock9**);
	typedef HRESULT(STDMETHODCALLTYPE* ApplyFn)(IDirect3DStateBlock9*);
	typedef HRESULT(STDMETHODCALLTYPE* SetPixelShaderFn)(IDirect3DDevice9*, IDirect3DPixelShader9*);

	enum { Registers = 256 };
	struct Copy {
		float value[Registers][4];
		bool known[Registers];
	};

	inline bool Installed = false;
	inline bool Enabled = false;
	inline bool Broken = false;
	inline bool Recording = false;
	inline DWORD Thread = 0;
	inline volatile LONG ForeignTick = 0;
	inline LONG SeenForeignTick = 0;
	inline bool Foreign = false;
	inline unsigned QuietFrames = 0;
	inline Copy Vertex = {}, Pixel = {};
	inline SetConstantsFn OrigVertex = nullptr, OrigPixel = nullptr;
	inline ResetFn OrigReset = nullptr;
	inline DeviceFn OrigBeginStateBlock = nullptr;
	inline EndStateBlockFn OrigEndStateBlock = nullptr;
	inline ApplyFn OrigApply = nullptr;
	inline SetPixelShaderFn OrigSetPixelShader = nullptr;
	inline unsigned StatFrames = 0;
	inline double StatVertexCalls = 0, StatVertexSkipped = 0, StatPixelCalls = 0, StatPixelSkipped = 0, StatWipes = 0;
	inline unsigned StatPauses = 0;
	inline void (*PixelShaderObserver)(IDirect3DPixelShader9*) = nullptr;
	inline void (*StateLostObserver)() = nullptr;

	inline void Wipe() {
		memset(Vertex.known, 0, sizeof(Vertex.known));
		memset(Pixel.known, 0, sizeof(Pixel.known));
		StatWipes += 1;
	}

	inline bool Redundant(Copy& copy, UINT start, const float* data, UINT count) {
		if (!data || start >= Registers || start + count > Registers) {
			for (UINT i = start; i < Registers && i < start + count; i++) copy.known[i] = false;
			return false;
		}
		bool same = true;
		for (UINT i = 0; i < count; i++) {
			if (!copy.known[start + i] || memcmp(copy.value[start + i], data + i * 4, sizeof(float) * 4) != 0) { same = false; break; }
		}
		if (same) return count > 0;
		memcpy(copy.value[start], data, sizeof(float) * 4 * count);
		for (UINT i = 0; i < count; i++) copy.known[start + i] = true;
		return false;
	}

	inline int Use() {
		if (Recording) return -1;
		const DWORD thread = GetCurrentThreadId();
		if (!Thread) return -1;
		if (thread != Thread) {
			InterlockedIncrement(&ForeignTick);
			return -1;
		}
		if (!Enabled || Broken) return 0;
		if (Foreign || ForeignTick != SeenForeignTick) { if (!Foreign) StatPauses++; Foreign = true; return 0; }
		return 1;
	}

	inline HRESULT STDMETHODCALLTYPE HookVertex(IDirect3DDevice9* self, UINT start, const float* data, UINT count) {
		const int use = Use();
		if (use > 0) {
			if (GpuTimer::Enabled) StatVertexCalls += 1;
			if (Redundant(Vertex, start, data, count)) { if (GpuTimer::Enabled) StatVertexSkipped += 1; return D3D_OK; }
		}
		else if (use == 0) for (UINT i = start; i < Registers && i < start + count; i++) Vertex.known[i] = false;
		return OrigVertex(self, start, data, count);
	}

	inline HRESULT SendPixel(IDirect3DDevice9* self, UINT start, const float* data, UINT count) {
		const int use = Use();
		if (use > 0) {
			if (GpuTimer::Enabled) StatPixelCalls += 1;
			if (Redundant(Pixel, start, data, count)) { if (GpuTimer::Enabled) StatPixelSkipped += 1; return D3D_OK; }
		}
		else if (use == 0) for (UINT i = start; i < Registers && i < start + count; i++) Pixel.known[i] = false;
		return OrigPixel(self, start, data, count);
	}

	inline bool PreDecodeThread() { return !Recording && Thread && GetCurrentThreadId() == Thread; }

	inline void PreDecodeCopy(IDirect3DDevice9* self, UINT r, const float* v) {
		using namespace LinearPreDecode;
		if (CopiesValid && CopyGamma == Gamma && SourceKnown[r] && !memcmp(Source[r], v, sizeof(float) * 4)) return;
		float d[4];
		DecodeFast(d, v, Gamma);
		SendPixel(self, CopyBase + r, d, 1);
		memcpy(Source[r], v, sizeof(float) * 4);
		SourceKnown[r] = true;
		StatCopies++;
	}

	struct PreDecodeRun {
		IDirect3DDevice9* device;
		UINT first = 0, n = 0;
		float values[LinearPreDecode::Last + 1][4];
		explicit PreDecodeRun(IDirect3DDevice9* d) : device(d) {}
		~PreDecodeRun() { Flush(); }
		void Flush() { if (n) SendPixel(device, LinearPreDecode::CopyBase + first, &values[0][0], n); n = 0; }
		void Add(UINT r, const float* v) {
			using namespace LinearPreDecode;
			if (CopiesValid && CopyGamma == Gamma && SourceKnown[r] && !memcmp(Source[r], v, sizeof(float) * 4)) { Flush(); return; }
			if (n && first + n != r) Flush();
			if (!n) first = r;
			DecodeFast(values[n], v, Gamma);
			memcpy(Source[r], v, sizeof(float) * 4);
			SourceKnown[r] = true;
			StatCopies++;
			n++;
		}
	};

	inline void PreDecodeRefresh(IDirect3DDevice9* self) {
		using namespace LinearPreDecode;
		float game[(Last + 1) * 4];
		if (FAILED(self->GetPixelShaderConstantF(0, game, Last + 1))) { StatReadFailures++; return; }
		if (!CopiesValid || CopyGamma != Gamma) for (UINT r = 0; r <= Last; r++) SourceKnown[r] = false;
		CopiesValid = true;
		CopyGamma = Gamma;
		if (UseCache) {
			PreDecodeRun run(self);
			for (UINT r = 1; r <= Last; r++) { if (Decodes(r)) run.Add(r, game + r * 4); else run.Flush(); }
		}
		else for (UINT r = 1; r <= Last; r++) if (Decodes(r)) PreDecodeCopy(self, r, game + r * 4);
		StatRefreshes++;
	}

	inline void PreDecodeResync(IDirect3DDevice9* device) {
		LinearPreDecode::CopiesValid = false;
		LinearPreDecode::Active = false;
		IDirect3DPixelShader9* shader = nullptr;
		if (device && SUCCEEDED(device->GetPixelShader(&shader)) && shader) {
			LinearPreDecode::Active = LinearPreDecode::Is(shader);
			shader->Release();
		}
		if (LinearPreDecode::Active) PreDecodeRefresh(device);
	}

	inline HRESULT STDMETHODCALLTYPE HookPixel(IDirect3DDevice9* self, UINT start, const float* data, UINT count) {
		const HRESULT result = SendPixel(self, start, data, count);
		if (data && count && !LinearPreDecode::Shaders.empty() && PreDecodeThread()) {
			if (start <= LinearPreDecode::CopyLast && start + count > LinearPreDecode::CopyFirst) LinearPreDecode::CopiesValid = false;
			if (LinearPreDecode::Active) {
				if (!LinearPreDecode::CopiesValid) PreDecodeRefresh(self);
				else if (start <= LinearPreDecode::Last && LinearPreDecode::UseCache) {
					PreDecodeRun run(self);
					for (UINT i = 0; i < count && start + i <= LinearPreDecode::Last; i++) {
						if (LinearPreDecode::Decodes(start + i)) run.Add(start + i, data + i * 4);
						else run.Flush();
					}
				}
				else if (start <= LinearPreDecode::Last)
					for (UINT i = 0; i < count && start + i <= LinearPreDecode::Last; i++)
						if (LinearPreDecode::Decodes(start + i)) PreDecodeCopy(self, start + i, data + i * 4);
			}
		}
		return result;
	}

	inline void PreDecodeCheck(IDirect3DDevice9* self) {
		using namespace LinearPreDecode;
		float game[(CopyLast + 1) * 4];
		if (FAILED(self->GetPixelShaderConstantF(0, game, CopyLast + 1))) { StatReadFailures++; return; }
		StatChecks++;
		for (UINT r = 1; r <= Last; r++) {
			if (!Decodes(r)) continue;
			float d[4];
			Decode(d, game + r * 4, Gamma);
			if (memcmp(d, game + (CopyBase + r) * 4, sizeof(d))) { StatMismatches++; break; }
		}
	}

	inline HRESULT STDMETHODCALLTYPE HookSetPixelShader(IDirect3DDevice9* self, IDirect3DPixelShader9* shader) {
		if (GpuTimer::Enabled && LinearPreDecode::Active && !LinearPreDecode::Shaders.empty() && PreDecodeThread()) PreDecodeCheck(self);
		const HRESULT result = OrigSetPixelShader(self, shader);
		if (SUCCEEDED(result) && PixelShaderObserver) PixelShaderObserver(shader);
		if (SUCCEEDED(result) && !LinearPreDecode::Shaders.empty() && PreDecodeThread()) {
			LinearPreDecode::Active = LinearPreDecode::Is(shader);
			if (LinearPreDecode::Active) PreDecodeRefresh(self);
		}
		return result;
	}

	inline HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters) {
		if (GetCurrentThreadId() != Thread) { InterlockedIncrement(&ForeignTick); const HRESULT r = OrigReset(self, parameters); if (StateLostObserver) StateLostObserver(); return r; }
		Wipe();
		const HRESULT result = OrigReset(self, parameters);
		if (StateLostObserver) StateLostObserver();
		Wipe();
		PreDecodeResync(self);
		return result;
	}

	inline HRESULT STDMETHODCALLTYPE HookBeginStateBlock(IDirect3DDevice9* self) {
		const HRESULT result = OrigBeginStateBlock(self);
		if (SUCCEEDED(result)) Recording = true;
		return result;
	}

	inline HRESULT STDMETHODCALLTYPE HookEndStateBlock(IDirect3DDevice9* self, IDirect3DStateBlock9** block) {
		Recording = false;
		if (GetCurrentThreadId() == Thread) Wipe();
		else InterlockedIncrement(&ForeignTick);
		const HRESULT result = OrigEndStateBlock(self, block);
		if (StateLostObserver) StateLostObserver();
		if (GetCurrentThreadId() == Thread) PreDecodeResync(self);
		return result;
	}

	inline HRESULT STDMETHODCALLTYPE HookApply(IDirect3DStateBlock9* self) {
		if (GetCurrentThreadId() == Thread) Wipe();
		else InterlockedIncrement(&ForeignTick);
		const HRESULT result = OrigApply(self);
		if (StateLostObserver) StateLostObserver();
		if (GetCurrentThreadId() == Thread) {
			IDirect3DDevice9* device = nullptr;
			if (SUCCEEDED(self->GetDevice(&device)) && device) { PreDecodeResync(device); device->Release(); }
		}
		return result;
	}

	inline void Install(IDirect3DDevice9* device) {
		if (Installed || !device) return;
		Installed = true;
		void** vtable = *(void***)device;
		OrigReset = (ResetFn)vtable[16];
		OrigBeginStateBlock = (DeviceFn)vtable[60];
		OrigEndStateBlock = (EndStateBlockFn)vtable[61];
		OrigVertex = (SetConstantsFn)vtable[94];
		OrigPixel = (SetConstantsFn)vtable[109];
		OrigSetPixelShader = (SetPixelShaderFn)vtable[107];
		IDirect3DStateBlock9* block = nullptr;
		if (SUCCEEDED(device->CreateStateBlock(D3DSBT_PIXELSTATE, &block)) && block) {
			OrigApply = (ApplyFn)(*(void***)block)[5];
			block->Release();
		}
		if (!OrigApply) {
			Logger::Log("UNOFFICIAL SkipRedundantConstants: no state block to hook; filter not installed.");
			if (!LinearPreDecode::Shaders.empty()) Logger::Log("ERROR: LinearPreDecode needs the constant hooks: lit objects' light colours will be wrong; set [Shaders.PBR.Linear] PreDecode = false.");
			return;
		}
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&(PVOID&)OrigReset, (PVOID)&HookReset);
		DetourAttach(&(PVOID&)OrigBeginStateBlock, (PVOID)&HookBeginStateBlock);
		DetourAttach(&(PVOID&)OrigEndStateBlock, (PVOID)&HookEndStateBlock);
		DetourAttach(&(PVOID&)OrigApply, (PVOID)&HookApply);
		DetourAttach(&(PVOID&)OrigVertex, (PVOID)&HookVertex);
		DetourAttach(&(PVOID&)OrigPixel, (PVOID)&HookPixel);
		DetourAttach(&(PVOID&)OrigSetPixelShader, (PVOID)&HookSetPixelShader);
		const LONG result = DetourTransactionCommit();
		if (result != NO_ERROR) {
			Broken = true;
			Logger::Log("UNOFFICIAL SkipRedundantConstants: detours failed (%ld); filter off.", result);
			if (!LinearPreDecode::Shaders.empty()) Logger::Log("ERROR: LinearPreDecode needs the constant hooks: lit objects' light colours will be wrong; set [Shaders.PBR.Linear] PreDecode = false.");
			return;
		}
		Wipe();
		Logger::Log("UNOFFICIAL SkipRedundantConstants: shader-constant uploads the device already holds are skipped (setting %s).",
			TheSettingManager->GetSettingI("Main.Main.Performance", "SkipRedundantConstants") ? "on" : "off");
	}

	inline void BeginFrame(IDirect3DDevice9* device) {
		Thread = GetCurrentThreadId();
		Install(device);
		LinearPreDecode::UseCache = TheSettingManager->SettingsMain.Main.PreDecodeCache;
		if (LinearPreDecode::CopyGamma != LinearPreDecode::Gamma) LinearPreDecode::CopiesValid = false;
		const bool enabled = TheSettingManager->SettingsMain.Main.SkipRedundantConstants;
		if (enabled && !Enabled) Wipe();
		Enabled = enabled;
		const LONG foreignTick = ForeignTick;
		if (foreignTick != SeenForeignTick) {
			if (!Foreign) StatPauses++;
			SeenForeignTick = foreignTick;
			Foreign = true;
			QuietFrames = 0;
			if (!LinearPreDecode::Shaders.empty()) PreDecodeResync(device);
		}
		else if (Foreign && ++QuietFrames >= 30) {
			Foreign = false;
			Wipe();
		}
		if (GpuTimer::Enabled && Installed && ++StatFrames >= 240) {
			const double perFrame = 1.0 / StatFrames;
			Logger::Log("CONSTANT FILTER %s: pixel %.0f calls, %.0f skipped (%.0f%%) | vertex %.0f calls, %.0f skipped (%.0f%%) per frame | copy wiped %.1f times per frame | paused for another thread %u times so far",
				Broken ? "OFF (detours failed)" : !Enabled ? "off" : Foreign ? "paused (another thread)" : "on", StatPixelCalls * perFrame, StatPixelSkipped * perFrame,
				StatPixelCalls > 0 ? 100.0 * StatPixelSkipped / StatPixelCalls : 0.0, StatVertexCalls * perFrame, StatVertexSkipped * perFrame,
				StatVertexCalls > 0 ? 100.0 * StatVertexSkipped / StatVertexCalls : 0.0, StatWipes * perFrame, StatPauses);
			if (!LinearPreDecode::Shaders.empty())
				Logger::Log("LINEAR PREDECODE: %u shaders, %.0f decoded copies and %.0f copy refreshes per frame (gamma %.2f), %u read-back failures | self-check: %u of %u finished draw runs had a stale copy | PreDecodeCache %s: %.0f channel lookups per frame, %.1f%% from the table",
					(unsigned)LinearPreDecode::Shaders.size(), LinearPreDecode::StatCopies * perFrame, LinearPreDecode::StatRefreshes * perFrame, LinearPreDecode::Gamma,
					LinearPreDecode::StatReadFailures, LinearPreDecode::StatMismatches, LinearPreDecode::StatChecks, LinearPreDecode::UseCache ? "on" : "off",
					LinearPreDecode::StatPowLookups * perFrame, LinearPreDecode::StatPowLookups ? 100.0 * (LinearPreDecode::StatPowLookups - LinearPreDecode::StatPowMisses) / LinearPreDecode::StatPowLookups : 0.0);
			LinearPreDecode::StatPowLookups = LinearPreDecode::StatPowMisses = 0;
			LinearPreDecode::StatMismatches = LinearPreDecode::StatChecks = 0;
			LinearPreDecode::StatCopies = LinearPreDecode::StatRefreshes = 0;
			StatFrames = 0;
			StatVertexCalls = StatVertexSkipped = StatPixelCalls = StatPixelSkipped = StatWipes = 0;
		}
	}
}
