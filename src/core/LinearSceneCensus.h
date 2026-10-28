#pragma once

#include <d3d9.h>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace LinearSceneCensus {

	inline int Mode = 0;
	inline bool InMain = false;
	inline UInt16 LastPass = 0xFFFF;

	struct Name { const char* name; bool nvr; };
	inline std::unordered_map<IDirect3DPixelShader9*, Name> Names;

	struct Key {
		IDirect3DPixelShader9* ps; UInt16 pass; UInt8 context, main, blend;
		bool operator==(const Key& o) const { return ps == o.ps && pass == o.pass && context == o.context && main == o.main && blend == o.blend; }
	};
	struct KeyHash { size_t operator()(const Key& k) const { return (size_t)k.ps * 31u + k.pass * 131u + k.context * 7u + k.main * 3u + k.blend; } };
	inline std::unordered_map<Key, UInt64, KeyHash> Counts;
	inline UInt32 Frames = 0;

	typedef HRESULT(STDMETHODCALLTYPE* DIPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* DPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
	inline DIPFn NextDIP = nullptr;
	inline DPFn NextDP = nullptr;
	inline bool Hooked = false;
	inline int (*ContextOf)() = nullptr;

	inline UInt8 BlendKind(IDirect3DDevice9* d) {
		DWORD on = 0, src = 0, dst = 0;
		d->GetRenderState(D3DRS_ALPHABLENDENABLE, &on);
		if (!on) return 0;
		d->GetRenderState(D3DRS_SRCBLEND, &src);
		d->GetRenderState(D3DRS_DESTBLEND, &dst);
		if (src == D3DBLEND_ONE && dst == D3DBLEND_ONE) return 1;
		if ((src == D3DBLEND_DESTCOLOR && dst == D3DBLEND_ZERO) || (src == D3DBLEND_ZERO && dst == D3DBLEND_SRCCOLOR)) return 2;
		if (src == D3DBLEND_SRCALPHA && dst == D3DBLEND_INVSRCALPHA) return 3;
		return 4;
	}

	inline void Count(IDirect3DDevice9* d) {
		if (!Mode || !ContextOf) return;
		const int c = ContextOf();
		if (!c) return;
		IDirect3DPixelShader9* ps = nullptr;
		d->GetPixelShader(&ps);
		if (ps) ps->Release();
		Counts[{ ps, LastPass, (UInt8)c, (UInt8)(InMain ? 1 : 0), BlendKind(d) }]++;
	}

	inline HRESULT STDMETHODCALLTYPE DIPHook(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT s, UINT pc) {
		Count(d);
		return NextDIP(d, t, b, mi, nv, s, pc);
	}
	inline HRESULT STDMETHODCALLTYPE DPHook(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT s, UINT pc) {
		Count(d);
		return NextDP(d, t, s, pc);
	}

	inline void OnBind(IDirect3DPixelShader9* handle, const char* name, bool nvr, UInt32 pass) {
		if (!Mode) return;
		LastPass = (UInt16)pass;
		if (!handle) return;
		Names[handle] = { name, nvr };
	}

	inline void Hook(IDirect3DDevice9* d) {
		if (Hooked || !d) return;
		void** table = *(void***)d;
		DWORD old = 0;
		if (!VirtualProtect(&table[81], 2 * sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
		NextDP = (DPFn)table[81];  table[81] = (void*)&DPHook;
		NextDIP = (DIPFn)table[82]; table[82] = (void*)&DIPHook;
		VirtualProtect(&table[81], 2 * sizeof(void*), old, &old);
		Hooked = true;
		Logger::Log("LINEAR CENSUS: draw hooks on");
	}

	inline void Report() {
		static const char* const blends[] = { "opaque", "add ONE+ONE", "multiply", "alpha", "other blend" };
		static const char* const contexts[] = { "other", "world", "first person" };
		std::vector<std::pair<Key, UInt64>> v(Counts.begin(), Counts.end());
		std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
		UInt64 total = 0, nvr = 0, mainAll = 0, mainNvr = 0;
		for (const auto& e : v) {
			auto it = Names.find(e.first.ps);
			const bool isNvr = it != Names.end() && it->second.nvr;
			total += e.second; if (isNvr) nvr += e.second;
			if (e.first.main) { mainAll += e.second; if (isNvr) mainNvr += e.second; }
		}
		Logger::Log("LINEAR CENSUS: %u frames, %.1f draws/frame (%.1f by NVR shaders); main opaque list %.1f/frame (%.1f NVR)", Frames,
			total / (double)Frames, nvr / (double)Frames, mainAll / (double)Frames, mainNvr / (double)Frames);
		unsigned shown = 0;
		for (const auto& e : v) {
			if (shown++ >= 60) break;
			auto it = Names.find(e.first.ps);
			char ptr[24]; sprintf_s(ptr, "%p", (void*)e.first.ps);
			Logger::Log("LINEAR CENSUS: %6.1f/frame  %-12s %-4s pass %03X  %-12s %-24s %s", e.second / (double)Frames, contexts[e.first.context],
				e.first.main ? "MAIN" : "-", (unsigned)e.first.pass, blends[e.first.blend], it != Names.end() && it->second.name ? it->second.name : ptr,
				it == Names.end() ? "(unnamed: not a pass bind)" : it->second.nvr ? "NVR" : "game");
		}
	}

	inline void BeginFrame(IDirect3DDevice9* d) {
		const int want = TheSettingManager->GetSettingI("Main.Main.Misc", "LinearSceneCensus");
		if (want && !Hooked) Hook(d);
		if (want != Mode) { Counts.clear(); Frames = 0; }
		Mode = want;
		if (!Mode) return;
		if (++Frames >= 600) { Report(); Counts.clear(); Frames = 0; }
	}
}
