#pragma once

// UNOFFICIAL redundant shader-constant filter ([Main.Main.Performance] SkipRedundantConstants).
//
// The draw-stats measuring build (HANDOFF "MEASURING BUILD RESULT", 2026-10-06) found the game CPU-bound indoors by its
// own draw submission (RCHangar: 10 ms submitting about 5500 draws), with 24.6K SetPixelShaderConstantF calls per frame of
// which 88% set exactly the values the device already held (98% of the registers), and 35% of the vertex ones. Each call
// still costs a trip through the D3D9 runtime and driver. Here every upload is compared with a copy of what the device
// holds and dropped when nothing changes, so the device ends up in the same state: the picture cannot change.
//
// The copy is only trusted while every write to the device's float constants comes through these two calls:
//  - IDirect3DStateBlock9::Apply (D3DX effects restore the state they saved at End) and IDirect3DDevice9::Reset wipe it;
//  - between BeginStateBlock and EndStateBlock the calls are recorded, not applied: passed through, copy untouched;
//  - a call from another thread than the one that draws the frames (the loading screen's) never touches the copy: it
//    only counts itself, and the frame thread then passes every call through and forgets what it sets until no such
//    call has come for 30 frames; then the copy starts again from empty. (The copy has no lock: only the frame thread
//    reads or writes it.) Until 2026-10-06 the first such call turned the filter off until restart, so it never ran
//    after the first loading screen.
// Hooked on the function bodies (Detours), not in the device's vtable: the native D3D9 runtime rewrites the vtable
// every frame (measuring build's lesson).
namespace ConstantFilter {

