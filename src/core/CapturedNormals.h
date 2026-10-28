#pragma once

// UNOFFICIAL 2026-10-28 CapturedNormals (docs/next/CAPTURED-NORMALS.md; tester macintroll's proposal, first reader: SnowAccumulation,
// [Shaders.SnowAccumulation.Main] CapturedNormals). Every screen-space effect reads TESR_NormalsBuffer, rebuilt from the depth buffer:
// one flat normal per triangle, noisy at edges and under MSAA / FSR (triangle-shaped snow on rocks, cloud shadow crust, striped facing
// terms). Here the surfaces' own smooth normals are written while they draw:
//   - while the world's main opaque list draws (RenderBatches(1, 0x25B), GrassFirst::MainListHook), a screen-sized fp16 target is bound as
//     render target 3, cleared to "nothing" first; NVR's lit templates (ObjectTemplate, ParallaxTemplate, SkinTemplate, TerrainTemplate,
//     compiled with CAPTURED_NORMALS_ON) write COLOR3 = the smooth interpolated vertex normal (world, after skinning and tessellation,
//     BEFORE the normal map; octahedral in rg), the camera distance (b) and 1 (a): Shaders/Includes/CapturedNormals.hlsl;
//   - a draw hook keeps everything else out: the target is written only by those templates drawn opaque (or with the land's
//     SRCALPHA / INVSRCALPHA fade: they write alpha 1, a replace) into the scene target; the game's own shaders, additive lamp and
//     highlight passes, other blends and depth-only draws have its write mask off (COLORWRITEENABLE3 = 0); a draw into any other target,
//     and any Clear, has it unbound (MRT targets must match in size; Clear clears every bound target). LinearScene's full-screen
//     conversions unbind targets 1-3 themselves;
//   - after the list the multisampled copy (the game's MSAA) is resolved; RenderScale v2 frames need nothing: the world draws into the
//     target's top-left part exactly as into the scene's, so the effects' rect coordinates match.
// Readers (Effects/Includes/CapturedNormals.hlsl) compare the stored distance with the depth buffer's at the pixel and use the normal
// only where they agree; anywhere else (the game's own shaders, about 9 % of the list's draws in the LinearScene census, alpha-blended
// and see-through things, water, grass, decals and particles drawn later, the first-person view) they keep the depth-rebuilt normal.
// Off (default): not compiled into the shaders, nothing hooked, no target.
#include <d3d9.h>
#include <unordered_set>
#include "DxvkCompute.h"

namespace CapturedNormals {

	inline bool Checked = false, Compiled = false;	// on at startup (the templates write COLOR3)
	inline bool Live = false;					// this frame: compiled, wanted by a reader, not failed
	inline bool Failed = false, Hooked = false, MsaaWarned = false;
	inline bool InSpan = false, Bound = false, Drawn = false;
	inline IDirect3DSurface9* SpanTarget = nullptr;	// the scene target the list began with
	inline IDirect3DTexture9* Texture = nullptr;  inline IDirect3DSurface9* TextureSurface = nullptr;	// what the readers sample (TESR_CapturedNormalsBuffer)
	inline IDirect3DSurface9* MsaaSurface = nullptr;	// with the game's MSAA: what the draws write, resolved into Texture after the list
	inline D3DSURFACE_DESC Made = {};
	inline int Recreations = 0;
	inline D3DXVECTOR4 Data = { 0.0f, 0.004f, 2.0f, 0.0f };	// TESR_CapturedNormalsData: x 1 = holds this frame's, y z the distance match's share and units
	inline std::unordered_set<IDirect3DPixelShader9*> Writers;	// the templates' pixel shaders compiled with CAPTURED_NORMALS_ON (ShaderRecord)
	inline IDirect3DPixelShader9* LastPS = nullptr;  inline bool LastWriter = false;
	inline UInt32 Frames = 0, Writes = 0, Masked = 0, Unbinds = 0;

	inline void NoteWriter(IDirect3DPixelShader9* handle) { if (handle) Writers.insert(handle); }

	// The self-check (main session, 2026-10-28: P31's MergedDepthNormals wrote two targets in one draw, and on native D3D9 the GTX 1070
	// driver silently dropped that draw: the buffers stopped updating and every effect read stale data). On a few frames (the 60th, 300th
	// and 1200th captured frame, then every 18,000th) the target is read back, shrunk to 64 x 64 texels: right after the clear it must hold
	// no written texel (the clear really happened), after the list at least one (the draws really wrote it) when templates drew. A
	// failure switches the capture off for the session (logged): the readers keep the depth-rebuilt normal. A read-back waits for the GPU
	// once, on those frames only. One failure switches it off (a dropped draw also drops the scene there: no waiting).
	inline IDirect3DSurface9* CheckTarget = nullptr;  inline IDirect3DSurface9* CheckCopy = nullptr;
	inline UInt32 LiveFrames = 0, CheckFailures = 0;
	inline bool CheckThisFrame = false, CheckReported = false;

