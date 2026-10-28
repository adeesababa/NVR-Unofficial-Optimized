#pragma once
#include <vector>

namespace OnePassDepth {

	inline int Mode = 0;
	inline int MinLamps = 0;
	inline int Count = 0;
	inline bool HookSeen = false;
	inline bool Recording = false;
	inline bool InDepth = false;
	inline bool DepthDrawn = false;
	inline bool Swapped = false;
	inline DWORD SavedWrite[4] = {};
	inline IDirect3DPixelShader9* DepthShader = nullptr;
	inline bool ShaderFailed = false;
	inline bool LoggedOn = false;

	struct Record { void* pass; UInt32 passId, a3, a4, a5, cull, alphaTest; bool depth; };
	inline std::vector<Record> Records;

	inline bool IsOpt(UInt32 id) { return id >= 0xC3 && id <= 0xD6; }

	inline unsigned StatFrames = 0;
	inline int StatMode = -1;
	inline double StatPasses = 0, StatDepth = 0, StatSkinned = 0, StatAlpha = 0, StatFewLamps = 0, StatBailNotFirst = 0, StatLists = 0, StatCpuMs = 0;

	inline bool MakeShader(IDirect3DDevice9* device) {
		if (DepthShader) return true;
		if (ShaderFailed) return false;
		static const char* src = "float4 main() : COLOR0 { return float4(0.0, 0.0, 0.0, 0.0); }";
		ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
		if (FAILED(D3DXCompileShader(src, (UINT)strlen(src), NULL, NULL, "main", "ps_3_0", 0, &code, &errors, NULL)) || !code ||
			FAILED(device->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &DepthShader))) {
			Logger::Log("UNOFFICIAL LampsDepthFirst: the depth-only shader failed (%s); off", errors ? (const char*)errors->GetBufferPointer() : "?");
			if (errors) errors->Release();
			if (code) code->Release();
			DepthShader = nullptr;
			ShaderFailed = true;
			return false;
		}
		code->Release();
		return true;
	}

	namespace Counts {
		enum { kRing = 4, kMax = 2048 };
		inline std::vector<IDirect3DQuery9*> Pool[kRing];
		inline unsigned Used[kRing] = {};
		inline bool Issued[kRing] = {};
		inline unsigned Slot = 0;
		inline IDirect3DQuery9* Open = nullptr;
		inline double Samples = 0, Draws = 0, Frames = 0, NotReady = 0;

		inline void BeginFrame(IDirect3DDevice9* device, bool on) {
			Slot = (Slot + 1) % kRing;
			if (Issued[Slot]) {
				unsigned long long sum = 0;
				bool ready = true;
				for (unsigned i = 0; i < Used[Slot]; i++) {
					DWORD n = 0;
					if (Pool[Slot][i]->GetData(&n, sizeof(n), 0) != S_OK) { ready = false; break; }
					sum += n;
				}
				if (ready) { Samples += (double)sum; Draws += Used[Slot]; Frames += 1; }
				else NotReady += 1;
			}
			Issued[Slot] = false;
			Used[Slot] = 0;
			if (!on) return;
			Issued[Slot] = true;
		}
		inline void Before(IDirect3DDevice9* device) {
			Open = nullptr;
			if (!Issued[Slot] || Used[Slot] >= kMax) return;
			std::vector<IDirect3DQuery9*>& pool = Pool[Slot];
			if (Used[Slot] == pool.size()) {
				IDirect3DQuery9* q = nullptr;
				if (FAILED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &q)) || !q) return;
				pool.push_back(q);
			}
			Open = pool[Used[Slot]];
			if (FAILED(Open->Issue(D3DISSUE_BEGIN))) { Open = nullptr; return; }
		}
		inline void After() {
			if (!Open) return;
			Open->Issue(D3DISSUE_END);
			Open = nullptr;
			Used[Slot]++;
		}
	}

	inline void Report(int mode) {
		const double f = StatFrames;
		if (f < 30) return;
		const double screen = (double)TheRenderManager->width * TheRenderManager->height;
		Logger::Log("LAMPS DEPTH FIRST %s (min lamps %d), %.0f frames, per frame: lists moved %.2f, OPT passes %.1f, depth-first %.1f, left out: skinned %.1f, alpha %.1f, few lamps %.1f; frames not moved (an OPT pass not a first pass) %.2f; CPU of the move + both steps %.3f ms",
			mode ? "on" : "off", MinLamps, f, StatLists / f, StatPasses / f, StatDepth / f, StatSkinned / f, StatAlpha / f, StatFewLamps / f, StatBailNotFirst / f, StatCpuMs / f);
		if (Counts::Frames > 0)
			Logger::Log("LAMPS DEPTH FIRST counts (%s): OPT draws %.1f a frame shade %.0f samples a frame (%.3f per screen pixel; with MSAA, samples per pixel count); %.0f frames read, %.0f not ready",
				mode ? "on" : "off", Counts::Draws / Counts::Frames, Counts::Samples / Counts::Frames, Counts::Samples / Counts::Frames / screen, Counts::Frames, Counts::NotReady);
		Counts::Samples = Counts::Draws = Counts::Frames = Counts::NotReady = 0;
		StatFrames = 0;
		StatPasses = StatDepth = StatSkinned = StatAlpha = StatFewLamps = StatBailNotFirst = StatLists = StatCpuMs = 0;
	}

	inline int OnDxvk = -1;
	inline bool LoggedNative = false;

	inline void BeginFrame(IDirect3DDevice9* device) {
		if (OnDxvk < 0 && TheRenderManager) OnDxvk = !strncmp(TheRenderManager->D3D9RuntimeDescription(), "DXVK", 4) ? 1 : 0;
		const int want = TheSettingManager->GetSettingI("Main.Main.Misc", "LampsDepthFirst");
		Mode = OnDxvk == 1 ? want : 0;
		if (want && OnDxvk == 0 && !LoggedNative) {
			LoggedNative = true;
			Logger::Log("UNOFFICIAL LampsDepthFirst: does nothing on native D3D9 (measured slower there: the extra draws cost the driver's main-thread time); needs DXVK (stock or NVR DXVK)");
		}
		MinLamps = TheSettingManager->GetSettingI("Main.Main.Misc", "LampsDepthFirstMinLamps");
#if NVR_TEST_TOOLS
		Count = TheSettingManager->GetSettingI("Main.Main.Misc", "LampsDepthFirstCount");
#else
		Count = 0;
#endif
		if (Mode && !LoggedOn) {
			LoggedOn = true;
			Logger::Log("UNOFFICIAL LampsDepthFirst: on (the one many-lamp passes drawn depth first, then shaded once per visible pixel)%s",
				HookSeen ? "" : "; waiting for the per-object pass hooks");
		}
		Recording = InDepth = DepthDrawn = false;
		const bool counting = Count != 0 && GpuTimer::Enabled;
		Counts::BeginFrame(device, counting);
		if (!GpuTimer::Enabled) { StatFrames = 0; return; }
		if (StatMode != Mode || StatFrames >= 240) { Report(StatMode); StatMode = Mode; StatFrames = 0; }
		StatFrames++;
	}

	inline void BeginDepth(IDirect3DDevice9* device) {
		Swapped = false;
		if (!MakeShader(device)) return;
		for (int i = 0; i < 4; i++) device->GetRenderState((D3DRENDERSTATETYPE)(i ? D3DRS_COLORWRITEENABLE1 + i - 1 : D3DRS_COLORWRITEENABLE), &SavedWrite[i]);
		for (int i = 0; i < 4; i++) device->SetRenderState((D3DRENDERSTATETYPE)(i ? D3DRS_COLORWRITEENABLE1 + i - 1 : D3DRS_COLORWRITEENABLE), 0);
		device->SetPixelShader(DepthShader);
		Swapped = true;
	}
	inline void Reapply(IDirect3DDevice9* device) {
		if (Swapped && InDepth) device->SetPixelShader(DepthShader);
	}
	inline void EndDepth(IDirect3DDevice9* device) {
		if (!Swapped) return;
		Swapped = false;
		device->SetPixelShader(TheRenderManager->renderState->GetPixelShader());
		for (int i = 0; i < 4; i++) device->SetRenderState((D3DRENDERSTATETYPE)(i ? D3DRS_COLORWRITEENABLE1 + i - 1 : D3DRS_COLORWRITEENABLE), SavedWrite[i]);
	}

	inline bool Eligible(const void* passPtr, DWORD alphaTest, DWORD alphaBlend) {
		const UInt8* pass = (const UInt8*)passPtr;
		const NiGeometry* geometry = *(NiGeometry* const*)pass;
		const bool stats = GpuTimer::Enabled;
		if (stats) StatPasses += 1;
		const NiAlphaProperty* alpha = geometry ? geometry->propertyState.m_spAlphaProperty : nullptr;
		if (alphaTest || alphaBlend || (alpha && (alpha->flags & (1 | 0x200)))) { if (stats) StatAlpha += 1; return false; }
		if ((int)pass[9] < MinLamps) { if (stats) StatFewLamps += 1; return false; }
		return true;
	}
}