	typedef HRESULT(STDMETHODCALLTYPE* SetConstantsFn)(IDirect3DDevice9*, UINT, const float*, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
	typedef HRESULT(STDMETHODCALLTYPE* DeviceFn)(IDirect3DDevice9*);
	typedef HRESULT(STDMETHODCALLTYPE* EndStateBlockFn)(IDirect3DDevice9*, IDirect3DStateBlock9**);
	typedef HRESULT(STDMETHODCALLTYPE* ApplyFn)(IDirect3DStateBlock9*);

	enum { Registers = 256 };
	struct Copy {
		float value[Registers][4];
		bool known[Registers];
	};

	inline bool Installed = false;
	inline bool Enabled = false;		// the setting, read each frame
	inline bool Broken = false;			// the detours failed: off for good
	inline bool Recording = false;		// between BeginStateBlock and EndStateBlock
	inline DWORD Thread = 0;			// the thread that draws the frames (BeginFrame's)
	inline volatile LONG ForeignTick = 0;	// counts calls from another thread
	inline LONG SeenForeignTick = 0;	// the last ForeignTick the frame thread has seen
	inline bool Foreign = false;		// another thread called lately: pass through, forget, until 30 quiet frames
	inline unsigned QuietFrames = 0;
	inline Copy Vertex = {}, Pixel = {};
	inline SetConstantsFn OrigVertex = nullptr, OrigPixel = nullptr;
	inline ResetFn OrigReset = nullptr;
	inline DeviceFn OrigBeginStateBlock = nullptr;
	inline EndStateBlockFn OrigEndStateBlock = nullptr;
	inline ApplyFn OrigApply = nullptr;
	inline unsigned StatFrames = 0;
	inline double StatVertexCalls = 0, StatVertexSkipped = 0, StatPixelCalls = 0, StatPixelSkipped = 0, StatWipes = 0;
	inline unsigned StatPauses = 0;		// times another thread paused it (since start)

	inline void Wipe() {
		memset(Vertex.known, 0, sizeof(Vertex.known));
		memset(Pixel.known, 0, sizeof(Pixel.known));
		StatWipes += 1;
	}

	// True when the call can be dropped; otherwise the copy is updated to what the call sets.
	inline bool Redundant(Copy& copy, UINT start, const float* data, UINT count) {
		if (!data || start >= Registers || start + count > Registers) {
			// Out of the copy's range: let it through and forget what it covers.
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

	// 1: filter the call; 0: pass it through and forget the registers it sets; -1: pass it through, copy untouched.
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

	inline HRESULT STDMETHODCALLTYPE HookPixel(IDirect3DDevice9* self, UINT start, const float* data, UINT count) {
		const int use = Use();
		if (use > 0) {
			if (GpuTimer::Enabled) StatPixelCalls += 1;
			if (Redundant(Pixel, start, data, count)) { if (GpuTimer::Enabled) StatPixelSkipped += 1; return D3D_OK; }
		}
		else if (use == 0) for (UINT i = start; i < Registers && i < start + count; i++) Pixel.known[i] = false;
		return OrigPixel(self, start, data, count);
	}

	inline HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* parameters) {
		if (GetCurrentThreadId() != Thread) { InterlockedIncrement(&ForeignTick); return OrigReset(self, parameters); }
		Wipe();
		const HRESULT result = OrigReset(self, parameters);
		Wipe();
		return result;
	}

	inline HRESULT STDMETHODCALLTYPE HookBeginStateBlock(IDirect3DDevice9* self) {
		const HRESULT result = OrigBeginStateBlock(self);
		if (SUCCEEDED(result)) Recording = true;
		return result;
	}

	inline HRESULT STDMETHODCALLTYPE HookEndStateBlock(IDirect3DDevice9* self, IDirect3DStateBlock9** block) {
		Recording = false;
		if (GetCurrentThreadId() == Thread) Wipe();	// in case the runtime applied what it recorded
		else InterlockedIncrement(&ForeignTick);
		return OrigEndStateBlock(self, block);
	}

	inline HRESULT STDMETHODCALLTYPE HookApply(IDirect3DStateBlock9* self) {
		if (GetCurrentThreadId() == Thread) Wipe();	// it may set any constant without going through the calls above
		else InterlockedIncrement(&ForeignTick);
		return OrigApply(self);
	}

	// Once, with the device (Render.cpp, at the start of a frame). Detours the bodies the device's vtable points at now.
	inline void Install(IDirect3DDevice9* device) {
		if (Installed || !device) return;
		Installed = true;
		void** vtable = *(void***)device;
		OrigReset = (ResetFn)vtable[16];
		OrigBeginStateBlock = (DeviceFn)vtable[60];
		OrigEndStateBlock = (EndStateBlockFn)vtable[61];
		OrigVertex = (SetConstantsFn)vtable[94];
		OrigPixel = (SetConstantsFn)vtable[109];
		IDirect3DStateBlock9* block = nullptr;
		if (SUCCEEDED(device->CreateStateBlock(D3DSBT_PIXELSTATE, &block)) && block) {
			OrigApply = (ApplyFn)(*(void***)block)[5];
			block->Release();
		}
		if (!OrigApply) {
			Logger::Log("UNOFFICIAL SkipRedundantConstants: no state block to hook; filter not installed.");
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
		const LONG result = DetourTransactionCommit();
		if (result != NO_ERROR) {
			Broken = true;
			Logger::Log("UNOFFICIAL SkipRedundantConstants: detours failed (%ld); filter off.", result);
			return;
		}
		Wipe();
		Logger::Log("UNOFFICIAL SkipRedundantConstants: shader-constant uploads the device already holds are skipped (setting %s).",
			TheSettingManager->GetSettingI("Main.Main.Performance", "SkipRedundantConstants") ? "on" : "off");
	}

	// Each frame: the setting (live; turning it on starts from an empty copy), and the F10 line.
	inline void BeginFrame(IDirect3DDevice9* device) {
		Thread = GetCurrentThreadId();
		Install(device);
		const bool enabled = TheSettingManager->GetSettingI("Main.Main.Performance", "SkipRedundantConstants") != 0;
		if (enabled && !Enabled) Wipe();
		Enabled = enabled;
		// Another thread (the loading screen) set constants: pause until it has been quiet for 30 frames, then start again
		// from an empty copy.
		const LONG foreignTick = ForeignTick;
		if (foreignTick != SeenForeignTick) {
			if (!Foreign) StatPauses++;
			SeenForeignTick = foreignTick;
			Foreign = true;
			QuietFrames = 0;
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
			StatFrames = 0;
			StatVertexCalls = StatVertexSkipped = StatPixelCalls = StatPixelSkipped = StatWipes = 0;
		}
	}
}
