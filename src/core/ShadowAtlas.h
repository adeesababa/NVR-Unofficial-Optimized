#pragma once

// UNOFFICIAL 2026-10-30 ShadowAtlas ([Shaders.ShadowsInteriors.Atlas] Enabled, restart, off; docs/next/INTERIOR-SHADOWS-MODERN.md step 1).
// Indoors every lamp shadow NVR publishes -- the live slots (people included) and the ShadowPool's still maps -- is also an OCTAHEDRAL
// tile of one depth atlas (R16F, Size x Size), and the object shaders read the draw's lamps from that one texture on s8
// (PointShadowForward.hlsl POINT_SHADOW_ATLAS) instead of six cube maps. Step 1 changes where the shadows are read from, not what they
// hold: each tile is its lamp's cube map resampled (distance / radius, the cube lookup's own direction convention), so the A/B against
// the cube path is the octahedral resampling; with PoolLamps raised every lamp past the live slots gets its still shadow without a
// sampler per lamp. (Steps 2-3, capsule people and contact shadows, come after.)
//   - layout in units of Size / 16: the live slots (up to 32, LightPoints) take 2 x 2 units each (1024 texels at 8192), in rows from the
//     top; the pool's lamps one unit each (512) in the rows below, as many as fit (pool index order);
//   - live tiles are converted every frame after the cube maps are drawn; a pool tile only when its cube map was drawn again (or another
//     lamp took that pool entry);
//   - PointShadowForward::SetForPass writes each matched light's tile (u, v of its first texel centre, its span in atlas uv) into the
//     light's tint register, and binds the atlas on s8.
// Off: nothing is made, the shaders are as before (no define).
#include "GpuProfiler.h"
#include "ShadowPool.h"
#include "PointShadowForward.h"

namespace ShadowAtlas {

	inline int Wanted = -1;
	inline IDirect3DTexture9* Texture = nullptr;
	inline IDirect3DSurface9* Surface = nullptr;
	inline IDirect3DPixelShader9* PS = nullptr;
	inline UINT Size = 0;
	inline bool Failed = false;
	struct PoolTile { const void* light = nullptr; const void* texture = nullptr; UInt32 drawn = 0; float x = 0, y = 0, z = 0, radius = 0; };
	inline PoolTile PoolTiles[ShadowPoolMax];
	inline unsigned StatFrames = 0;
	inline unsigned long long StatLive = 0, StatPool = 0, StatPoolConverted = 0, StatNoTile = 0;

	inline bool CompiledIn() {
		if (Wanted < 0) Wanted = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Atlas", "Enabled") != 0;
		return Wanted != 0;
	}