	inline bool IsCheckFrame(UInt32 f) { return f == 60 || f == 300 || f == 1200 || (f > 1200 && f % 18000 == 0); }

	// Texels written (alpha 1) in a 64 x 64 point-sampled copy of the target; -1 if it could not be read.
	inline int CountWritten(IDirect3DDevice9* d) {
		if (!CheckTarget && (FAILED(d->CreateRenderTarget(64, 64, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE, 0, FALSE, &CheckTarget, NULL)) ||
			FAILED(d->CreateOffscreenPlainSurface(64, 64, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &CheckCopy, NULL)))) {
			if (CheckTarget) { CheckTarget->Release(); CheckTarget = nullptr; }
			return -1;
		}
		if (FAILED(d->StretchRect(TextureSurface, NULL, CheckTarget, NULL, D3DTEXF_POINT)) || FAILED(d->GetRenderTargetData(CheckTarget, CheckCopy))) return -1;
		D3DLOCKED_RECT r = {};
		if (FAILED(CheckCopy->LockRect(&r, NULL, D3DLOCK_READONLY))) return -1;
		int written = 0;
		for (int y = 0; y < 64; y++) {
			const UInt16* row = (const UInt16*)((const char*)r.pBits + y * r.Pitch);
			for (int x = 0; x < 64; x++) if (row[x * 4 + 3] == 0x3C00) written++;	// alpha exactly 1.0 (half float)
		}
		CheckCopy->UnlockRect();
		return written;
	}

	inline void CheckResult(bool ok, const char* what, int count) {
		Logger::Log("CapturedNormals: self-check at captured frame %u, %s: %d of 4096 texels written -> %s", LiveFrames, what, count, ok ? "ok" : "WRONG");
		if (ok) return;
		CheckFailures++;
		Logger::Log("[WARNING] CapturedNormals: the normal target does not update (the driver drops the draws or the clear?): off for the session; the effects keep the depth-rebuilt normals");
		Failed = true;
	}

	// Read once (the shaders are compiled with it): any reader's switch at startup.
	inline bool CompiledIn() {
		if (!Checked) {
			Checked = true;
			Compiled = TheSettingManager->GetSettingI("Shaders.SnowAccumulation.Main", "CapturedNormals") != 0;
		}
		return Compiled;
	}

