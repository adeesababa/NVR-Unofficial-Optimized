#pragma once

// UNOFFICIAL 2026-10-18 NewGrass ([Shaders.Grass.NewGrass], OFF by default: a look change the user judges). Design and limits:
// bench\deep-roadmap\evidence\newgrass-DESIGN.md, log newgrass-LOG.md.
//
// The game's grass is alpha-tested cards (up to 228 tufts per draw, shader-constant instanced from c20..c247, one grass type x one
// terrain chunk per draw; evidence\wave-1018b\f05-grass.md). Here the card draw of every piece GrassFirst draws before the land
// (Render.cpp namespace GrassFirst, part 1 = the tufts before the fade start) is replaced by one draw of real blade geometry built
// in NewGrass.vso.hlsl from the SAME instance table the game just uploaded: positions, density, grass types per region, per-tuft
// size and brightness are the game's; the blade colour comes from the grass type's own card texture (a 4-step ramp read once per
// texture). Opaque blades: no alpha test (early-Z works, the alpha kill was the cost of the cards), the game's MSAA smooths the
// edges, blades per tuft and triangles per blade fall with distance (LOD), the sun's cascade shadows are received. The fading
// tufts after the land (part 2) stay the game's cards, as does any grass piece the rule below leaves alone.
//
// How a draw is replaced: GrassFirst's early call installs the device's DrawIndexedPrimitive (vtable slot 82) for its span, as
// SandstormGlass does (the native runtime rewrites the table, so nothing stays in it); RenderPassImmediatelyHook arms one pass at
// a time (its geometry, part, nearest distance); the FIRST DrawIndexedPrimitive of an armed pass is ours instead of the game's,
// the rest of that pass (strip arrays) are swallowed; a pass we leave alone passes through untouched. Replaced only when the pass's
// vertex shader is a card variant (GRASS23x000 LF / 002 LFS; 001 / 003 light by real vertex normals = scatter rocks and debris
// placed as grass), the model is a card (<= 16 vertices per tuft) and its texture has a cut-out (alpha coverage below 97%), and
// the piece's nearest tuft is within BladeDistance. Everything we bind is put back right after the draw (shaders, declaration,
// stream, indices, three render states through the game's state cache); GrassFirst::Restore puts constants and samplers back.
//
// Ideas credited: Ghost of Tsushima's procedural grass (Eric Wohllaib, GDC 2021), Jahrmann & Wimmer 2017 'Responsive Real-Time
// Grass Rendering' (width grows with distance), Community Shaders' Grass Optimizations size fade through Balebandro's PBR fork
// (90ae7541, GPL-3; the idea, this code is ours).
#include <d3dx9.h>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace NewGrass {

	struct SettingsStruct {
		bool	Enabled = false;
		float	BladeDistance = 7000.0f;	// pieces whose nearest tuft is farther stay the game's cards
		int		Blades = 8;					// blades per tuft near the player (1-8)
		float	LodNear = 1200.0f, LodFar = 4500.0f, WidenDistance = 2500.0f;
		float	HeightScale = 1.0f, WidthRatio = 0.09f, Spread = 0.8f, Bend = 0.35f, Flutter = 0.15f;
		bool	ReceiveShadows = true;
		float	Wrap = 0.3f, TipLift = 0.5f, Variation = 0.12f;
		int		Log = 0;					// 1 = a line per draw for 3 frames (diagnostic)
	};
	inline SettingsStruct S;

	inline const unsigned kTufts = 228, kMaxBlades = 8;
	inline bool Active = false;					// this early call replaces draws (settings + resources OK)
	inline bool Hooked = false;
	typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	inline DrawIndexedPrimitiveFn Next = nullptr;

	// Resources (device objects live for the session; the buffers are MANAGED so a device reset keeps them).
	inline ShaderRecordVertex* VS = nullptr;
	inline ShaderRecordPixel* PS = nullptr;
	inline ShaderRecordPixel* PSNoShadow = nullptr;
	inline bool LoadTried = false, LoadFailed = false;
	inline IDirect3DVertexDeclaration9* Decl = nullptr;
	struct Variant { IDirect3DVertexBuffer9* VB = nullptr; IDirect3DIndexBuffer9* IB = nullptr; unsigned Levels = 0, VertsPerBlade = 0, TrisPerBlade = 0, Verts = 0; };
	inline Variant Variants[3];					// 4, 3, 2 levels along the blade (7 / 5 / 3 vertices, 5 / 3 / 1 triangles)

	// What a grass model is (per NiGeometryData, learned once).
	struct Model { unsigned VertsPerModel = 0, Capacity = 0, Total = 0; float Height = 0.0f, Width = 0.0f; bool Card = false, FromBound = false; };
	inline std::unordered_map<const void*, Model> Models;
	// The colour ramp of a grass type (per texture, read from a small mip; refreshed every 60 s in case the address is reused).
	struct Ramp { float C[4][4] = {}; bool Read = false, Cutout = false; float Coverage = 1.0f; ULONGLONG When = 0; char Info[128] = ""; };
	inline std::unordered_map<const void*, Ramp> Ramps;

	// The armed pass.
	inline bool Armed = false, Replaced = false;
	inline void* ArmPass = nullptr;
	inline float ArmNearest = 0.0f;
	inline int ArmPart = 0;						// 0 every tuft of the draw, 1 only the tufts before the fade start
	inline char LastVariant = 0;				// the digit of the grass VS bound last ('0' LF, '1' LV, '2' LFS, '3' LVS; GrassShaders::SetForDraw)

	// Statistics (per early call = per frame).
	inline unsigned StatReplaced = 0, StatLetThrough = 0, StatSwallowed = 0, StatTufts = 0, StatVerts = 0, StatTris = 0, StatFar = 0, StatNotCard = 0, StatNormals = 0, Frames = 0;
	inline bool MatricesChecked = false;

	inline bool CreateResources(IDirect3DDevice9* device) {
		if (Decl) return true;
		D3DVERTEXELEMENT9 elements[] = { { 0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
		if (FAILED(device->CreateVertexDeclaration(elements, &Decl))) { Decl = nullptr; Logger::Log("UNOFFICIAL NewGrass: no vertex declaration (UBYTE4): off"); return false; }
		const unsigned levels[3] = { 4, 3, 2 };
		for (int v = 0; v < 3; v++) {
			Variant& var = Variants[v];
			var.Levels = levels[v];
			var.VertsPerBlade = 2 * (var.Levels - 1) + 1;	// two vertices per level, one at the tip
			var.TrisPerBlade = 2 * (var.Levels - 2) + 1;
			var.Verts = kTufts * kMaxBlades * var.VertsPerBlade;
			std::vector<unsigned char> vb(var.Verts * 4);
			std::vector<unsigned short> ib(kTufts * kMaxBlades * var.TrisPerBlade * 3);
			unsigned ii = 0;
			for (unsigned b = 0; b < kMaxBlades; b++) for (unsigned t = 0; t < kTufts; t++) {	// blade-major: the first k blades of every tuft = one primitive count
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
		LoadFailed = !VS || !PS || !PSNoShadow;
		Logger::Log("UNOFFICIAL NewGrass: shaders %s (VS %p, PS %p, PS without shadows %p)", LoadFailed ? "FAILED to load: off" : "loaded", VS, PS, PSNoShadow);
		return !LoadFailed;
	}

	// The grass model behind a pass's geometry: vertices per tuft (the TEXCOORD1 instance index run of 0), the card's height and width.
	inline const Model& GetModel(NiGeometry* geometry) {
		NiGeometryData* gd = geometry->geomData;
		Model& m = Models[gd];
		if (m.Total) return m;
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
		m.Card = vpm <= 16;
		Logger::Log("UNOFFICIAL NewGrass: model %p: %u vertices, %u per tuft (%u tufts), height %.1f width %.1f%s -> %s", gd, m.Total, vpm, m.Capacity,
			m.Height, m.Width, m.FromBound ? " (from the bound)" : "", m.Card ? "card" : "NOT a card (kept the game's)");
		return m;
	}

	// The grass type's colours: 4 bands (root -> tip) of alpha-weighted mean colour from a mip of its card texture (<= 32 wide).
	inline const Ramp& GetRamp(IDirect3DDevice9* device, IDirect3DBaseTexture9* base) {
		Ramp& r = Ramps[base];
		const ULONGLONG now = GetTickCount64();
		if (r.When && now - r.When < 60000) return r;
		const bool first = r.When == 0;
		r.When = now;
		r.Read = false;
		const float fallback[4][4] = { { 0.26f, 0.30f, 0.12f, 1.0f }, { 0.34f, 0.40f, 0.16f, 1.0f }, { 0.44f, 0.50f, 0.22f, 1.0f }, { 0.55f, 0.58f, 0.30f, 1.0f } };
		memcpy(r.C, fallback, sizeof(fallback));
		if (!base || base->GetType() != D3DRTYPE_TEXTURE) { strcpy_s(r.Info, "no 2D texture at stage 0"); r.Cutout = false; return r; }
		IDirect3DTexture9* tex = (IDirect3DTexture9*)base;
		D3DSURFACE_DESC d0 = {}, d = {};
		tex->GetLevelDesc(0, &d0);
		DWORD level = 0;
		const DWORD levels = tex->GetLevelCount();
		for (; level + 1 < levels; level++) { tex->GetLevelDesc(level, &d); if (d.Width <= 32 && d.Height <= 32) break; }
		tex->GetLevelDesc(level, &d);
		IDirect3DSurface9* src = nullptr; IDirect3DSurface9* dst = nullptr;
		HRESULT hr = tex->GetSurfaceLevel(level, &src);
		if (SUCCEEDED(hr)) hr = device->CreateOffscreenPlainSurface(d.Width, d.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &dst, NULL);
		if (SUCCEEDED(hr)) hr = D3DXLoadSurfaceFromSurface(dst, NULL, NULL, src, NULL, NULL, D3DX_FILTER_NONE, 0);
		D3DLOCKED_RECT lr = {};
		if (SUCCEEDED(hr)) hr = dst->LockRect(&lr, NULL, D3DLOCK_READONLY);
		if (SUCCEEDED(hr)) {
			double sum[4][4] = {}, alpha[4] = {}, cover = 0.0, all = 0.0;
			for (UINT y = 0; y < d.Height; y++) {
				const unsigned char* row = (const unsigned char*)lr.pBits + y * lr.Pitch;
				const int band = 3 - (int)(y * 4 / d.Height);	// the top rows are the tip
				for (UINT x = 0; x < d.Width; x++) {
					const double a = row[x * 4 + 3] / 255.0;
					all += 1.0; cover += a;
					if (a <= 0.02) continue;
					sum[band][0] += row[x * 4 + 2] * a; sum[band][1] += row[x * 4 + 1] * a; sum[band][2] += row[x * 4 + 0] * a;
					alpha[band] += a;
				}
			}
			dst->UnlockRect();
			r.Coverage = all > 0.0 ? (float)(cover / all) : 1.0f;
			// A band without opaque texels takes its neighbour's colour (the tip band of a short tuft, for instance).
			int last = -1;
			for (int b = 0; b < 4; b++) if (alpha[b] > 0.0) { for (int k = 0; k < 3; k++) r.C[b][k] = (float)(sum[b][k] / alpha[b] / 255.0); r.C[b][3] = 1.0f; last = b; }
			else if (last >= 0) memcpy(r.C[b], r.C[last], sizeof(r.C[b]));
			for (int b = 2; b >= 0; b--) if (alpha[b] <= 0.0 && alpha[b + 1] > 0.0) memcpy(r.C[b], r.C[b + 1], sizeof(r.C[b]));
			r.Read = last >= 0;
		}
		if (dst) dst->Release();
		if (src) src->Release();
		r.Cutout = r.Read ? r.Coverage < 0.97f : (d0.Format == D3DFMT_DXT3 || d0.Format == D3DFMT_DXT5 || d0.Format == D3DFMT_A8R8G8B8 || d0.Format == D3DFMT_A8B8G8R8);
		const char* fmt = d0.Format == D3DFMT_DXT1 ? "DXT1" : d0.Format == D3DFMT_DXT3 ? "DXT3" : d0.Format == D3DFMT_DXT5 ? "DXT5" : d0.Format == D3DFMT_A8R8G8B8 ? "A8R8G8B8" : "other";
		sprintf_s(r.Info, "%ux%u %s pool %u, mip %u (%ux%u) %s, alpha coverage %.0f%%: root %.2f %.2f %.2f, tip %.2f %.2f %.2f", d0.Width, d0.Height, fmt, (unsigned)d0.Pool,
			level, d.Width, d.Height, r.Read ? "read" : "COPY FAILED (default ramp)", r.Coverage * 100.0f, r.C[0][0], r.C[0][1], r.C[0][2], r.C[3][0], r.C[3][1], r.C[3][2]);
		if (first) Logger::Log("UNOFFICIAL NewGrass: texture %p: %s -> %s", base, r.Info, r.Cutout ? "cut-out card" : "no cut-out (kept the game's)");
		return r;
	}

	// Our draw in place of the game's: the same instance constants, our blade buffers and shaders; everything put back after.
	inline void Draw(IDirect3DDevice9* device, const Model& m, const Ramp& ramp, unsigned tufts) {
		const float lodFar = (std::max)(S.LodFar, S.LodNear + 1.0f);
		const int variant = ArmNearest < S.LodNear ? 0 : ArmNearest < 0.5f * (S.LodNear + lodFar) ? 1 : 2;
		const float lodT = (std::min)((std::max)((ArmNearest - S.LodNear) / (lodFar - S.LodNear), 0.0f), 1.0f);
		const int blades = (std::min)((std::max)(S.Blades, 1), (int)kMaxBlades);
		int k = (int)ceilf(blades + (1.0f - blades) * lodT);
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
			S.LodNear, 1.0f / (lodFar - S.LodNear), S.WidenDistance > 1.0f ? 1.0f / S.WidenDistance : 0.0f, 0.0f,
			m.Height, m.Width * S.Spread, S.WidthRatio, S.Bend,
			LastVariant == '2' ? 1.0f : 0.0f, (float)blades, S.HeightScale, (float)tufts,
			1.0f / (float)(v.Levels - 1), (float)ArmPart, S.Flutter, 0.0f };
		device->SetVertexShaderConstantF(248, vsc, 4);
		float psc[20];
		memcpy(psc, ramp.C, sizeof(ramp.C));
		psc[16] = 0.0f; psc[17] = S.Wrap; psc[18] = S.TipLift; psc[19] = S.Variation;
		device->SetPixelShaderConstantF(20, psc, 5);
		device->SetVertexDeclaration(Decl);
		device->SetStreamSource(0, v.VB, 0, 4);
		device->SetIndices(v.IB);
		Next(device, D3DPT_TRIANGLELIST, 0, 0, verts, 0, prims);	// straight to the runtime: the hooked entry would swallow it

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

	// Once: is the grass's ModelViewProj the camera's ViewProj (instance positions in world space)? Logged for the record.
	inline void CheckMatrices(IDirect3DDevice9* device) {
		if (MatricesChecked) return;
		MatricesChecked = true;
		float mvp[16] = {};
		if (FAILED(device->GetVertexShaderConstantF(9, mvp, 4))) return;
		const float* vp = (const float*)&TheRenderManager->ViewProjMatrix;
		float worst = 0.0f, worstT = 0.0f;
		for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) {
			const float ref = fabsf(vp[r * 4 + c]) + 1e-3f;
			worst = (std::max)(worst, fabsf(mvp[r * 4 + c] - vp[r * 4 + c]) / ref);
			worstT = (std::max)(worstT, fabsf(mvp[r * 4 + c] - vp[c * 4 + r]) / ref);
		}
		Logger::Log("UNOFFICIAL NewGrass: grass ModelViewProj vs the camera's ViewProj: largest relative difference %.4f as stored, %.4f transposed (0 = the instance positions are world positions)", worst, worstT);
	}

	inline HRESULT STDMETHODCALLTYPE DrawHook(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT numVertices, UINT start, UINT count) {
		if (!Armed) return Next(device, type, base, minIndex, numVertices, start, count);
		if (Replaced) { StatSwallowed++; return D3D_OK; }
		NiGeometry* geometry = ArmPass ? *(NiGeometry**)ArmPass : nullptr;
		bool ok = geometry && geometry->geomData;
		if (ok && !(LastVariant == '0' || LastVariant == '2')) { ok = false; StatNormals++; }
		const Model* model = ok ? &GetModel(geometry) : nullptr;
		if (ok && !model->Card) { ok = false; StatNotCard++; }
		const Ramp* ramp = nullptr;
		if (ok) {
			IDirect3DBaseTexture9* texture = TheRenderManager->renderState->GetTexture(0);
			ramp = &GetRamp(device, texture);
			if (!ramp->Cutout) { ok = false; StatNotCard++; }
		}
		if (!ok) { StatLetThrough++; Armed = false; return Next(device, type, base, minIndex, numVertices, start, count); }
		CheckMatrices(device);
		unsigned tufts = 0;
		if (TallGrassShader* shader = TallGrassShader::GetShader()) tufts = shader->ucInstanceCount;
		const unsigned fromVertices = model->VertsPerModel ? numVertices / model->VertsPerModel : 0;
		if (tufts == 0 || tufts > kTufts) tufts = (std::min)(fromVertices, kTufts);
		else if (fromVertices && fromVertices != tufts && Frames < 3) Logger::Log("UNOFFICIAL NewGrass: instance count %u (shader) vs %u (vertices %u / %u per tuft): the shader's is used", tufts, fromVertices, numVertices, model->VertsPerModel);
		if (tufts == 0) { StatLetThrough++; Armed = false; return Next(device, type, base, minIndex, numVertices, start, count); }
		Replaced = true;
		Draw(device, *model, *ramp, tufts);
		return D3D_OK;
	}

	// GrassFirst's early grass call: the device's DrawIndexedPrimitive is ours for its span.
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
				Logger::Log("UNOFFICIAL NewGrass: frame %u: %u of %u early grass draws replaced by blades (%u tufts, %u vertices, %u triangles), %u left to the game (%u beyond BladeDistance, %u not a card, %u lit by real normals), %u extra draws swallowed",
					Frames, StatReplaced, earlyDraws, StatTufts, StatVerts, StatTris, StatLetThrough + StatFar, StatFar, StatNotCard, StatNormals, StatSwallowed);
		}
		Active = false;
		StatReplaced = StatLetThrough = StatSwallowed = StatTufts = StatVerts = StatTris = StatFar = StatNotCard = StatNormals = 0;
	}

	// One grass pass about to be drawn early (part 0 = every tuft, 1 = only the near ones): ours if its nearest tuft is within BladeDistance.
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
		S.ReceiveShadows = TheSettingManager->GetSettingI(s, "ReceiveShadows") != 0;
		S.Wrap = TheSettingManager->GetSettingF(s, "Wrap");
		S.TipLift = TheSettingManager->GetSettingF(s, "TipLift");
		S.Variation = TheSettingManager->GetSettingF(s, "Variation");
		S.Log = TheSettingManager->GetSettingI(s, "Log");
	}
}
