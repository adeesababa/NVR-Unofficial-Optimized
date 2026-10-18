#pragma once

#include <d3dx9.h>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace NewGrass {

	struct SettingsStruct {
		bool	Enabled = false;
		bool	KeepCards = true;
		float	BladeDistance = 7000.0f;
		int		Blades = 12;
		float	LodNear = 1000.0f, LodFar = 1500.0f, WidenDistance = 5000.0f;
		float	HeightScale = 0.8f, WidthRatio = 0.015f, Spread = 0.15f, Bend = 0.5f, Flutter = 0.15f;
		float	TipReach = 1.0f;
		float	Kink = 0.5f;
		float	LengthMin = 0.4f;
		float	Droop = 0.5f;
		float	FillRef = 0.15f;
		bool	ReceiveShadows = false;
		float	ShadowLift = 10.0f;
		float	Wrap = 0.0f, TipLift = 0.3f, Variation = 0.2f;
		float	NormalBlend = 1.0f;
		float	SkyLight = 0.0f;
		bool	GameLighting = true;
		int		Log = 0;
	};
	inline SettingsStruct S;

	inline const unsigned kTufts = 228, kMaxBlades = 32;
	inline bool Active = false;
	inline bool Hooked = false;
	typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	inline DrawIndexedPrimitiveFn Next = nullptr;

	inline ShaderRecordVertex* VS = nullptr;
	inline ShaderRecordPixel* PS = nullptr;
	inline ShaderRecordPixel* PSNoShadow = nullptr;
	inline bool LoadTried = false, LoadFailed = false;
	inline IDirect3DVertexDeclaration9* Decl = nullptr;
	struct Variant { IDirect3DVertexBuffer9* VB = nullptr; IDirect3DIndexBuffer9* IB = nullptr; unsigned Levels = 0, VertsPerBlade = 0, TrisPerBlade = 0, Verts = 0; };
	inline Variant Variants[3];

	struct Model { unsigned VertsPerModel = 0, Capacity = 0, Total = 0; float Height = 0.0f, Width = 0.0f; bool Card = false, FromBound = false; float Rects[4][4] = { { 0.0f, 0.0f, 1.0f, 1.0f } }; unsigned RectCount = 1; };
	inline std::unordered_map<const void*, Model> Models;
	struct Ramp { float C[4][4] = {}; float Bright[4][4] = {}; float Fill[4] = {}, Radius[4] = {}, Edge[4] = {}; float Top = 1.0f; bool Read = false, Cutout = false; float Coverage = 1.0f; ULONGLONG When = 0; const void* Texture = nullptr; char Info[384] = ""; };
	inline std::unordered_map<const void*, Ramp> Ramps;
	inline IDirect3DVertexShader9* RampVS = nullptr;
	inline IDirect3DPixelShader9* RampPS = nullptr;
	inline IDirect3DVertexDeclaration9* RampDecl = nullptr;
	inline IDirect3DTexture9* RampTex = nullptr;
	inline IDirect3DSurface9* RampSurf = nullptr;
	inline IDirect3DSurface9* RampSys = nullptr;
	inline bool RampGpuFailed = false;

	inline bool Armed = false, Replaced = false;
	inline void* ArmPass = nullptr;
	inline float ArmNearest = 0.0f;
	inline int ArmPart = 0;
	inline char LastVariant = 0;

	inline unsigned StatReplaced = 0, StatLetThrough = 0, StatSwallowed = 0, StatTufts = 0, StatVerts = 0, StatTris = 0, StatFar = 0, StatNotCard = 0, StatNormals = 0, Frames = 0;

	inline bool CreateResources(IDirect3DDevice9* device) {
		if (Decl) return true;
		D3DVERTEXELEMENT9 elements[] = { { 0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
		if (FAILED(device->CreateVertexDeclaration(elements, &Decl))) { Decl = nullptr; Logger::Log("UNOFFICIAL NewGrass: no vertex declaration (UBYTE4): off"); return false; }
		const unsigned levels[3] = { 4, 3, 2 };
		for (int v = 0; v < 3; v++) {
			Variant& var = Variants[v];
			var.Levels = levels[v];
			var.VertsPerBlade = 2 * (var.Levels - 1) + 1;
			var.TrisPerBlade = 2 * (var.Levels - 2) + 1;
			var.Verts = kTufts * kMaxBlades * var.VertsPerBlade;
			std::vector<unsigned char> vb(var.Verts * 4);
			std::vector<unsigned short> ib(kTufts * kMaxBlades * var.TrisPerBlade * 3);
			unsigned ii = 0;
			for (unsigned b = 0; b < kMaxBlades; b++) for (unsigned t = 0; t < kTufts; t++) {
				const unsigned base = (b * kTufts + t) * var.VertsPerBlade;
				for (unsigned l = 0; l + 1 < var.Levels; l++) for (unsigned s = 0; s < 2; s++) {
					unsigned char* p = &vb[(base + 2 * l + s) * 4];
					p[0] = (unsigned char)t; p[1] = (unsigned char)b; p[2] = (unsigned char)l; p[3] = (unsigned char)s;
				}
				unsigned char* tip = &vb[(base + 2 * (var.Levels - 1)) * 4];
				tip[0] = (unsigned char)t; tip[1] = (unsigned char)b; tip[2] = (unsigned char)(var.Levels - 1); tip[3] = 0;
				for (unsigned l = 0; l + 2 < var.Levels; l++) {
					const unsigned a = base + 2 * l;
					ib[ii++] = (unsigned short)a;       ib[ii++] = (unsigned short)(a + 1); ib[ii++] = (unsigned short)(a + 2);
					ib[ii++] = (unsigned short)(a + 1); ib[ii++] = (unsigned short)(a + 3); ib[ii++] = (unsigned short)(a + 2);
				}
				const unsigned a = base + 2 * (var.Levels - 2);
				ib[ii++] = (unsigned short)a; ib[ii++] = (unsigned short)(a + 1); ib[ii++] = (unsigned short)(base + 2 * (var.Levels - 1));
			}
			void* data = nullptr;
			if (FAILED(device->CreateVertexBuffer((UINT)vb.size(), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &var.VB, NULL)) ||
				FAILED(device->CreateIndexBuffer((UINT)(ib.size() * 2), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &var.IB, NULL)) ||
				FAILED(var.VB->Lock(0, 0, &data, 0))) {
				Logger::Log("UNOFFICIAL NewGrass: could not make the blade buffers: off");
				Decl->Release(); Decl = nullptr;
				return false;
			}
			memcpy(data, vb.data(), vb.size()); var.VB->Unlock();
			if (FAILED(var.IB->Lock(0, 0, &data, 0))) { Decl->Release(); Decl = nullptr; return false; }
			memcpy(data, ib.data(), ib.size() * 2); var.IB->Unlock();
		}
		Logger::Log("UNOFFICIAL NewGrass: blade buffers made (%u tufts x %u blades; 7/5/3 vertices per blade)", kTufts, kMaxBlades);
		return true;
	}

	inline bool LoadShaders() {
		if (LoadTried) return !LoadFailed;
		LoadTried = true;
		VS = (ShaderRecordVertex*)ShaderRecord::LoadShader("NewGrass.vso", NULL);
		PS = (ShaderRecordPixel*)ShaderRecord::LoadShader("NewGrass.pso", NULL);
		ShaderTemplate t = {};
		t.Name = "NewGrass.pso";
		const D3DXMACRO noShadow[] = { { "NEWGRASS_SHADOWS", "0" }, { NULL, NULL } };
		PSNoShadow = (ShaderRecordPixel*)ShaderRecord::LoadShader("NewGrassNoShadow.pso", NULL, t, NULL, noShadow);
		if (PS) PS->ClearSamplers = false;
		if (PSNoShadow) PSNoShadow->ClearSamplers = false;
		LoadFailed = !VS || !PS || !PSNoShadow;
		Logger::Log("UNOFFICIAL NewGrass: shaders %s (VS %p, PS %p, PS without shadows %p)", LoadFailed ? "FAILED to load: off" : "loaded", VS, PS, PSNoShadow);
		return !LoadFailed;
	}

	inline const Model& GetModel(NiGeometry* geometry) {
		NiGeometryData* gd = geometry->geomData;
		Model& m = Models[gd];
		if (m.Total && m.Total == gd->m_usVertices) return m;
		m = Model();
		m.Total = gd->m_usVertices;
		const unsigned uvSets = gd->m_usDataFlags & 0x3F;
		unsigned vpm = 0;
		if (gd->m_pkTexture && uvSets >= 2) {
			const NiPoint2* uv1 = gd->m_pkTexture + m.Total;
			while (vpm < m.Total && uv1[vpm].x < 0.5f) vpm++;
		}
		if (vpm == 0 || vpm >= m.Total) vpm = (m.Total % kTufts == 0) ? m.Total / kTufts : m.Total;
		m.VertsPerModel = vpm;
		m.Capacity = vpm ? m.Total / vpm : 0;
		if (gd->m_pkVertex && vpm) {
			float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
			for (unsigned i = 0; i < vpm; i++) {
				const NiPoint3& p = gd->m_pkVertex[i];
				const float c[3] = { p.x, p.y, p.z };
				for (int k = 0; k < 3; k++) { lo[k] = (std::min)(lo[k], c[k]); hi[k] = (std::max)(hi[k], c[k]); }
			}
			m.Height = hi[2] - lo[2];
			m.Width = 0.5f * (std::max)(hi[0] - lo[0], hi[1] - lo[1]);
		}
		else {
			m.Height = gd->m_kBound.Radius * 1.4f;
			m.Width = gd->m_kBound.Radius;
			m.FromBound = true;
		}
		if (!(m.Height > 1.0f)) m.Height = 1.0f;
		if (!(m.Width > 1.0f)) m.Width = 1.0f;
		if (gd->m_pkTexture && vpm) {
			float box[8][4]; unsigned n = 0;
			for (unsigned i = 0; i < vpm; i++) {
				const NiPoint2& t = gd->m_pkTexture[i];
				if (!(t.x >= -0.01f && t.x <= 1.01f && t.y >= -0.01f && t.y <= 1.01f)) continue;
				unsigned k = 0;
				for (; k < n; k++) if (t.x >= box[k][0] - 0.3f && t.x <= box[k][2] + 0.3f && t.y >= box[k][1] - 0.3f && t.y <= box[k][3] + 0.3f) break;
				if (k < n) { box[k][0] = (std::min)(box[k][0], t.x); box[k][1] = (std::min)(box[k][1], t.y); box[k][2] = (std::max)(box[k][2], t.x); box[k][3] = (std::max)(box[k][3], t.y); }
				else if (n < 8) { box[n][0] = box[n][2] = t.x; box[n][1] = box[n][3] = t.y; n++; }
			}
			for (bool merged = true; merged;) {
				merged = false;
				for (unsigned a = 0; a < n && !merged; a++) for (unsigned b = a + 1; b < n && !merged; b++)
					if (box[a][0] <= box[b][2] + 0.05f && box[b][0] <= box[a][2] + 0.05f && box[a][1] <= box[b][3] + 0.05f && box[b][1] <= box[a][3] + 0.05f) {
						box[a][0] = (std::min)(box[a][0], box[b][0]); box[a][1] = (std::min)(box[a][1], box[b][1]); box[a][2] = (std::max)(box[a][2], box[b][2]); box[a][3] = (std::max)(box[a][3], box[b][3]);
						for (unsigned c = b; c + 1 < n; c++) memcpy(box[c], box[c + 1], sizeof(box[c]));
						n--; merged = true;
					}
			}
			m.RectCount = 0;
			for (unsigned k = 0; k < n && m.RectCount < 4; k++) {
				const float u0 = (std::max)(box[k][0], 0.0f), v0 = (std::max)(box[k][1], 0.0f), u1 = (std::min)(box[k][2], 1.0f), v1 = (std::min)(box[k][3], 1.0f);
				if (u1 - u0 < 0.01f || v1 - v0 < 0.01f) continue;
				m.Rects[m.RectCount][0] = u0; m.Rects[m.RectCount][1] = v0; m.Rects[m.RectCount][2] = u1 - u0; m.Rects[m.RectCount][3] = v1 - v0; m.RectCount++;
			}
			if (m.RectCount == 0) { m.Rects[0][0] = m.Rects[0][1] = 0.0f; m.Rects[0][2] = m.Rects[0][3] = 1.0f; m.RectCount = 1; }
		}
		m.Card = vpm <= 64;
		Logger::Log("UNOFFICIAL NewGrass: model %p: %u vertices, %u per tuft (%u tufts), height %.1f width %.1f%s, %u texture tile(s): %.2f %.2f + %.2f %.2f | %.2f %.2f + %.2f %.2f | %.2f %.2f + %.2f %.2f | %.2f %.2f + %.2f %.2f -> %s",
			gd, m.Total, vpm, m.Capacity, m.Height, m.Width, m.FromBound ? " (from the bound)" : "", m.RectCount,
			m.Rects[0][0], m.Rects[0][1], m.Rects[0][2], m.Rects[0][3], m.Rects[1][0], m.Rects[1][1], m.Rects[1][2], m.Rects[1][3],
			m.Rects[2][0], m.Rects[2][1], m.Rects[2][2], m.Rects[2][3], m.Rects[3][0], m.Rects[3][1], m.Rects[3][2], m.Rects[3][3], m.Card ? "card" : "NOT a card (kept the game's)");
		return m;
	}

	inline void FinishRamp(Ramp& r, const double sum[4][3], const double alpha[4], float coverage, const float fill[4], const float radius[4], float top, const float bright[4][3], const float edge[4]) {
		r.Coverage = coverage;
		int last = -1;
		for (int b = 0; b < 4; b++) {
			r.Fill[b] = fill[b];
			if (alpha[b] > 0.0) {
				for (int k = 0; k < 3; k++) { r.C[b][k] = (float)(sum[b][k] / alpha[b]); r.Bright[b][k] = bright[b][k]; }
				r.C[b][3] = r.Bright[b][3] = 1.0f; r.Radius[b] = radius[b]; r.Edge[b] = edge[b]; last = b;
			}
			else if (last >= 0) { memcpy(r.C[b], r.C[last], sizeof(r.C[b])); memcpy(r.Bright[b], r.Bright[last], sizeof(r.Bright[b])); r.Radius[b] = r.Radius[last] * 0.5f; r.Edge[b] = r.Edge[last] * 0.5f; }
		}
		for (int b = 2; b >= 0; b--) if (alpha[b] <= 0.0 && alpha[b + 1] > 0.0) { memcpy(r.C[b], r.C[b + 1], sizeof(r.C[b])); memcpy(r.Bright[b], r.Bright[b + 1], sizeof(r.Bright[b])); r.Radius[b] = r.Radius[b + 1]; r.Edge[b] = r.Edge[b + 1]; }
		r.Top = (std::min)((std::max)(top, 0.25f), 1.0f);
		r.Read = last >= 0;
	}

	inline bool ReadRampCPU(IDirect3DDevice9* device, IDirect3DTexture9* tex, DWORD level, const D3DSURFACE_DESC& d, const float rect[4], Ramp& r) {
		IDirect3DSurface9* src = nullptr; IDirect3DSurface9* dst = nullptr;
		HRESULT hr = tex->GetSurfaceLevel(level, &src);
		if (SUCCEEDED(hr)) hr = device->CreateOffscreenPlainSurface(d.Width, d.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &dst, NULL);
		if (SUCCEEDED(hr)) hr = D3DXLoadSurfaceFromSurface(dst, NULL, NULL, src, NULL, NULL, D3DX_FILTER_NONE, 0);
		D3DLOCKED_RECT lr = {};
		if (SUCCEEDED(hr)) hr = dst->LockRect(&lr, NULL, D3DLOCK_READONLY);
		bool ok = false;
		if (SUCCEEDED(hr)) {
			double sum[4][3] = {}, alpha[4] = {}, rad[4] = {}, cover = 0.0, all = 0.0, bandAll[4] = {}, bandCover[4] = {}, bsum[4][3] = {}, bw[4] = {};
			float top = 0.0f, edge[4] = {};
			const UINT x0 = (UINT)(rect[0] * d.Width), x1 = (std::max)(x0 + 1, (UINT)((rect[0] + rect[2]) * d.Width));
			const UINT y0 = (UINT)(rect[1] * d.Height), y1 = (std::max)(y0 + 1, (UINT)((rect[1] + rect[3]) * d.Height));
			for (UINT y = y0; y < y1 && y < d.Height; y++) {
				const unsigned char* row = (const unsigned char*)lr.pBits + y * lr.Pitch;
				const float q = (y - y0 + 0.5f) / (y1 - y0);
				const int band = 3 - (std::min)((int)(q * 4.0f), 3);
				for (UINT x = x0; x < x1 && x < d.Width; x++) {
					const double a = row[x * 4 + 3] / 255.0;
					all += 1.0; cover += a; bandAll[band] += 1.0; bandCover[band] += a;
					if (a > 0.15) { top = (std::max)(top, 1.0f - q); edge[band] = (std::max)(edge[band], (float)fabs((x - x0 + 0.5) / (x1 - x0) - 0.5) * 2.0f); }
					if (a <= 0.02) continue;
					const double c[3] = { row[x * 4 + 2] / 255.0, row[x * 4 + 1] / 255.0, row[x * 4 + 0] / 255.0 };
					for (int k = 0; k < 3; k++) sum[band][k] += c[k] * a;
					alpha[band] += a;
					rad[band] += a * fabs((x - x0 + 0.5) / (x1 - x0) - 0.5) * 2.0;
					const double l = (c[0] + c[1] + c[2]) / 3.0, w = a * l * l * l * l;
					for (int k = 0; k < 3; k++) bsum[band][k] += c[k] * w;
					bw[band] += w;
				}
			}
			dst->UnlockRect();
			float fill[4], radius[4], bright[4][3];
			for (int b = 0; b < 4; b++) {
				fill[b] = bandAll[b] > 0.0 ? (float)(bandCover[b] / bandAll[b]) : 0.0f; radius[b] = alpha[b] > 0.0 ? (float)(rad[b] / alpha[b]) : 0.0f;
				for (int k = 0; k < 3; k++) bright[b][k] = bw[b] > 0.0 ? (float)(bsum[b][k] / bw[b]) : 0.0f;
			}
			FinishRamp(r, sum, alpha, all > 0.0 ? (float)(cover / all) : 1.0f, fill, radius, top, bright, edge);
			ok = r.Read;
		}
		if (dst) dst->Release();
		if (src) src->Release();
		return ok;
	}

	inline bool MakeRampResources(IDirect3DDevice9* device) {
		if (RampTex) return true;
		if (RampGpuFailed) return false;
		RampGpuFailed = true;
		static const char* kVS = "struct O { float4 p : POSITION; float2 uv : TEXCOORD0; }; O main(float4 p : POSITION, float2 uv : TEXCOORD0) { O o; o.p = p; o.uv = uv; return o; }";
		static const char* kPS =
			"sampler2D S : register(s0); float4 P : register(c0); float4 R : register(c1);"
			"float4 main(float2 uv : TEXCOORD0) : COLOR0 {"
			"  float b = floor(uv.x * 12.0 + 0.25); float g = floor(b * 0.25); float band = b - 4.0 * g; float v0 = (3.0 - band) * 0.25;"
			"  float4 acc = 0; float3 bright = 0; float bw = 0; float rad = 0; float top = 0; float edge = 0;"
			"  for (int j = 0; j < 8; j++) for (int i = 0; i < 16; i++) {"
			"    float2 q = float2((i + 0.5) / 16.0, v0 + (j + 0.5) / 32.0);"
			"    float4 c = tex2Dlod(S, float4(R.xy + R.zw * q, 0, P.x));"
			"    float radial = abs(q.x - 0.5) * 2.0;"
			"    acc += float4(c.rgb * c.a, c.a); rad += c.a * radial; top = max(top, c.a > 0.15 ? 1.0 - q.y : 0.0); edge = max(edge, c.a > 0.15 ? radial : 0.0);"
			"    float l = dot(c.rgb, 0.3333); float w = c.a * l * l * l * l; bright += c.rgb * w; bw += w; }"
			"  if (g < 0.5) return float4(acc.rgb / max(acc.a, 1e-4), acc.a / 128.0);"
			"  if (g < 1.5) return float4(acc.a / 128.0, rad / max(acc.a, 1e-4), top, edge);"
			"  return float4(bright / max(bw, 1e-6), acc.a / 128.0); }";
		ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
		if (FAILED(D3DXCompileShader(kVS, (UINT)strlen(kVS), NULL, NULL, "main", "vs_3_0", 0, &code, &errors, NULL)) || !code ||
			FAILED(device->CreateVertexShader((const DWORD*)code->GetBufferPointer(), &RampVS))) {
			Logger::Log("UNOFFICIAL NewGrass: ramp vertex shader failed: %s", errors ? (const char*)errors->GetBufferPointer() : "?");
			if (errors) errors->Release(); if (code) code->Release();
			return false;
		}
		code->Release(); code = nullptr;
		if (FAILED(D3DXCompileShader(kPS, (UINT)strlen(kPS), NULL, NULL, "main", "ps_3_0", 0, &code, &errors, NULL)) || !code ||
			FAILED(device->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &RampPS))) {
			Logger::Log("UNOFFICIAL NewGrass: ramp pixel shader failed: %s", errors ? (const char*)errors->GetBufferPointer() : "?");
			if (errors) errors->Release(); if (code) code->Release();
			return false;
		}
		code->Release();
		D3DVERTEXELEMENT9 elements[] = { { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
		if (FAILED(device->CreateVertexDeclaration(elements, &RampDecl)) ||
			FAILED(device->CreateTexture(12, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &RampTex, NULL)) ||
			FAILED(RampTex->GetSurfaceLevel(0, &RampSurf)) ||
			FAILED(device->CreateOffscreenPlainSurface(12, 1, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &RampSys, NULL))) {
			Logger::Log("UNOFFICIAL NewGrass: ramp target failed: textures that cannot be locked get the default ramp");
			if (RampTex) { RampTex->Release(); RampTex = nullptr; }
			return false;
		}
		RampGpuFailed = false;
		return true;
	}

	inline bool ReadRampGPU(IDirect3DDevice9* device, IDirect3DTexture9* tex, DWORD level, const float rect[4], Ramp& r) {
		if (!MakeRampResources(device)) return false;
		IDirect3DSurface9* oldRT = nullptr; IDirect3DSurface9* oldDS = nullptr; D3DVIEWPORT9 oldVP = {};
		device->GetRenderTarget(0, &oldRT); device->GetDepthStencilSurface(&oldDS); device->GetViewport(&oldVP);
		IDirect3DVertexShader9* oldVS = nullptr; IDirect3DPixelShader9* oldPS = nullptr; IDirect3DVertexDeclaration9* oldDecl = nullptr;
		IDirect3DVertexBuffer9* oldVB = nullptr; UINT oldOffset = 0, oldStride = 0; IDirect3DBaseTexture9* oldTex = nullptr;
		device->GetVertexShader(&oldVS); device->GetPixelShader(&oldPS); device->GetVertexDeclaration(&oldDecl);
		device->GetStreamSource(0, &oldVB, &oldOffset, &oldStride); device->GetTexture(0, &oldTex);
		const D3DSAMPLERSTATETYPE ss[6] = { D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE };
		const DWORD ssv[6] = { D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_POINT, 0 };
		const D3DRENDERSTATETYPE rs[8] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE };
		const DWORD rsv[8] = { FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, 0xF, FALSE, FALSE };
		DWORD oldSS[6] = {}, oldRS[8] = {};
		for (int i = 0; i < 6; i++) device->GetSamplerState(0, ss[i], &oldSS[i]);
		for (int i = 0; i < 8; i++) device->GetRenderState(rs[i], &oldRS[i]);
		float oldC0[8] = {};
		device->GetPixelShaderConstantF(0, oldC0, 2);

		device->SetRenderTarget(0, RampSurf);
		device->SetDepthStencilSurface(NULL);
		D3DVIEWPORT9 vp = { 0, 0, 12, 1, 0.0f, 1.0f };
		device->SetViewport(&vp);
		device->SetVertexShader(RampVS); device->SetPixelShader(RampPS); device->SetVertexDeclaration(RampDecl);
		device->SetTexture(0, tex);
		for (int i = 0; i < 6; i++) device->SetSamplerState(0, ss[i], ssv[i]);
		for (int i = 0; i < 8; i++) device->SetRenderState(rs[i], rsv[i]);
		const float c0[8] = { (float)level, 0.0f, 0.0f, 0.0f, rect[0], rect[1], rect[2], rect[3] };
		device->SetPixelShaderConstantF(0, c0, 2);
		struct V { float x, y, z, w, u, v; };
		const V quad[4] = { { -1.0f, -1.0f, 0.5f, 1.0f, 0.0f, 1.0f }, { -1.0f, 1.0f, 0.5f, 1.0f, 0.0f, 0.0f }, { 1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 0.0f } };
		device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));

		device->SetRenderTarget(0, oldRT); device->SetDepthStencilSurface(oldDS); device->SetViewport(&oldVP);
		device->SetVertexShader(oldVS); device->SetPixelShader(oldPS); device->SetVertexDeclaration(oldDecl);
		device->SetStreamSource(0, oldVB, oldOffset, oldStride); device->SetTexture(0, oldTex);
		for (int i = 0; i < 6; i++) device->SetSamplerState(0, ss[i], oldSS[i]);
		for (int i = 0; i < 8; i++) device->SetRenderState(rs[i], oldRS[i]);
		device->SetPixelShaderConstantF(0, oldC0, 2);
		if (oldRT) oldRT->Release(); if (oldDS) oldDS->Release(); if (oldVS) oldVS->Release(); if (oldPS) oldPS->Release();
		if (oldDecl) oldDecl->Release(); if (oldVB) oldVB->Release(); if (oldTex) oldTex->Release();

		bool ok = false;
		D3DLOCKED_RECT lr = {};
		if (SUCCEEDED(device->GetRenderTargetData(RampSurf, RampSys)) && SUCCEEDED(RampSys->LockRect(&lr, NULL, D3DLOCK_READONLY))) {
			const unsigned char* px = (const unsigned char*)lr.pBits;
			double sum[4][3] = {}, alpha[4] = {}, cover = 0.0;
			float fill[4] = {}, radius[4] = {}, top = 0.0f, bright[4][3] = {}, edge[4] = {};
			for (int b = 0; b < 4; b++) {
				const double a = px[b * 4 + 3] / 255.0;
				cover += a / 4.0;
				const unsigned char* q = px + (4 + b) * 4;
				fill[b] = q[2] / 255.0f; radius[b] = q[1] / 255.0f; top = (std::max)(top, q[0] / 255.0f); edge[b] = q[3] / 255.0f;
				const unsigned char* w = px + (8 + b) * 4;
				bright[b][0] = w[2] / 255.0f; bright[b][1] = w[1] / 255.0f; bright[b][2] = w[0] / 255.0f;
				if (a <= 0.005) continue;
				sum[b][0] = px[b * 4 + 2] / 255.0 * a; sum[b][1] = px[b * 4 + 1] / 255.0 * a; sum[b][2] = px[b * 4 + 0] / 255.0 * a;
				alpha[b] = a;
			}
			RampSys->UnlockRect();
			FinishRamp(r, sum, alpha, (float)cover, fill, radius, top, bright, edge);
			ok = r.Read;
		}
		return ok;
	}

	inline const Ramp& GetRamp(IDirect3DDevice9* device, IDirect3DBaseTexture9* base, const void* modelKey, const Model& model) {
		const float* rect = model.Rects[0];
		Ramp& r = Ramps[modelKey];
		const ULONGLONG now = GetTickCount64();
		if (r.When && r.Texture == base && now - r.When < 300000) return r;
		const bool first = r.When == 0;
		r.When = now;
		r.Texture = base;
		r.Read = false;
		const float fallback[4][4] = { { 0.26f, 0.30f, 0.12f, 1.0f }, { 0.34f, 0.40f, 0.16f, 1.0f }, { 0.44f, 0.50f, 0.22f, 1.0f }, { 0.55f, 0.58f, 0.30f, 1.0f } };
		memcpy(r.C, fallback, sizeof(fallback));
		if (!base || base->GetType() != D3DRTYPE_TEXTURE) { strcpy_s(r.Info, sizeof(r.Info), "no 2D texture at stage 0"); r.Cutout = false; return r; }
		IDirect3DTexture9* tex = (IDirect3DTexture9*)base;
		D3DSURFACE_DESC d0 = {}, d = {};
		tex->GetLevelDesc(0, &d0);
		DWORD level = 0;
		const DWORD levels = tex->GetLevelCount();
		for (; level + 1 < levels; level++) { tex->GetLevelDesc(level, &d); if (d.Width <= 32 && d.Height <= 32) break; }
		tex->GetLevelDesc(level, &d);
		const char* how = "CPU copy";
		bool ok = false;
		Ramp tiles[4];
		unsigned nTiles = 0;
		for (unsigned k = 0; k < model.RectCount; k++) {
			Ramp& t = tiles[k];
			bool got = d0.Pool != D3DPOOL_DEFAULT && ReadRampCPU(device, tex, level, d, model.Rects[k], t);
			if (!got) { how = "GPU reduction"; got = ReadRampGPU(device, tex, level, model.Rects[k], t); }
			if (got) nTiles++; else t.Read = false;
		}
		if (nTiles) {
			ok = true;
			double cover = 0.0;
			r.Top = 0.0f;
			for (int b = 0; b < 4; b++) {
				double w = 0.0, c[3] = {}, br[3] = {}, rad = 0.0, ed = 0.0, fill = 0.0;
				for (unsigned k = 0; k < model.RectCount; k++) {
					const Ramp& t = tiles[k];
					if (!t.Read) continue;
					const double wk = (double)t.Fill[b] + 1e-4;
					w += wk; fill += t.Fill[b];
					for (int i = 0; i < 3; i++) { c[i] += t.C[b][i] * wk; br[i] += t.Bright[b][i] * wk; }
					rad += t.Radius[b] * wk; ed += t.Edge[b] * wk;
				}
				for (int i = 0; i < 3; i++) { r.C[b][i] = (float)(c[i] / w); r.Bright[b][i] = (float)(br[i] / w); }
				r.C[b][3] = r.Bright[b][3] = 1.0f;
				r.Radius[b] = (float)(rad / w); r.Edge[b] = (float)(ed / w); r.Fill[b] = (float)(fill / nTiles);
				cover += r.Fill[b] / 4.0;
			}
			for (unsigned k = 0; k < model.RectCount; k++) if (tiles[k].Read) r.Top = (std::max)(r.Top, tiles[k].Top);
			r.Top = (std::min)((std::max)(r.Top, 0.25f), 1.0f);
			r.Coverage = (float)cover;
			r.Read = true;
		}
		if (!ok) { how = "NOT READ (default ramp)"; r.Read = false; memcpy(r.C, fallback, sizeof(fallback)); memcpy(r.Bright, fallback, sizeof(fallback)); r.Coverage = 1.0f; for (int b = 0; b < 4; b++) r.Edge[b] = 0.8f; }
		r.Cutout = r.Read ? r.Coverage < 0.97f : (d0.Format == D3DFMT_DXT3 || d0.Format == D3DFMT_DXT5 || d0.Format == D3DFMT_A8R8G8B8 || d0.Format == D3DFMT_A8B8G8R8);
		const char* fmt = d0.Format == D3DFMT_DXT1 ? "DXT1" : d0.Format == D3DFMT_DXT3 ? "DXT3" : d0.Format == D3DFMT_DXT5 ? "DXT5" : d0.Format == D3DFMT_A8R8G8B8 ? "A8R8G8B8" : "other";
		_snprintf_s(r.Info, sizeof(r.Info), _TRUNCATE, "%ux%u %s pool %u, mip %u (%ux%u), %u tile(s) (first %.2f %.2f + %.2f %.2f), %s, alpha coverage %.0f%%: bright root %.2f %.2f %.2f, mid %.2f %.2f %.2f, tip %.2f %.2f %.2f (mean mid %.2f %.2f %.2f); shape: top %.2f, fill %.2f %.2f %.2f %.2f, mean radius %.2f %.2f %.2f %.2f, edge %.2f %.2f %.2f %.2f",
			d0.Width, d0.Height, fmt, (unsigned)d0.Pool, level, d.Width, d.Height, model.RectCount, rect[0], rect[1], rect[2], rect[3], how, r.Coverage * 100.0f,
			r.Bright[0][0], r.Bright[0][1], r.Bright[0][2], r.Bright[2][0], r.Bright[2][1], r.Bright[2][2], r.Bright[3][0], r.Bright[3][1], r.Bright[3][2], r.C[2][0], r.C[2][1], r.C[2][2],
			r.Top, r.Fill[0], r.Fill[1], r.Fill[2], r.Fill[3], r.Radius[0], r.Radius[1], r.Radius[2], r.Radius[3], r.Edge[0], r.Edge[1], r.Edge[2], r.Edge[3]);
		if (first) Logger::Log("UNOFFICIAL NewGrass: model %p texture %p: %s -> %s", modelKey, base, r.Info, r.Cutout ? "cut-out card" : "no cut-out (kept the game's)");
		return r;
	}

	inline unsigned StatCardsOnly = 0;
	inline void Draw(IDirect3DDevice9* device, const Model& m, const Ramp& ramp, unsigned tufts) {
		const float lodFar = (std::max)(S.LodFar, S.LodNear + 1.0f);
		if (S.KeepCards && ArmNearest >= lodFar) { StatCardsOnly++; return; }
		const int variant = ArmNearest < S.LodNear ? 0 : ArmNearest < 0.5f * (S.LodNear + lodFar) ? 1 : 2;
		const float lodT = (std::min)((std::max)((ArmNearest - S.LodNear) / (lodFar - S.LodNear), 0.0f), 1.0f);
		const float fillShare = S.FillRef > 0.0f ? (std::min)((std::max)(ramp.Coverage / S.FillRef, 0.5f), 1.0f) : 1.0f;
		const int blades = (std::min)((std::max)((int)(S.Blades * fillShare + 0.5f), 1), (int)kMaxBlades);
		int k = (int)ceilf(blades + ((S.KeepCards ? 0.0f : 1.0f) - blades) * lodT);
		k = (std::min)((std::max)(k, 1), blades);
		const Variant& v = Variants[variant];
		const unsigned prims = k * kTufts * v.TrisPerBlade, verts = k * kTufts * v.VertsPerBlade;

		IDirect3DVertexShader9* oldVS = nullptr; IDirect3DPixelShader9* oldPS = nullptr; IDirect3DVertexDeclaration9* oldDecl = nullptr;
		IDirect3DVertexBuffer9* oldVB = nullptr; UINT oldOffset = 0, oldStride = 0; IDirect3DIndexBuffer9* oldIB = nullptr;
		device->GetVertexShader(&oldVS); device->GetPixelShader(&oldPS); device->GetVertexDeclaration(&oldDecl);
		device->GetStreamSource(0, &oldVB, &oldOffset, &oldStride); device->GetIndices(&oldIB);
		NiDX9RenderState* state = TheRenderManager->renderState;
		const DWORD cull = state->GetRenderState(D3DRS_CULLMODE), alphaTest = state->GetRenderState(D3DRS_ALPHATESTENABLE), blend = state->GetRenderState(D3DRS_ALPHABLENDENABLE);
		state->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
		state->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
		state->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);

		ShaderRecordPixel* ps = S.ReceiveShadows ? PS : PSNoShadow;
		device->SetVertexShader(VS->ShaderHandle);
		device->SetPixelShader(ps->ShaderHandle);
		VS->SetCT();
		ps->SetCT();
		const float vsc[16] = {
			S.LodNear, 1.0f / (lodFar - S.LodNear), S.WidenDistance > 1.0f ? 1.0f / S.WidenDistance : 0.0f, S.Droop,
			m.Height * ramp.Top, m.Width, S.WidthRatio, S.Bend,
			LastVariant == '2' ? 1.0f : 0.0f, (float)blades, S.HeightScale, (float)tufts,
			1.0f / (float)(v.Levels - 1), (float)ArmPart, S.Flutter, (std::min)((std::max)(S.LengthMin, 0.1f), 1.0f) };
		device->SetVertexShaderConstantF(248, vsc, 4);
		const float* prof0 = S.KeepCards ? ramp.Edge : ramp.Radius;
		const float prof[8] = { (std::min)(prof0[0] * (S.KeepCards ? 1.0f : 1.1f), 1.0f), (std::min)(prof0[1] * (S.KeepCards ? 1.0f : 1.1f), 1.0f), (std::min)(prof0[2] * (S.KeepCards ? 1.0f : 1.1f), 1.0f), (std::min)(prof0[3] * (S.KeepCards ? 1.0f : 1.1f), 1.0f),
			S.Spread, S.KeepCards ? 1.0f : 0.0f, S.KeepCards ? S.TipReach : 1.0f, S.Kink };
		device->SetVertexShaderConstantF(254, prof, 2);
		float psc[24];
		memcpy(psc, S.KeepCards ? ramp.Bright : ramp.C, sizeof(ramp.C));
		psc[16] = S.ShadowLift; psc[17] = S.Wrap; psc[18] = S.TipLift; psc[19] = S.Variation;
		psc[20] = S.NormalBlend; psc[21] = S.SkyLight; psc[22] = S.GameLighting ? 1.0f : 0.0f; psc[23] = 0.0f;
		device->SetPixelShaderConstantF(20, psc, 6);
		device->SetVertexDeclaration(Decl);
		device->SetStreamSource(0, v.VB, 0, 4);
		device->SetIndices(v.IB);
		Next(device, D3DPT_TRIANGLELIST, 0, 0, verts, 0, prims);

		device->SetVertexShader(oldVS); device->SetPixelShader(oldPS); device->SetVertexDeclaration(oldDecl);
		device->SetStreamSource(0, oldVB, oldOffset, oldStride); device->SetIndices(oldIB);
		if (oldVS) oldVS->Release(); if (oldPS) oldPS->Release(); if (oldDecl) oldDecl->Release(); if (oldVB) oldVB->Release(); if (oldIB) oldIB->Release();
		state->SetRenderState(D3DRS_CULLMODE, cull, RenderStateArgs);
		state->SetRenderState(D3DRS_ALPHATESTENABLE, alphaTest, RenderStateArgs);
		state->SetRenderState(D3DRS_ALPHABLENDENABLE, blend, RenderStateArgs);
		StatReplaced++; StatTufts += tufts; StatVerts += verts; StatTris += prims;
		if (S.Log && Frames < 3) Logger::Log("UNOFFICIAL NewGrass: draw: %u tufts, part %d, nearest %.0f, variant %u levels, %d blades, %u tris, VS '%c', model h %.1f w %.1f",
			tufts, ArmPart, ArmNearest, v.Levels, k, prims, LastVariant ? LastVariant : '?', m.Height, m.Width);
	}

	inline HRESULT STDMETHODCALLTYPE DrawHook(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT numVertices, UINT start, UINT count) {
		if (!Armed) return Next(device, type, base, minIndex, numVertices, start, count);
		if (Replaced) { if (S.KeepCards) return Next(device, type, base, minIndex, numVertices, start, count); StatSwallowed++; return D3D_OK; }
		NiGeometry* geometry = ArmPass ? *(NiGeometry**)ArmPass : nullptr;
		bool ok = geometry && geometry->geomData;
		if (ok && !(LastVariant == '0' || LastVariant == '2')) { ok = false; StatNormals++; }
		const Model* model = ok ? &GetModel(geometry) : nullptr;
		if (ok && !model->Card) { ok = false; StatNotCard++; }
		const Ramp* ramp = nullptr;
		if (ok) {
			IDirect3DBaseTexture9* texture = TheRenderManager->renderState->GetTexture(0);
			ramp = &GetRamp(device, texture, geometry->geomData, *model);
			if (!ramp->Cutout) { ok = false; StatNotCard++; }
		}
		if (!ok) { StatLetThrough++; Armed = false; return Next(device, type, base, minIndex, numVertices, start, count); }
		unsigned tufts = 0;
		if (TallGrassShader* shader = TallGrassShader::GetShader()) tufts = shader->ucInstanceCount;
		const unsigned fromVertices = model->VertsPerModel ? numVertices / model->VertsPerModel : 0;
		if (tufts == 0 || tufts > kTufts) tufts = (std::min)(fromVertices, kTufts);
		else if (fromVertices && fromVertices != tufts && Frames < 3) Logger::Log("UNOFFICIAL NewGrass: instance count %u (shader) vs %u (vertices %u / %u per tuft): the shader's is used", tufts, fromVertices, numVertices, model->VertsPerModel);
		if (tufts == 0) { StatLetThrough++; Armed = false; return Next(device, type, base, minIndex, numVertices, start, count); }
		Replaced = true;
		Draw(device, *model, *ramp, tufts);
		if (S.KeepCards) return Next(device, type, base, minIndex, numVertices, start, count);
		return D3D_OK;
	}

	inline void BeginEarly(IDirect3DDevice9* device, bool grassEnabled) {
		Active = false;
		Armed = Replaced = false;
		if (!S.Enabled || !grassEnabled || !device) return;
		if (!LoadShaders() || !CreateResources(device)) return;
		void** table = *(void***)device;
		if (table[82] != (void*)&DrawHook) {
			DWORD old = 0;
			if (!VirtualProtect(&table[82], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return;
			Next = (DrawIndexedPrimitiveFn)table[82];
			table[82] = (void*)&DrawHook;
			VirtualProtect(&table[82], sizeof(void*), old, &old);
			Hooked = true;
		}
		Active = table[82] == (void*)&DrawHook;
	}

	inline void EndEarly(IDirect3DDevice9* device, unsigned earlyDraws) {
		Armed = Replaced = false;
		if (Hooked && device) {
			void** table = *(void***)device;
			if (table[82] == (void*)&DrawHook) {
				DWORD old = 0;
				if (VirtualProtect(&table[82], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
					table[82] = (void*)Next;
					VirtualProtect(&table[82], sizeof(void*), old, &old);
				}
			}
			Hooked = false;
		}
		if (Active) {
			Frames++;
			if (Frames <= 3 || Frames % 3600 == 0)
				Logger::Log("UNOFFICIAL NewGrass: frame %u: %u of %u early grass draws %s blades (%u tufts, %u vertices, %u triangles), %u cards alone beyond LodFar, %u left to the game (%u beyond BladeDistance, %u not a card, %u lit by real normals), %u extra draws swallowed",
					Frames, StatReplaced, earlyDraws, S.KeepCards ? "got strands around their cards (hybrid), the cards drawn as well; the strands:" : "replaced by", StatTufts, StatVerts, StatTris, StatCardsOnly, StatLetThrough + StatFar, StatFar, StatNotCard, StatNormals, StatSwallowed);
		}
		Active = false;
		StatReplaced = StatLetThrough = StatSwallowed = StatTufts = StatVerts = StatTris = StatFar = StatNotCard = StatNormals = StatCardsOnly = 0;
	}

	inline void Arm(void* pass, int part, float nearest) {
		Armed = Replaced = false;
		if (!Active) return;
		if (nearest > S.BladeDistance) { StatFar++; return; }
		ArmPass = pass; ArmPart = part; ArmNearest = (std::max)(nearest, 0.0f);
		Armed = true;
	}

	inline void Disarm() { Armed = Replaced = false; ArmPass = nullptr; }

	inline void ReadSettings() {
		const char* s = "Shaders.Grass.NewGrass";
		S.Enabled = TheSettingManager->GetSettingI(s, "Enabled") != 0;
		S.KeepCards = TheSettingManager->GetSettingI(s, "KeepCards") != 0;
		S.TipReach = TheSettingManager->GetSettingF(s, "TipReach");
		S.Kink = TheSettingManager->GetSettingF(s, "Kink");
		S.BladeDistance = TheSettingManager->GetSettingF(s, "BladeDistance");
		S.Blades = TheSettingManager->GetSettingI(s, "Blades");
		S.LodNear = TheSettingManager->GetSettingF(s, "LodNear");
		S.LodFar = TheSettingManager->GetSettingF(s, "LodFar");
		S.WidenDistance = TheSettingManager->GetSettingF(s, "WidenDistance");
		S.HeightScale = TheSettingManager->GetSettingF(s, "HeightScale");
		S.WidthRatio = TheSettingManager->GetSettingF(s, "WidthRatio");
		S.Spread = TheSettingManager->GetSettingF(s, "Spread");
		S.Bend = TheSettingManager->GetSettingF(s, "Bend");
		S.Flutter = TheSettingManager->GetSettingF(s, "Flutter");
		S.LengthMin = TheSettingManager->GetSettingF(s, "LengthMin");
		S.Droop = TheSettingManager->GetSettingF(s, "Droop");
		S.FillRef = TheSettingManager->GetSettingF(s, "FillRef");
		S.ReceiveShadows = TheSettingManager->GetSettingI(s, "ReceiveShadows") != 0;
		S.ShadowLift = TheSettingManager->GetSettingF(s, "ShadowLift");
		S.NormalBlend = TheSettingManager->GetSettingF(s, "NormalBlend");
		S.SkyLight = TheSettingManager->GetSettingF(s, "SkyLight");
		S.GameLighting = TheSettingManager->GetSettingI(s, "GameLighting") != 0;
		S.Wrap = TheSettingManager->GetSettingF(s, "Wrap");
		S.TipLift = TheSettingManager->GetSettingF(s, "TipLift");
		S.Variation = TheSettingManager->GetSettingF(s, "Variation");
		S.Log = TheSettingManager->GetSettingI(s, "Log");
	}
}
