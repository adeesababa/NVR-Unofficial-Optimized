#pragma once

#include <d3dx9.h>
#include "GpuProfiler.h"

namespace PassSums {

	inline D3DXVECTOR4 Redirect = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	inline float Gamma = 1.0f;

	enum Kind { None = 0, Lamps = 1, Highlights = 2 };
	inline int Mode = 0;
	inline Kind Active = None;
	inline int Runs[3] = { 0, 0, 0 };
	constexpr int kFoldRunCap = 2;
	constexpr int kIncrementRunCap = 8;
	inline IDirect3DSurface9* Scene = nullptr;
	inline D3DVIEWPORT9 SceneViewport = {};
	inline bool SceneViewportSaved = false;
	inline IDirect3DTexture9* AccTex = nullptr;   inline IDirect3DSurface9* Acc = nullptr;
	inline IDirect3DSurface9* AccMS = nullptr;
	inline IDirect3DTexture9* CopyTex = nullptr;  inline IDirect3DSurface9* Copy = nullptr;
	inline IDirect3DPixelShader9* ComposePS = nullptr;
	inline D3DSURFACE_DESC Made = {};
	inline bool Failed = false;
	inline UInt32 Composes = 0, Frames = 0;
	inline int LogFrames = 0;
	inline UInt32 RunPasses = 0;	inline UInt16 RunFirst = 0, RunLast = 0;
	inline int LastMode = 0;
	constexpr int kIncrements = 4;
	constexpr DWORD kRunStartStage = 9;
	constexpr int kHighlightIncrements = 8;
	constexpr DWORD kHighlightStartStage = 6;
	inline DWORD IncStage = kRunStartStage;
	inline bool HighlightSumsChecked = false, HighlightSumsCompiled = false;
	inline UInt32 IncHlFrames = 0, IncHlCopies = 0, IncHlPasses = 0, IncHlLeft = 0;
	inline IDirect3DTexture9* IncTex = nullptr;  inline IDirect3DSurface9* IncSurf = nullptr;
	inline D3DSURFACE_DESC IncMade = {};
	inline bool RunIsIncrements = false;
	inline bool IncLive = false;
	inline bool LampSumsChecked = false, LampSumsCompiled = false;
	inline UInt32 IncFrames = 0, IncCopies = 0, IncPasses = 0, IncLeft = 0, IncFailed = 0;

	inline Kind KindOf(UInt16 e) {
		if ((e >= 0x132 && e <= 0x13C && e != 0x137) || (e >= 0x13E && e <= 0x148 && e != 0x143)) return Lamps;
		switch (e) {
		case 0x15A: case 0x15C: case 0x15D: case 0x160: case 0x162: case 0x163:
		case 0x166: case 0x168: case 0x169: case 0x16C: case 0x16E: case 0x16F:
		case 0x172: case 0x174: case 0x175:
			return Highlights;
		}
		return None;
	}

	inline void Release() {
		for (IUnknown** p : { (IUnknown**)&AccMS, (IUnknown**)&Acc, (IUnknown**)&AccTex, (IUnknown**)&Copy, (IUnknown**)&CopyTex })
			if (*p) { (*p)->Release(); *p = nullptr; }
		Made = {};
	}

	inline void ReleaseIncrements() {
		if (IncSurf) { IncSurf->Release(); IncSurf = nullptr; }
		if (IncTex) { IncTex->Release(); IncTex = nullptr; }
		IncMade = {};
	}