	inline bool Ensure(IDirect3DDevice9* d) {
		if (Texture) return true;
		if (Failed) return false;
		int size = TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Atlas", "Size");
		size = size >= 8192 ? 8192 : size >= 4096 ? 4096 : 2048;
		if (FAILED(d->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F, D3DPOOL_DEFAULT, &Texture, NULL)) || FAILED(Texture->GetSurfaceLevel(0, &Surface))) {
			Logger::Log("[ERROR] ShadowAtlas: could not make its %dx%d R16F atlas: off for the session (the cube maps as before)", size, size);
			if (Texture) { Texture->Release(); Texture = nullptr; }
			Failed = true;
			return false;
		}
		Size = (UINT)size;
		d->ColorFill(Surface, NULL, D3DCOLOR_ARGB(255, 255, 255, 255));	// 1 = nothing recorded (lit)
		static const char* kPS =
			"samplerCUBE C : register(s0); float4 K : register(c0);"	// K.x tile texels
			"float4 main(float2 uv : TEXCOORD0) : COLOR0 {"
			"  float2 v = (uv * K.x - 0.5f) / (K.x - 1.0f) * 2.0f - 1.0f;"
			"  float3 n = float3(v.x, v.y, 1.0f - abs(v.x) - abs(v.y));"
			"  float t = saturate(-n.z);"
			"  n.xy += float2(n.x >= 0.0f ? -t : t, n.y >= 0.0f ? -t : t);"
			"  return texCUBElod(C, float4(normalize(n), 0.0f)).r; }";
		ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
		const bool ok = SUCCEEDED(D3DXCompileShader(kPS, (UINT)strlen(kPS), NULL, NULL, "main", "ps_3_0", 0, &code, &errors, NULL)) && code &&
			SUCCEEDED(d->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &PS));
		if (!ok) Logger::Log("[ERROR] ShadowAtlas: the conversion shader failed (%s): off for the session", errors ? (const char*)errors->GetBufferPointer() : "?");
		if (code) code->Release();
		if (errors) errors->Release();
		if (!ok) { Failed = true; return false; }
		Logger::Log("ShadowAtlas: %ux%u R16F atlas made (%.0f MB)", Size, Size, Size * (double)Size * 2.0 / 1048576.0);
		PointShadowForward::AtlasTexture = Texture;
		return true;
	}

	// A live slot's tile (2 x 2 units) or a pool entry's (1 unit), in atlas texels; false when it has none.
	inline bool LiveRect(int slot, int liveSlots, UINT& x, UINT& y, UINT& n) {
		const UINT unit = Size / 16;
		if (slot < 0 || slot >= 32 || slot >= liveSlots) return false;
		x = 2 * unit * (slot % 8); y = 2 * unit * (slot / 8); n = 2 * unit;
		return true;
	}
	inline bool PoolRect(int index, int liveSlots, UINT& x, UINT& y, UINT& n) {
		const UINT unit = Size / 16;
		const int firstRow = 2 * (((std::min)(liveSlots, 32) + 7) / 8);
		const int at = firstRow * 16 + index;
		if (index < 0 || at >= 256) return false;
		x = unit * (at % 16); y = unit * (at / 16); n = unit;
		return true;
	}

	// ShadowManager, indoors after Publish + PublishPool: every published lamp's tile, converted when needed; its uv into the slot.
	// liveSlots: the live slot count (the slots below it are live, ShadowCubeMapsMax and up the pool's).
	inline void Update(IDirect3DDevice9* d, int liveSlots) {
		if (!CompiledIn() || !Ensure(d)) return;
		static GpuTimer convertTimer("Shadow atlas convert");
		GpuProfileScope gpu(convertTimer, d);
		NiDX9RenderState* state = TheRenderManager->renderState;
		IDirect3DVertexShader9* vs = nullptr; IDirect3DPixelShader9* ps = nullptr; IDirect3DVertexDeclaration9* decl = nullptr; DWORD fvf = 0;
		IDirect3DSurface9* rt0 = nullptr; IDirect3DSurface9* zs = nullptr; IDirect3DBaseTexture9* tex0 = nullptr; D3DVIEWPORT9 vp = {};
		d->GetVertexShader(&vs); d->GetPixelShader(&ps); d->GetVertexDeclaration(&decl); d->GetFVF(&fvf);
		d->GetRenderTarget(0, &rt0); d->GetDepthStencilSurface(&zs); d->GetTexture(0, &tex0); d->GetViewport(&vp);
		static const D3DRENDERSTATETYPE kRS[] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE,
			D3DRS_SCISSORTESTENABLE, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_FILLMODE, D3DRS_SRGBWRITEENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE };
		const DWORD want[] = { FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, 0xFu, D3DFILL_SOLID, FALSE, FALSE, 0 };
		DWORD savedRS[sizeof(kRS) / sizeof(kRS[0])];
		for (unsigned i = 0; i < sizeof(kRS) / sizeof(kRS[0]); i++) { if (FAILED(d->GetRenderState(kRS[i], &savedRS[i]))) savedRS[i] = want[i]; if (savedRS[i] != want[i]) d->SetRenderState(kRS[i], want[i]); }
		static const D3DSAMPLERSTATETYPE kSS[] = { D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER, D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_SRGBTEXTURE };
		const DWORD wantS[] = { D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, FALSE };
		DWORD savedS[sizeof(kSS) / sizeof(kSS[0])];
		for (unsigned i = 0; i < sizeof(kSS) / sizeof(kSS[0]); i++) { if (FAILED(d->GetSamplerState(0, kSS[i], &savedS[i]))) savedS[i] = wantS[i]; if (savedS[i] != wantS[i]) d->SetSamplerState(0, kSS[i], wantS[i]); }
		float savedPS[4] = {};
		d->GetPixelShaderConstantF(0, savedPS, 1);

		d->SetRenderTarget(0, Surface);
		d->SetDepthStencilSurface(NULL);
		state->SetVertexShader(NULL, false);
		state->SetPixelShader(PS, false);
		d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
		auto convert = [&](IDirect3DCubeTexture9* cube, UINT x, UINT y, UINT n) {
			const D3DVIEWPORT9 tile = { x, y, n, n, 0.0f, 1.0f };
			d->SetViewport(&tile);
			d->SetTexture(0, cube);
			const float k[4] = { (float)n, 0.0f, 0.0f, 0.0f };
			d->SetPixelShaderConstantF(0, k, 1);
			struct V { float x, y, z, w, u, v; };
			const float x0 = x - 0.5f, y0 = y - 0.5f, x1 = x + n - 0.5f, y1 = y + n - 0.5f;
			const V quad[4] = { { x0, y0, 0, 1, 0, 0 }, { x1, y0, 0, 1, 1, 0 }, { x0, y1, 0, 1, 0, 1 }, { x1, y1, 0, 1, 1, 1 } };
			d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V));
		};
		auto uv = [&](PointShadowForward::Slot& slot, UINT x, UINT y, UINT n) {
			slot.tile[0] = (x + 0.5f) / Size; slot.tile[1] = (y + 0.5f) / Size; slot.tile[2] = (n - 1.0f) / Size;
		};
		for (int s = 0; s < PointShadowForward::SlotCount; s++) {
			PointShadowForward::Slot& slot = PointShadowForward::Slots[s];
			slot.tile[0] = slot.tile[1] = slot.tile[2] = 0.0f;
			if (!slot.cube) continue;
			UINT x, y, n;
			if (slot.slot < ShadowCubeMapsMax) {
				if (!LiveRect(slot.slot, liveSlots, x, y, n)) { if (GpuTimer::Enabled) StatNoTile++; continue; }
				convert(slot.cube, x, y, n);
				uv(slot, x, y, n);
				if (GpuTimer::Enabled) StatLive++;
			}
			else {
				const int index = slot.slot - ShadowCubeMapsMax;
				if (index >= ShadowPoolMax || !PoolRect(index, liveSlots, x, y, n)) { if (GpuTimer::Enabled) StatNoTile++; continue; }
				PoolTile& t = PoolTiles[index];
				const ShadowPool::Cube* cube = index < (int)ShadowPool::Cubes.size() ? &ShadowPool::Cubes[index] : nullptr;
				const UInt32 drawn = cube ? cube->drawn : 0;
				if (t.light != slot.light || t.texture != slot.cube || t.drawn != drawn || t.x != slot.position.x || t.y != slot.position.y ||
					t.z != slot.position.z || t.radius != slot.radius) {
					convert(slot.cube, x, y, n);
					t = { slot.light, slot.cube, drawn, slot.position.x, slot.position.y, slot.position.z, slot.radius };
					if (GpuTimer::Enabled) StatPoolConverted++;
				}
				uv(slot, x, y, n);
				if (GpuTimer::Enabled) StatPool++;
			}
		}

		for (unsigned i = 0; i < sizeof(kSS) / sizeof(kSS[0]); i++) if (savedS[i] != wantS[i]) d->SetSamplerState(0, kSS[i], savedS[i]);
		for (unsigned i = 0; i < sizeof(kRS) / sizeof(kRS[0]); i++) if (savedRS[i] != want[i]) d->SetRenderState(kRS[i], savedRS[i]);
		d->SetPixelShaderConstantF(0, savedPS, 1);
		if (rt0) { d->SetRenderTarget(0, rt0); rt0->Release(); }
		d->SetDepthStencilSurface(zs); if (zs) zs->Release();
		d->SetViewport(&vp);
		d->SetTexture(0, tex0); if (tex0) tex0->Release();
		if (decl) { d->SetVertexDeclaration(decl); decl->Release(); } else d->SetFVF(fvf);
		state->SetVertexShader(vs, false); if (vs) vs->Release();
		state->SetPixelShader(ps, false); if (ps) ps->Release();

		if (GpuTimer::Enabled && ++StatFrames >= 240) {
			const double f = 1.0 / StatFrames;
			Logger::Log("SHADOW ATLAS per frame: %.1f live tiles converted, %.1f pool tiles used (%.2f converted), %.2f lamps without a tile",
				StatLive * f, StatPool * f, StatPoolConverted * f, StatNoTile * f);
			StatFrames = 0; StatLive = StatPool = StatPoolConverted = StatNoTile = 0;
		}
	}
}