	typedef HRESULT(STDMETHODCALLTYPE* DIPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* DPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* DPUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, CONST void*, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* DIPUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT);
	typedef HRESULT(STDMETHODCALLTYPE* ClearFn)(IDirect3DDevice9*, DWORD, CONST D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
	inline DIPFn NextDIP = nullptr;  inline DPFn NextDP = nullptr;  inline DPUPFn NextDPUP = nullptr;  inline DIPUPFn NextDIPUP = nullptr;
	inline ClearFn NextClear = nullptr;

	inline void Release() {
		if (MsaaSurface) { MsaaSurface->Release(); MsaaSurface = nullptr; }
		if (TextureSurface) { TextureSurface->Release(); TextureSurface = nullptr; }
		if (Texture) { Texture->Release(); Texture = nullptr; }
		Made = {};
	}

	inline IDirect3DSurface9* DrawSurface() { return MsaaSurface ? MsaaSurface : TextureSurface; }

	inline bool Ensure(IDirect3DDevice9* d, const D3DSURFACE_DESC& t) {
		if (Texture && Made.Width == t.Width && Made.Height == t.Height && Made.MultiSampleType == t.MultiSampleType && Made.MultiSampleQuality == t.MultiSampleQuality) return true;
		if (Texture && ++Recreations > 4) {	// the world's target never changes; something else draws its list into another full-screen target
			Logger::Log("[WARNING] CapturedNormals: the world's main list draws into changing targets; off for the session");
			Release();
			Failed = true;
			return false;
		}
		Release();
		bool ok = SUCCEEDED(d->CreateTexture(t.Width, t.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &Texture, NULL)) &&
			SUCCEEDED(Texture->GetSurfaceLevel(0, &TextureSurface));
		// MRT targets must share the scene's multisampling: a multisampled copy, resolved into the texture after the list.
		if (ok && t.MultiSampleType != D3DMULTISAMPLE_NONE)
			ok = SUCCEEDED(d->CreateRenderTarget(t.Width, t.Height, D3DFMT_A16B16G16R16F, t.MultiSampleType, t.MultiSampleQuality, FALSE, &MsaaSurface, NULL));
		if (!ok) {
			Logger::Log("[ERROR] CapturedNormals: could not make the %ux%u target (msaa %u); off for the session", t.Width, t.Height, (unsigned)t.MultiSampleType);
			Release();
			Failed = true;
			return false;
		}
		Made = t;
		Logger::Log("CapturedNormals: target %ux%u, msaa %u", t.Width, t.Height, (unsigned)t.MultiSampleType);
		return true;
	}

	// Target 3 on or off. Binding a target past 0 leaves the viewport alone in D3D9; it is put back anyway (cheap, and some layers reset it).
	inline void Bind(IDirect3DDevice9* d) {
		D3DVIEWPORT9 vp; d->GetViewport(&vp);
		d->SetRenderTarget(3, DrawSurface());
		d->SetViewport(&vp);
		Bound = true;
	}
	inline void Unbind(IDirect3DDevice9* d) {
		D3DVIEWPORT9 vp; d->GetViewport(&vp);
		d->SetRenderTarget(3, nullptr);
		d->SetViewport(&vp);
		d->SetRenderState(D3DRS_COLORWRITEENABLE3, 0xF);
		Bound = false;
		Unbinds++;
	}

	inline void BeforeDraw(IDirect3DDevice9* d) {
		if (!InSpan) return;
		// Is it bound now? Read every draw: LinearScene's conversion takes targets 1-3 off and puts them back itself.
		IDirect3DSurface9* rt3 = nullptr;
		if (SUCCEEDED(d->GetRenderTarget(3, &rt3)) && rt3) rt3->Release();
		Bound = rt3 && rt3 == DrawSurface();
		IDirect3DSurface9* rt0 = nullptr;
		d->GetRenderTarget(0, &rt0);
		if (rt0) rt0->Release();	// a key only
		if (rt0 != SpanTarget) { if (Bound) Unbind(d); return; }	// a copy, a shadow map, a mask: never with ours bound
		IDirect3DPixelShader9* ps = nullptr;
		d->GetPixelShader(&ps);
		if (ps) ps->Release();
		if (ps != LastPS) { LastPS = ps; LastWriter = ps && Writers.count(ps) != 0; }
		bool write = LastWriter;
		if (write) {
			DWORD mask = 0xF, blend = FALSE, src = D3DBLEND_ONE, dst = D3DBLEND_ZERO;
			d->GetRenderState(D3DRS_COLORWRITEENABLE, &mask);
			d->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
			if (blend) { d->GetRenderState(D3DRS_SRCBLEND, &src); d->GetRenderState(D3DRS_DESTBLEND, &dst); }
			// opaque, or a SRCALPHA / INVSRCALPHA fade (the land over objects): the templates write alpha 1 there, a plain replace
			write = (mask & 7) && (!blend || (src == D3DBLEND_SRCALPHA && dst == D3DBLEND_INVSRCALPHA));
		}
		if (write) {
			if (!Bound) Bind(d);
			d->SetRenderState(D3DRS_COLORWRITEENABLE3, 0xF);
			Writes++;
			Drawn = true;
		}
		else if (Bound) {
			d->SetRenderState(D3DRS_COLORWRITEENABLE3, 0);
			Masked++;
		}
	}

	inline HRESULT STDMETHODCALLTYPE DIPHook(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT s, UINT pc) {
		BeforeDraw(d);
		return NextDIP(d, t, b, mi, nv, s, pc);
	}
	inline HRESULT STDMETHODCALLTYPE DPHook(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT s, UINT pc) {
		BeforeDraw(d);
		return NextDP(d, t, s, pc);
	}
	inline HRESULT STDMETHODCALLTYPE DPUPHook(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT pc, CONST void* v, UINT stride) {
		BeforeDraw(d);
		return NextDPUP(d, t, pc, v, stride);
	}
	inline HRESULT STDMETHODCALLTYPE DIPUPHook(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, CONST void* i, D3DFORMAT f, CONST void* v, UINT stride) {
		BeforeDraw(d);
		return NextDIPUP(d, t, mi, nv, pc, i, f, v, stride);
	}
	inline HRESULT STDMETHODCALLTYPE ClearHook(IDirect3DDevice9* d, DWORD count, CONST D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
		if (InSpan) {	// D3D9's Clear clears every bound target; the next writing draw binds it again
			IDirect3DSurface9* rt3 = nullptr;
			if (SUCCEEDED(d->GetRenderTarget(3, &rt3)) && rt3) rt3->Release();
			if (rt3 && rt3 == DrawSurface()) Unbind(d);
		}
		return NextClear(d, count, rects, flags, color, z, stencil);
	}

	inline void Hook(IDirect3DDevice9* d) {
		if (Hooked || !d) return;
		void** table = *(void***)d;
		DWORD old = 0;
		if (!VirtualProtect(&table[43], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
		NextClear = (ClearFn)table[43];  table[43] = (void*)&ClearHook;
		VirtualProtect(&table[43], sizeof(void*), old, &old);
		if (!VirtualProtect(&table[81], 4 * sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
		NextDP = (DPFn)table[81];  table[81] = (void*)&DPHook;
		NextDIP = (DIPFn)table[82];  table[82] = (void*)&DIPHook;
		NextDPUP = (DPUPFn)table[83];  table[83] = (void*)&DPUPHook;
		NextDIPUP = (DIPUPFn)table[84];  table[84] = (void*)&DIPUPHook;
		VirtualProtect(&table[81], 4 * sizeof(void*), old, &old);
		Hooked = true;	// stays for the session (other hooks chain through it); outside the list it only passes the call on
	}

	// Render.cpp, once a frame. wanted: a reader will use this frame's normals (SnowAccumulation with CapturedNormals on).
	inline void BeginFrame(IDirect3DDevice9* d, bool wanted) {
		Data.x = 0.0f;	// nothing captured yet this frame
		Live = CompiledIn() && !Failed && wanted;
		if (Live && !Hooked) {
			Hook(d);
			Logger::Log("CapturedNormals: on (%u template pixel shaders write the normal target)", (unsigned)Writers.size());
		}
		if (Live && ++Frames >= 600) {
			Logger::Log("CapturedNormals: per frame %.1f draws wrote normals, %.1f masked, %.2f unbinds (other targets, clears)",
				Writes / (double)Frames, Masked / (double)Frames, Unbinds / (double)Frames);
			Frames = Writes = Masked = Unbinds = 0;
		}
	}

	// GrassFirst::MainListHook, around the game's RenderBatches(1, 0x25B) call (world context only).
	inline void BeginSpan(IDirect3DDevice9* d) {
		if (!Live || InSpan) return;
		IDirect3DSurface9* rt0 = nullptr;
		if (FAILED(d->GetRenderTarget(0, &rt0)) || !rt0) return;
		D3DSURFACE_DESC t = {};
		rt0->GetDesc(&t);
		// Reflections, menus and other views draw the list into smaller targets: the main view only.
		if (t.Width != TheRenderManager->width || t.Height != TheRenderManager->height) { rt0->Release(); return; }
		// Measured by SkinScattering on a GTX 1070 (native D3D9): with multisampled extra targets bound a draw writes nothing at all to
		// target 0. Under DXVK it works; on native D3D9 the game's MSAA keeps the depth-rebuilt normals.
		if (t.MultiSampleType != D3DMULTISAMPLE_NONE && !DxvkCompute::Available()) {
			if (!MsaaWarned) { MsaaWarned = true; Logger::Log("[WARNING] CapturedNormals: the scene is multisampled (the game's MSAA) on native D3D9, where extra multisampled targets draw nothing: off while MSAA is on (fine under NVR DXVK)"); }
			rt0->Release();
			return;
		}
		if (!Ensure(d, t)) { rt0->Release(); return; }
		SpanTarget = rt0;	// (holds the reference until EndSpan)
		d->ColorFill(DrawSurface(), NULL, D3DCOLOR_ARGB(0, 0, 0, 0));	// "nothing here": readers keep the depth-rebuilt normal
		CheckThisFrame = IsCheckFrame(++LiveFrames);
		if (CheckThisFrame) {	// the self-check's first half: the clear must have emptied it
			if (MsaaSurface) d->StretchRect(MsaaSurface, NULL, TextureSurface, NULL, D3DTEXF_NONE);
			const int n = CountWritten(d);
			if (n < 0) { CheckThisFrame = false; if (!CheckReported) { CheckReported = true; Logger::Log("CapturedNormals: self-check could not read the target back (skipped)"); } }
			else CheckResult(n == 0, "after the clear", n);
			if (Failed) { SpanTarget = nullptr; rt0->Release(); return; }
		}
		InSpan = true;
		Bound = false;
		Drawn = false;
		LastPS = nullptr;
	}

	inline void EndSpan(IDirect3DDevice9* d) {
		if (!InSpan) return;
		IDirect3DSurface9* rt3 = nullptr;
		if (SUCCEEDED(d->GetRenderTarget(3, &rt3)) && rt3) rt3->Release();
		if (rt3 && rt3 == DrawSurface()) Unbind(d);
		InSpan = false;
		if (SpanTarget) { SpanTarget->Release(); SpanTarget = nullptr; }
		if (MsaaSurface) d->StretchRect(MsaaSurface, NULL, TextureSurface, NULL, D3DTEXF_NONE);	// the game's MSAA: resolved for the readers
		if (CheckThisFrame && Drawn) {	// the self-check's second half: templates drew, so something must be written
			const int n = CountWritten(d);
			if (n >= 0) CheckResult(n > 0, "after the main list", n);
		}
		CheckThisFrame = false;
		Data.x = Drawn && !Failed ? 1.0f : 0.0f;
	}
}