	inline bool MakeIncrements(IDirect3DDevice9* device, const D3DSURFACE_DESC& d) {
		if (IncTex && IncMade.Width == d.Width && IncMade.Height == d.Height && IncMade.Format == d.Format) return true;
		ReleaseIncrements();
		if (FAILED(device->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &IncTex, NULL)) ||
			FAILED(IncTex->GetSurfaceLevel(0, &IncSurf))) {
			Logger::Log("LampSums: could not make the frame's copy (%ux%u format %u): off", d.Width, d.Height, (unsigned)d.Format);
			ReleaseIncrements();
			return false;
		}
		IncMade = d;
		Logger::Log("LampSums: copy made (%ux%u format %u, the frame msaa %u)", d.Width, d.Height, (unsigned)d.Format, (unsigned)d.MultiSampleType);
		return true;
	}

	inline bool Make(IDirect3DDevice9* device, const D3DSURFACE_DESC& d) {
		if (AccTex && Made.Width == d.Width && Made.Height == d.Height && Made.Format == d.Format && Made.MultiSampleType == d.MultiSampleType &&
			Made.MultiSampleQuality == d.MultiSampleQuality) return true;
		Release();
		if (d.MultiSampleType != D3DMULTISAMPLE_NONE &&
			FAILED(device->CreateRenderTarget(d.Width, d.Height, d.Format, d.MultiSampleType, d.MultiSampleQuality, FALSE, &AccMS, NULL))) {
			Logger::Log("ExactPassSums: no multisampled buffer (%ux%u, msaa %u): off", d.Width, d.Height, (unsigned)d.MultiSampleType);
			return false;
		}
		if (FAILED(device->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &AccTex, NULL)) ||
			FAILED(AccTex->GetSurfaceLevel(0, &Acc)) ||
			FAILED(device->CreateTexture(d.Width, d.Height, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &CopyTex, NULL)) ||
			FAILED(CopyTex->GetSurfaceLevel(0, &Copy))) {
			Logger::Log("ExactPassSums: could not make its buffers (%ux%u format %u): off", d.Width, d.Height, (unsigned)d.Format);
			Release();
			return false;
		}
		if (!ComposePS) {
			static const char* kPS =
				"sampler2D SceneS : register(s0); sampler2D AccS : register(s1); float4 G : register(c0);"
				"float4 main(float2 uv : TEXCOORD0) : COLOR0 {"
				"  float3 a = tex2D(AccS, uv).rgb;"
				"  clip(max(a.r, max(a.g, a.b)) - 1e-10);"
				"  float3 s = max(tex2D(SceneS, uv).rgb, 0.0);"
				"  return float4(pow(pow(s, G.x) + max(a, 0.0), G.y) - s, 1.0); }";
			ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
			if (FAILED(D3DXCompileShader(kPS, (UINT)strlen(kPS), NULL, NULL, "main", "ps_3_0", 0, &code, &errors, NULL)) || !code ||
				FAILED(device->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &ComposePS))) {
				Logger::Log("ExactPassSums: compose shader failed: %s", errors ? (const char*)errors->GetBufferPointer() : "?");
				if (errors) errors->Release();
				if (code) code->Release();
				Release();
				return false;
			}
			code->Release();
		}
		Made = d;
		Logger::Log("ExactPassSums: buffers made (%ux%u format %u msaa %u)", d.Width, d.Height, (unsigned)d.Format, (unsigned)d.MultiSampleType);
		return true;
	}

	inline void SetFlag(IDirect3DDevice9* device, float v) {
		Redirect.x = v;
		device->SetPixelShaderConstantF(210, (const float*)&Redirect, 1);
	}

	inline void Begin(IDirect3DDevice9* device, Kind kind) {
		IDirect3DSurface9* target = nullptr;
		if (FAILED(device->GetRenderTarget(0, &target)) || !target) return;
		D3DSURFACE_DESC d = {};
		target->GetDesc(&d);
		if (!Make(device, d)) { target->Release(); Failed = true; return; }
		static GpuTimer timer("  Pass sums (clear)");
		GpuProfileScope gpu(timer, device);
		Scene = target;
		SceneViewportSaved = SUCCEEDED(device->GetViewport(&SceneViewport));
		device->SetRenderTarget(0, AccMS ? AccMS : Acc);
		if (SceneViewportSaved) device->SetViewport(&SceneViewport);
		DWORD scissor = FALSE;
		device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor);
		if (scissor) device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
		device->Clear(0, NULL, D3DCLEAR_TARGET, 0, 0.0f, 0);
		if (scissor) device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
		SetFlag(device, 1.0f);
		Active = kind;
		Runs[kind]++;
		RunPasses = 0;
	}

	inline void RecordFoldStates(IDirect3DDevice9* device) {
		const float g = Gamma > 0.0f ? Gamma : 1.0f;
		const float G[4] = { g, 1.0f / g, 0.0f, 0.0f };
		device->SetVertexShader(NULL);
		device->SetPixelShader(ComposePS);
		device->SetPixelShaderConstantF(0, G, 1);
		device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
		for (DWORD s = 0; s < 2; s++) {
			device->SetTexture(s, s ? (IDirect3DBaseTexture9*)AccTex : (IDirect3DBaseTexture9*)CopyTex);
			device->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT); device->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
			device->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
			device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
			device->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
		}
		const DWORD rs[][2] = { { D3DRS_ZENABLE, FALSE }, { D3DRS_ZWRITEENABLE, FALSE }, { D3DRS_ALPHABLENDENABLE, TRUE },
			{ D3DRS_SRCBLEND, D3DBLEND_ONE }, { D3DRS_DESTBLEND, D3DBLEND_ONE }, { D3DRS_BLENDOP, D3DBLENDOP_ADD },
			{ D3DRS_SEPARATEALPHABLENDENABLE, FALSE },
			{ D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_STENCILENABLE, FALSE }, { D3DRS_SCISSORTESTENABLE, FALSE },
			{ D3DRS_CULLMODE, D3DCULL_NONE }, { D3DRS_COLORWRITEENABLE, 7 }, { D3DRS_FILLMODE, D3DFILL_SOLID },
			{ D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FOGENABLE, FALSE } };
		for (auto& r : rs) device->SetRenderState((D3DRENDERSTATETYPE)r[0], r[1]);
	}

	inline void BindRunStart() {
		TheRenderManager->renderState->SetTexture(IncStage, IncTex);
		TheRenderManager->SetSamplerState(IncStage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		TheRenderManager->SetSamplerState(IncStage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		TheRenderManager->SetSamplerState(IncStage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		TheRenderManager->SetSamplerState(IncStage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		TheRenderManager->SetSamplerState(IncStage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
		TheRenderManager->SetSamplerState(IncStage, D3DSAMP_SRGBTEXTURE, FALSE);
	}

	inline void BeginIncrements(IDirect3DDevice9* device, Kind kind) {
		Active = kind;
		Runs[kind]++;
		RunPasses = 0;
		RunIsIncrements = true;
		IncLive = false;
		IncStage = kind == Highlights ? kHighlightStartStage : kRunStartStage;
		IDirect3DSurface9* target = nullptr;
		if (FAILED(device->GetRenderTarget(0, &target)) || !target) { IncFailed++; return; }
		D3DSURFACE_DESC d = {};
		target->GetDesc(&d);
		const bool made = MakeIncrements(device, d);
		static GpuTimer timer("  Lamp sums (copy)");
		bool copied = false;
		if (made) {
			GpuProfileScope gpu(timer, device);
			copied = SUCCEEDED(device->StretchRect(target, NULL, IncSurf, NULL, D3DTEXF_NONE));
		}
		target->Release();
		if (!copied) { IncFailed++; if (!made) Failed = true; return; }
		BindRunStart();
		Redirect = D3DXVECTOR4(0.0f, 1.0f, 1.0f / (float)d.Width, 1.0f / (float)d.Height);
		device->SetPixelShaderConstantF(210, (const float*)&Redirect, 1);
		IncLive = true;
		if (kind == Highlights) IncHlCopies++; else IncCopies++;
	}

	inline void EndIncrements(IDirect3DDevice9* device) {
		const Kind kind = Active;
		Active = None;
		RunIsIncrements = false;
		if (!IncLive) return;
		IncLive = false;
		if (kind == Highlights) IncHlPasses += RunPasses; else IncPasses += RunPasses;
		Redirect = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
		device->SetPixelShaderConstantF(210, (const float*)&Redirect, 1);
		TheRenderManager->renderState->SetTexture(IncStage, nullptr);
	}

	inline void End(IDirect3DDevice9* device) {
		if (Active == None) return;
		if (RunIsIncrements) { EndIncrements(device); return; }
		Active = None;
		SetFlag(device, 0.0f);
		static GpuTimer timer("  Pass sums (fold in)");
		GpuProfileScope gpu(timer, device);
		device->SetRenderTarget(0, Scene);
		if (SceneViewportSaved) device->SetViewport(&SceneViewport);
		IDirect3DStateBlock9* saved = nullptr;
		if (SUCCEEDED(device->BeginStateBlock())) {
			RecordFoldStates(device);
			device->EndStateBlock(&saved);
			if (saved) saved->Capture();
		}
		if (saved && SUCCEEDED(device->StretchRect(Scene, NULL, Copy, NULL, D3DTEXF_NONE)) &&
			(!AccMS || SUCCEEDED(device->StretchRect(AccMS, NULL, Acc, NULL, D3DTEXF_NONE)))) {
			RecordFoldStates(device);
			const float w = (float)Made.Width, h = (float)Made.Height;
			const float quad[4][6] = { { -0.5f, -0.5f, 0, 1, 0, 0 }, { w - 0.5f, -0.5f, 0, 1, 1, 0 },
				{ -0.5f, h - 0.5f, 0, 1, 0, 1 }, { w - 0.5f, h - 0.5f, 0, 1, 1, 1 } };
			device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));
			saved->Apply();
			Composes++;
		}
		else Logger::Log("ExactPassSums: fold-in skipped (state block or copy failed): this frame's run keeps its separate sums wrong way");
		if (saved) saved->Release();
		Scene->Release();
		Scene = nullptr;
	}

	inline void BeginFrame(int mode) {
		if (mode != LastMode) { LastMode = mode; LogFrames = 3; } else if (LogFrames > 0) LogFrames--;
		Mode = Failed ? 0 : mode;
		Runs[0] = Runs[1] = Runs[2] = 0;
		const bool foldIn = ((Mode & Highlights) && !(Mode & kHighlightIncrements)) || ((Mode & Lamps) && !(Mode & kIncrements));
		if (foldIn && ++Frames >= 600) { Logger::Log("ExactPassSums (mode %d): %.2f fold-ins per frame", Mode, Composes / (float)Frames); Frames = Composes = 0; }
		if ((Mode & kIncrements) && (Mode & Lamps) && ++IncFrames >= 600) {
			Logger::Log("UNOFFICIAL LampSums: per frame %.2f lamp runs copied (%.1f extra lamp passes each), %.2f extra lamp passes left as before (past the run cap), "
				"%.2f copies failed", IncCopies / (float)IncFrames, IncCopies ? IncPasses / (float)IncCopies : 0.0f, IncLeft / (float)IncFrames, IncFailed / (float)IncFrames);
			IncFrames = IncCopies = IncPasses = IncLeft = IncFailed = 0;
		}
		if ((Mode & kHighlightIncrements) && (Mode & Highlights) && ++IncHlFrames >= 600) {
			Logger::Log("UNOFFICIAL HighlightSums: per frame %.2f highlight runs copied (%.1f highlight passes each), %.2f highlight passes left as before (past the run cap)",
				IncHlCopies / (float)IncHlFrames, IncHlCopies ? IncHlPasses / (float)IncHlCopies : 0.0f, IncHlLeft / (float)IncHlFrames);
			IncHlFrames = IncHlCopies = IncHlPasses = IncHlLeft = 0;
		}
		if (!foldIn && AccTex) Release();
		const bool anyIncrements = ((Mode & kIncrements) && (Mode & Lamps)) || ((Mode & kHighlightIncrements) && (Mode & Highlights));
		if (!anyIncrements && IncTex) ReleaseIncrements();
	}

	inline int ModeFromSettings() {
		if (!LampSumsChecked) {
			LampSumsChecked = true;
			LampSumsCompiled = TheSettingManager->GetSettingI("Shaders.PBR.Main", "LampSums") != 0 && TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") != 0;
			Logger::Log("UNOFFICIAL LampSums: %s", LampSumsCompiled ? "compiled in (the extra lamp passes add their light in linear light)" :
				"off (not compiled into the shaders)");
		}
		if (!HighlightSumsChecked) {
			HighlightSumsChecked = true;
			HighlightSumsCompiled = TheSettingManager->GetSettingI("Shaders.PBR.Main", "HighlightSums") != 0 && TheSettingManager->GetSettingI("Shaders.PBR.Linear", "Enabled") != 0;
			Logger::Log("UNOFFICIAL HighlightSums: %s", HighlightSumsCompiled ? "compiled in (the highlight passes FoldHighlights leaves to the game add their light in linear light)" :
				"off (not compiled into the shaders)");
		}
		const int exact = TheSettingManager->GetSettingI("Shaders.PBR.Linear", "ExactPassSums");
		int mode = 0;
		if (LampSumsCompiled && Gamma > 1.0f && TheSettingManager->GetSettingI("Shaders.PBR.Main", "LampSums")) mode |= kIncrements | Lamps;
		if (HighlightSumsCompiled && Gamma > 1.0f && TheSettingManager->GetSettingI("Shaders.PBR.Main", "HighlightSums")) mode |= kHighlightIncrements | Highlights;
		if (exact & Lamps) mode = (mode & ~(kIncrements | Lamps)) | Lamps;
		if (exact & Highlights) mode = (mode & ~(kHighlightIncrements | Highlights)) | Highlights;
		return mode;
	}

	inline void BeforePass(const void* pass, bool world, IDirect3DDevice9* device) {
		if (!Mode || !pass || !device) return;
		if (!world) { if (Active) End(device); return; }
		const UInt16 e = *(const UInt16*)((const UInt8*)pass + 0x04);
		const Kind k = KindOf(e);
		if (k == Active) {
			RunPasses++; RunLast = e;
			if (IncLive) TheRenderManager->renderState->SetTexture(IncStage, IncTex);
			return;
		}
		if (Active) {
			if (LogFrames > 0) Logger::Log("ExactPassSums run: kind %d (%s), %u passes (%03X..%03X), ended by pass %03X", (int)Active,
				RunIsIncrements ? (Active == Highlights ? "HighlightSums" : "LampSums") : "fold-in", RunPasses, RunFirst, RunLast, e);
			End(device);
		}
		const bool increments = (k == Lamps && (Mode & kIncrements)) || (k == Highlights && (Mode & kHighlightIncrements));
		const int cap = increments ? kIncrementRunCap : kFoldRunCap;
		if (k != None && (Mode & k) && Runs[k] >= cap) {
			if (LogFrames > 0) Logger::Log("ExactPassSums: pass %03X of kind %d NOT redirected (run %d this frame, past the cap of %d)", e, (int)k, Runs[k] + 1, cap);
			if (k == Lamps && (Mode & kIncrements)) IncLeft++;
			else if (k == Highlights && (Mode & kHighlightIncrements)) IncHlLeft++;
		}
		if (k != None && (Mode & k) && Runs[k] < cap) {
			if (increments) BeginIncrements(device, k); else Begin(device, k);
			RunPasses = 1; RunFirst = RunLast = e;
		}
	}

	inline void EndWorld(IDirect3DDevice9* device) {
		if (!Active || !device) return;
		if (LogFrames > 0) Logger::Log("ExactPassSums run: kind %d (%s), %u passes (%03X..%03X), ended by the world's end", (int)Active,
			RunIsIncrements ? (Active == Highlights ? "HighlightSums" : "LampSums") : "fold-in", RunPasses, RunFirst, RunLast);
		End(device);
	}
}
