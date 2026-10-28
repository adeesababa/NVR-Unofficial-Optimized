#pragma once

#include <d3d9.h>
#include <unordered_set>
#include "DxvkCompute.h"

namespace CapturedNormals {

	inline bool Checked = false, Compiled = false;
	inline bool Live = false;
	inline bool Failed = false, Hooked = false, MsaaWarned = false;
	inline bool InSpan = false, Bound = false, Drawn = false;
	inline IDirect3DSurface9* SpanTarget = nullptr;
	inline IDirect3DTexture9* Texture = nullptr;  inline IDirect3DSurface9* TextureSurface = nullptr;
	inline IDirect3DSurface9* MsaaSurface = nullptr;
	inline D3DSURFACE_DESC Made = {};
	inline int Recreations = 0;
	inline D3DXVECTOR4 Data = { 0.0f, 0.004f, 2.0f, 0.5f };
	inline std::unordered_set<IDirect3DPixelShader9*> Writers;
	inline IDirect3DPixelShader9* LastPS = nullptr;  inline bool LastWriter = false;
	inline UInt32 Frames = 0, Writes = 0, Masked = 0, Unbinds = 0;

	inline void NoteWriter(IDirect3DPixelShader9* handle) { if (handle) Writers.insert(handle); }

	inline IDirect3DSurface9* CheckTarget = nullptr;  inline IDirect3DSurface9* CheckCopy = nullptr;
	inline UInt32 LiveFrames = 0, CheckFailures = 0;
	inline bool CheckThisFrame = false, CheckReported = false;

	inline bool IsCheckFrame(UInt32 f) { return f == 60 || f == 300 || f == 1200 || (f > 1200 && f % 18000 == 0); }

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
			for (int x = 0; x < 64; x++) if (row[x * 4 + 3] == 0x3C00) written++;
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

	inline bool CompiledIn() {
		if (!Checked) {
			Checked = true;
			Compiled = TheSettingManager->GetSettingI("Shaders.SnowAccumulation.Main", "CapturedNormals") != 0 ||
				TheSettingManager->GetSettingI("Main.Main.Quality", "CapturedNormals") != 0;
		}
		return Compiled;
	}

	inline bool MergeWanted() { return CompiledIn() && TheSettingManager->GetSettingI("Main.Main.Quality", "CapturedNormals") != 0; }
	inline IDirect3DPixelShader9* MergePS = nullptr;
	inline bool MergeFailed = false, MergeLogged = false;
	inline UInt32 Merges = 0;

	inline bool MergeIntoNormals(IDirect3DDevice9* d, IDirect3DSurface9* normals, IDirect3DBaseTexture9* depth) {
		if (MergeFailed || Failed || Data.x <= 0.0f || !Texture || !normals || !depth) return false;
		auto constant = [](const char* name) -> const D3DXVECTOR4* {
			auto found = TheShaderManager->ConstantsTable.find(name);
			return found == TheShaderManager->ConstantsTable.end() ? nullptr : found->second;
		};
		const D3DXMATRIX* view = (const D3DXMATRIX*)constant("TESR_ViewTransform");
		const D3DXMATRIX* proj = (const D3DXMATRIX*)constant("TESR_ProjectionTransform");
		const D3DXVECTOR4* camera = constant("TESR_CameraData");
		const D3DXVECTOR4* scale = constant("TESR_RenderScale");
		if (!view || !proj || !camera) return false;
		if (!MergePS) {
			static const char* kPS =
				"sampler2D C : register(s0); sampler2D D : register(s1);"
				"float4 P : register(c0); float4 R : register(c1); float4 G : register(c2); row_major float4x4 V : register(c3);"
				"float4 main(float2 uv : TEXCOORD0) : COLOR0 {"
				"  float4 c = tex2D(C, uv); float z = tex2D(D, uv).x * P.z;"
				"  float2 s = uv * R.zw;"
				"  float3 vp = float3((s.x * 2.0f - 1.0f) * P.x, (1.0f - s.y * 2.0f) * P.y, 1.0f) * z;"
				"  float3 fn = cross(ddy(vp), ddx(vp)); fn = normalize(fn + float3(0.0f, 0.0f, 1.0e-20f)); fn *= dot(fn, vp) > 0.0f ? -1.0f : 1.0f;"
				"  float3 n = float3(c.rg, 1.0f - abs(c.r) - abs(c.g)); float t = saturate(-n.z);"
				"  n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t); n = normalize(n);"
				"  float3 nv = normalize(mul(n, (float3x3)V));"
				"  float d = length(vp);"
				"  clip(c.a > 0.99f && abs(c.b - d) <= d * G.x + G.y && dot(nv, fn) > G.z && z < P.z * 0.99f ? 1.0f : -1.0f);"
				"  return float4(nv * 0.5f + 0.5f, 1.0f); }";
			ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
			if (FAILED(D3DXCompileShader(kPS, (UINT)strlen(kPS), NULL, NULL, "main", "ps_3_0", 0, &code, &errors, NULL)) || !code ||
				FAILED(d->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &MergePS))) {
				Logger::Log("[ERROR] CapturedNormals: the merge shader failed (%s): the normals buffer stays depth-rebuilt", errors ? (const char*)errors->GetBufferPointer() : "?");
				MergeFailed = true;
			}
			if (errors) errors->Release();
			if (code) code->Release();
			if (MergeFailed) return false;
		}
		D3DSURFACE_DESC t = {};
		normals->GetDesc(&t);

		IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr; IDirect3DVertexDeclaration9* decl = nullptr;
		IDirect3DVertexBuffer9* vb = nullptr; UINT vbOffset = 0, vbStride = 0; IDirect3DBaseTexture9* tex[2] = {};
		IDirect3DSurface9* rt0 = nullptr; D3DVIEWPORT9 vp = {};
		DWORD fvf = 0; float saved[7][4] = {};
		d->GetVertexShader(&vs); d->GetPixelShader(&ps); d->GetVertexDeclaration(&decl); d->GetFVF(&fvf);
		d->GetStreamSource(0, &vb, &vbOffset, &vbStride); d->GetTexture(0, &tex[0]); d->GetTexture(1, &tex[1]);
		d->GetRenderTarget(0, &rt0); d->GetViewport(&vp); d->GetPixelShaderConstantF(0, &saved[0][0], 7);
		static const D3DRENDERSTATETYPE kRS[] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE,
			D3DRS_SCISSORTESTENABLE, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_FILLMODE, D3DRS_SRGBWRITEENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE };
		const DWORD want[] = { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, 0xFu, D3DFILL_SOLID, FALSE, FALSE, 0 };
		static_assert(sizeof(kRS) / sizeof(kRS[0]) == sizeof(want) / sizeof(want[0]), "one value per state");
		DWORD savedRS[sizeof(kRS) / sizeof(kRS[0])];
		for (unsigned i = 0; i < sizeof(kRS) / sizeof(kRS[0]); i++) { if (FAILED(d->GetRenderState(kRS[i], &savedRS[i]))) savedRS[i] = want[i]; if (savedRS[i] != want[i]) d->SetRenderState(kRS[i], want[i]); }
		static const D3DSAMPLERSTATETYPE kSS[] = { D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_SRGBTEXTURE };
		const DWORD wantS[] = { D3DTEXF_POINT, D3DTEXF_POINT, D3DTEXF_NONE, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, FALSE };
		DWORD savedS[2][sizeof(kSS) / sizeof(kSS[0])];
		for (DWORD s = 0; s < 2; s++)
			for (unsigned i = 0; i < sizeof(kSS) / sizeof(kSS[0]); i++) { if (FAILED(d->GetSamplerState(s, kSS[i], &savedS[s][i]))) savedS[s][i] = wantS[i]; if (savedS[s][i] != wantS[i]) d->SetSamplerState(s, kSS[i], wantS[i]); }

		d->SetRenderTarget(0, normals);
		D3DVIEWPORT9 full = { 0, 0, t.Width, t.Height, 0.0f, 1.0f };
		d->SetViewport(&full);
		d->SetVertexShader(nullptr);
		d->SetPixelShader(MergePS);
		const float c[7][4] = {
			{ 1.0f / proj->_11, 1.0f / proj->_22, camera->y, 0.0f },
			{ scale ? scale->x : 1.0f, scale ? scale->y : 1.0f, scale ? scale->z : 1.0f, scale ? scale->w : 1.0f },
			{ Data.y, Data.z, Data.w, 0.0f },
			{ view->_11, view->_12, view->_13, view->_14 }, { view->_21, view->_22, view->_23, view->_24 },
			{ view->_31, view->_32, view->_33, view->_34 }, { view->_41, view->_42, view->_43, view->_44 } };
		d->SetPixelShaderConstantF(0, &c[0][0], 7);
		d->SetTexture(0, Texture);
		d->SetTexture(1, depth);
		d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
		const float x1 = (float)t.Width - 0.5f, y1 = (float)t.Height - 0.5f;
		const float quad[4][6] = { { -0.5f, -0.5f, 0, 1, 0, 0 }, { x1, -0.5f, 0, 1, 1, 0 }, { -0.5f, y1, 0, 1, 0, 1 }, { x1, y1, 0, 1, 1, 1 } };
		d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0]));

		for (DWORD s = 0; s < 2; s++)
			for (unsigned i = 0; i < sizeof(kSS) / sizeof(kSS[0]); i++) if (savedS[s][i] != wantS[i]) d->SetSamplerState(s, kSS[i], savedS[s][i]);
		for (unsigned i = 0; i < sizeof(kRS) / sizeof(kRS[0]); i++) if (savedRS[i] != want[i]) d->SetRenderState(kRS[i], savedRS[i]);
		if (rt0) { d->SetRenderTarget(0, rt0); rt0->Release(); }
		d->SetViewport(&vp);
		for (DWORD s = 0; s < 2; s++) { d->SetTexture(s, tex[s]); if (tex[s]) tex[s]->Release(); }
		d->SetPixelShaderConstantF(0, &saved[0][0], 7);
		if (decl) { d->SetVertexDeclaration(decl); decl->Release(); } else d->SetFVF(fvf);
		d->SetStreamSource(0, vb, vbOffset, vbStride); if (vb) vb->Release();
		d->SetVertexShader(vs); if (vs) vs->Release();
		d->SetPixelShader(ps); if (ps) ps->Release();
		if (!MergeLogged) { MergeLogged = true; Logger::Log("CapturedNormals: merged into the normals buffer (%ux%u) for every effect that reads it", t.Width, t.Height); }
		Merges++;
		return true;
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
		if (Texture && ++Recreations > 4) {
			Logger::Log("[WARNING] CapturedNormals: the world's main list draws into changing targets; off for the session");
			Release();
			Failed = true;
			return false;
		}
		Release();
		bool ok = SUCCEEDED(d->CreateTexture(t.Width, t.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &Texture, NULL)) &&
			SUCCEEDED(Texture->GetSurfaceLevel(0, &TextureSurface));
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
		IDirect3DSurface9* rt3 = nullptr;
		if (SUCCEEDED(d->GetRenderTarget(3, &rt3)) && rt3) rt3->Release();
		Bound = rt3 && rt3 == DrawSurface();
		IDirect3DSurface9* rt0 = nullptr;
		d->GetRenderTarget(0, &rt0);
		if (rt0) rt0->Release();
		if (rt0 != SpanTarget) { if (Bound) Unbind(d); return; }
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
		if (InSpan) {
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
		Hooked = true;
	}

	inline UInt32 Reinstalls = 0;
	inline void EnsureHooked(IDirect3DDevice9* d) {
		if (!Hooked || !d) return;
		void** table = *(void***)d;
		struct Slot { int i; void* ours; void* next; };
		const Slot slots[5] = { { 43, (void*)&ClearHook, (void*)NextClear }, { 81, (void*)&DPHook, (void*)NextDP }, { 82, (void*)&DIPHook, (void*)NextDIP },
			{ 83, (void*)&DPUPHook, (void*)NextDPUP }, { 84, (void*)&DIPUPHook, (void*)NextDIPUP } };
		for (const Slot& s : slots) {
			if (table[s.i] == s.ours || table[s.i] != s.next) continue;
			DWORD old = 0;
			if (!VirtualProtect(&table[s.i], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) continue;
			table[s.i] = s.ours;
			VirtualProtect(&table[s.i], sizeof(void*), old, &old);
			if (++Reinstalls <= 3) Logger::Log("CapturedNormals: device table slot %d had been put back to the runtime's entry: hook put in again (logged 3 times)", s.i);
		}
	}

	inline void BeginFrame(IDirect3DDevice9* d, bool wanted) {
		Data.x = 0.0f;
		Live = CompiledIn() && !Failed && wanted;
		if (Live && !Hooked) {
			Hook(d);
			Logger::Log("CapturedNormals: on (%u template pixel shaders write the normal target)", (unsigned)Writers.size());
		}
		if (Live && ++Frames >= 600) {
			Logger::Log("CapturedNormals: per frame %.1f draws wrote normals, %.1f masked, %.2f unbinds (other targets, clears); %u hook reinstalls so far",
				Writes / (double)Frames, Masked / (double)Frames, Unbinds / (double)Frames, Reinstalls);
			Frames = Writes = Masked = Unbinds = 0;
		}
	}

	inline void BeginSpan(IDirect3DDevice9* d) {
		if (!Live || InSpan) return;
		IDirect3DSurface9* rt0 = nullptr;
		if (FAILED(d->GetRenderTarget(0, &rt0)) || !rt0) return;
		D3DSURFACE_DESC t = {};
		rt0->GetDesc(&t);
		if (t.Width != TheRenderManager->width || t.Height != TheRenderManager->height) { rt0->Release(); return; }
		if (t.MultiSampleType != D3DMULTISAMPLE_NONE && !DxvkCompute::Available()) {
			if (!MsaaWarned) { MsaaWarned = true; Logger::Log("[WARNING] CapturedNormals: the scene is multisampled (the game's MSAA) on native D3D9, where extra multisampled targets draw nothing: off while MSAA is on (fine under NVR DXVK)"); }
			rt0->Release();
			return;
		}
		if (!Ensure(d, t)) { rt0->Release(); return; }
		EnsureHooked(d);
		SpanTarget = rt0;
		d->ColorFill(DrawSurface(), NULL, D3DCOLOR_ARGB(0, 0, 0, 0));
		CheckThisFrame = IsCheckFrame(++LiveFrames);
		if (CheckThisFrame) {
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
		if (MsaaSurface) d->StretchRect(MsaaSurface, NULL, TextureSurface, NULL, D3DTEXF_NONE);
		if (CheckThisFrame && Drawn) {
			const int n = CountWritten(d);
			if (n >= 0) CheckResult(n > 0, "after the main list", n);
		}
		CheckThisFrame = false;
		Data.x = Drawn && !Failed ? 1.0f : 0.0f;
	}
}
