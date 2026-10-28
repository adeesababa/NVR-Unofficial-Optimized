#pragma once


#include <d3dx9.h>
#include <vector>
#include "NewGrass.h"

extern unsigned SunCascadesDrawnMask;

namespace GrassShadows {
	struct Record { void* pass; UInt32 passId, a3, a4, a5; };

	inline bool Enabled = true;
	inline float Reach = 1050.0f;
	inline int Cascade = 0;
	inline const char* const CascadeNames[3] = { "near", "middle", "far" };
	inline std::vector<Record> Records;
	inline bool Active = false;
	inline bool Hooked = false;
	inline ShaderRecordVertex* VS[4] = {};
	inline ShaderRecordPixel* PS = nullptr;
	inline bool LoadTried = false, LoadFailed = false;
	inline const D3DXMATRIX* CameraToLight = nullptr;
	inline DWORD ZFunc = D3DCMP_LESSEQUAL;
	inline IDirect3DSurface9* SavedTarget[4] = {};
	inline IDirect3DSurface9* SavedDepth = nullptr;
	inline D3DVIEWPORT9 SavedViewport = {};
	inline D3DXVECTOR4 NearSphere;
	inline unsigned Frames = 0, StatPasses = 0, StatOutside = 0, StatDraws = 0, StatSkipped = 0, StatRetarget = 0;
	inline bool MinBlend = false, MinBlendChecked = false;
	inline bool Black = false;
	inline IDirect3DSurface9* AtlasTarget = nullptr;
	inline IDirect3DSurface9* AtlasDepth = nullptr;
	inline D3DVIEWPORT9 AtlasViewport = {};
	inline IDirect3DQuery9* Query = nullptr;
	inline bool QueryOpen = false, QueryPending = false;
	inline DWORD LastPixels = 0;
	inline bool PixelsKnown = false;
	inline D3DXVECTOR3 ProbeLight = { 0.0f, 0.0f, 0.0f };
	inline bool ProbeSet = false;

	typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	inline DrawIndexedPrimitiveFn Next = nullptr;

	inline void ReadSettings() {
		Enabled = TheSettingManager->GetSettingI("Shaders.Grass.Main", "CastShadows") != 0;
		Black = TheSettingManager->GetSettingI("Shaders.Grass.Main", "CastShadowsBlack") != 0;
		Reach = max(0.0f, TheSettingManager->GetSettingF("Shaders.Grass.Main", "CastShadowsReach"));
	}

	inline void Note(void* pass, UInt32 passId, UInt32 a3, UInt32 a4, UInt32 a5) {
		if (Enabled) Records.push_back({ pass, passId, a3, a4, a5 });
	}

	inline bool InNear(void* pass) {
		NiAVObject* geometry = *(NiAVObject**)pass;
		if (!geometry) return Cascade == 0;
		const NiBound* bound = geometry->GetWorldBound();
		if (!bound || bound == NiBound::GetGlobalWorldBound() || !(bound->Radius > 0.0f)) return Cascade == 0;
		const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
		if (Cascade > 0) {
			const float cx = bound->Center.x - cam.x, cy = bound->Center.y - cam.y, cz = bound->Center.z - cam.z;
			const float limit = Reach + bound->Radius;
			if (cx * cx + cy * cy + cz * cz > limit * limit) return false;
		}
		const float dx = bound->Center.x - cam.x - NearSphere.x, dy = bound->Center.y - cam.y - NearSphere.y, dz = bound->Center.z - cam.z - NearSphere.z;
		const float reach = bound->Radius + NearSphere.w;
		const bool inside = dx * dx + dy * dy + dz * dz <= reach * reach;
		if (inside && !ProbeSet && CameraToLight) {
			const D3DXVECTOR3 rel(bound->Center.x - cam.x, bound->Center.y - cam.y, bound->Center.z - cam.z);
			D3DXVec3TransformCoord(&ProbeLight, &rel, CameraToLight);
			ProbeSet = true;
		}
		return inside;
	}

	inline bool LoadShaders() {
		if (LoadTried) return !LoadFailed;
		LoadTried = true;
		static const D3DXMACRO caster[] = { { "GRASS_SHADOW_CASTER", "1" }, { NULL, NULL } };
		static const char* const sources[4] = { "GRASS23x000.vso", "GRASS23x001.vso", "GRASS23x002.vso", "GRASS23x003.vso" };
		static const char* const names[4] = { "GrassShadowCaster0.vso", "GrassShadowCaster1.vso", "GrassShadowCaster2.vso", "GrassShadowCaster3.vso" };
		for (int i = 0; i < 4; i++) {
			ShaderTemplate t = {};
			t.Name = sources[i];
			VS[i] = (ShaderRecordVertex*)ShaderRecord::LoadShader(names[i], NULL, t, NULL, caster);
		}
		PS = (ShaderRecordPixel*)ShaderRecord::LoadShader("GrassShadowCaster.pso", NULL);
		LoadFailed = !PS || (!VS[0] && !VS[1] && !VS[2] && !VS[3]);
		Logger::Log("UNOFFICIAL grass shadows: caster shaders %s (VS %p %p %p %p, PS %p)", LoadFailed ? "FAILED to load: off" : "loaded", VS[0], VS[1], VS[2], VS[3], PS);
		return !LoadFailed;
	}

	inline HRESULT STDMETHODCALLTYPE DrawHook(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT numVertices, UINT start, UINT count) {
		if (!Active) return Next(device, type, base, minIndex, numVertices, start, count);
		const char v = NewGrass::LastVariant;
		ShaderRecordVertex* vs = GrassShaders::LastBindNVR && v >= '0' && v <= '7' ? VS[(v - '0') & 3] : nullptr;
		if (!vs || !vs->ShaderHandle || !CameraToLight) { StatSkipped++; return D3D_OK; }

		float view[4][4];
		if (FAILED(device->GetVertexShaderConstantF(252, &view[0][0], 4))) { StatSkipped++; return D3D_OK; }
		D3DXMATRIX invView;
		for (int r = 0; r < 4; r++) { for (int c = 0; c < 3; c++) invView.m[r][c] = view[r][c]; invView.m[r][3] = r == 3 ? 1.0f : 0.0f; }
		D3DXMATRIX viewToLight;
		D3DXMatrixMultiply(&viewToLight, &invView, CameraToLight);
		float light[4][4];
		for (int r = 0; r < 4; r++) { for (int c = 0; c < 3; c++) light[r][c] = viewToLight.m[r][c]; light[r][3] = view[r][3]; }
		device->SetVertexShaderConstantF(252, &light[0][0], 4);
		float proj[4][4];
		const bool projSaved = SUCCEEDED(device->GetVertexShaderConstantF(248, &proj[0][0], 4));
		device->SetVertexShaderConstantF(248, (const float*)&TheRenderManager->InvProjMatrix, 4);

		static const D3DRENDERSTATETYPE kStates[] = { D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE,
			D3DRS_ALPHABLENDENABLE, D3DRS_BLENDOP, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_SEPARATEALPHABLENDENABLE,
			D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_STENCILENABLE,
			D3DRS_DEPTHBIAS, D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_CLIPPLANEENABLE, D3DRS_ADAPTIVETESS_Y };
		const DWORD want[] = { MinBlend ? (DWORD)D3DZB_FALSE : (DWORD)D3DZB_TRUE, MinBlend ? 0u : 1u, ZFunc, D3DCULL_NONE, FALSE,
			MinBlend ? 1u : 0u, D3DBLENDOP_MIN, D3DBLEND_ONE, D3DBLEND_ONE, FALSE,
			0xF, FALSE, FALSE, FALSE, 0, 0, 0, 0 };
		static_assert(sizeof(kStates) / sizeof(kStates[0]) == sizeof(want) / sizeof(want[0]), "one value per state");
		DWORD saved[sizeof(kStates) / sizeof(kStates[0])];
		for (unsigned i = 0; i < sizeof(kStates) / sizeof(kStates[0]); i++) {
			if (FAILED(device->GetRenderState(kStates[i], &saved[i]))) saved[i] = want[i];
			if (saved[i] != want[i]) device->SetRenderState(kStates[i], want[i]);
		}
		IDirect3DVertexShader9* oldVS = nullptr;
		IDirect3DPixelShader9* oldPS = nullptr;
		device->GetVertexShader(&oldVS);
		device->GetPixelShader(&oldPS);
		device->SetVertexShader((IDirect3DVertexShader9*)vs->ShaderHandle);
		device->SetPixelShader((IDirect3DPixelShader9*)PS->ShaderHandle);
		float oldC0[4] = {};
		device->GetPixelShaderConstantF(0, oldC0, 1);
		const float casterParams[4] = { Black ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
		device->SetPixelShaderConstantF(0, casterParams, 1);

		IDirect3DSurface9* rt[4] = {};
		IDirect3DSurface9* ds = nullptr;
		D3DVIEWPORT9 vp = {};
		for (DWORD i = 0; i < 4; i++) if (FAILED(device->GetRenderTarget(i, &rt[i]))) rt[i] = nullptr;
		if (FAILED(device->GetDepthStencilSurface(&ds))) ds = nullptr;
		device->GetViewport(&vp);
		const bool retarget = rt[0] != AtlasTarget || rt[1] || rt[2] || rt[3] || ds != AtlasDepth || memcmp(&vp, &AtlasViewport, sizeof(vp)) != 0;
		if (retarget) {
			StatRetarget++;
			device->SetRenderTarget(0, AtlasTarget);
			for (DWORD i = 1; i < 4; i++) if (rt[i]) device->SetRenderTarget(i, nullptr);
			device->SetDepthStencilSurface(AtlasDepth);
			device->SetViewport(&AtlasViewport);
		}

		const HRESULT hr = Next(device, type, base, minIndex, numVertices, start, count);
		StatDraws++;

		if (retarget) {
			device->SetRenderTarget(0, rt[0]);
			for (DWORD i = 1; i < 4; i++) if (rt[i]) device->SetRenderTarget(i, rt[i]);
			device->SetDepthStencilSurface(ds);
			device->SetViewport(&vp);
		}
		for (DWORD i = 0; i < 4; i++) if (rt[i]) rt[i]->Release();
		if (ds) ds->Release();
		device->SetPixelShaderConstantF(0, oldC0, 1);
		device->SetVertexShader(oldVS);
		device->SetPixelShader(oldPS);
		if (oldVS) oldVS->Release();
		if (oldPS) oldPS->Release();
		for (unsigned i = 0; i < sizeof(kStates) / sizeof(kStates[0]); i++) if (saved[i] != want[i]) device->SetRenderState(kStates[i], saved[i]);
		device->SetVertexShaderConstantF(252, &view[0][0], 4);
		if (projSaved) device->SetVertexShaderConstantF(248, &proj[0][0], 4);
		return hr;
	}

	inline void Unhook(IDirect3DDevice9* device) {
		if (!Hooked || !device) return;
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

	inline bool Begin(IDirect3DDevice9* device, int cascade = ShadowsExteriorEffect::MapNear) {
		Active = false;
		if (!Enabled || Records.empty() || !device || !TheShaderManager->GameState.isExterior) return false;
		if (cascade < ShadowsExteriorEffect::MapNear || cascade > ShadowsExteriorEffect::MapFar) return false;
		if (cascade > ShadowsExteriorEffect::MapNear && (!(Reach > 0.0f) || !(SunCascadesDrawnMask & (1u << cascade)))) return false;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		if (!shadows || !shadows->Enabled || !shadows->ShadowAtlasSurface || !shadows->ShadowAtlasDepthSurface) return false;
		if (shadows->Settings.ShadowMaps.Mode != 3 || shadows->ShadowAtlasSurfaceMSAA) {
			static bool told = false;
			if (!told) Logger::Log("UNOFFICIAL grass shadows: only with the PCF sun shadow maps ([Shaders.ShadowsExteriors.ShadowMaps] Mode 3, no MSAA): off");
			told = true;
			return false;
		}
		if (!(shadows->Constants.ShadowFade.y > 0.0f) || shadows->Constants.ShadowFade.x >= 1.0f) return false;
		if (!LoadShaders()) return false;

		const ShadowsExteriorEffect::ShadowMapSettings& map = shadows->ShadowMaps[cascade];
		if (cascade > ShadowsExteriorEffect::MapNear) {
			const D3DXVECTOR4& inner = shadows->ShadowMaps[cascade - 1].ShadowMapCascadeCenterRadius;
			const float innerReach = sqrtf(inner.x * inner.x + inner.y * inner.y + inner.z * inner.z) + inner.w;
			if (innerReach >= Reach) return false;
		}
		Cascade = cascade;
		CameraToLight = &map.ShadowCameraToLight;
		NearSphere = map.ShadowMapCascadeCenterRadius;
		ProbeSet = false;
		ZFunc = (1.0 - NiDX9Renderer::GetSingleton()->m_fZClear) ? D3DCMP_GREATEREQUAL : D3DCMP_LESSEQUAL;
		if (!MinBlendChecked) {
			MinBlendChecked = true;
			IDirect3D9* d3d = nullptr;
			D3DSURFACE_DESC desc = {};
			if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d && SUCCEEDED(shadows->ShadowAtlasSurface->GetDesc(&desc)))
				MinBlend = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
					D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE, desc.Format));
			if (d3d) d3d->Release();
			Logger::Log("UNOFFICIAL grass shadows: %s (shadow map format %u, the game's depth clear %.1f: depth test %s)", MinBlend ?
				"the tufts are MIN-blended into the near map (no depth test)" : "the format does not blend: drawn with the depth test",
				(unsigned)desc.Format, NiDX9Renderer::GetSingleton()->m_fZClear, ZFunc == D3DCMP_LESSEQUAL ? "LESSEQUAL" : "GREATEREQUAL");
		}
		if (QueryPending && Query) {
			DWORD pixels = 0;
			const HRESULT q = Query->GetData(&pixels, sizeof(pixels), 0);
			if (q == S_OK) { LastPixels = pixels; PixelsKnown = true; QueryPending = false; }
			else if (q != S_FALSE) { Query->Release(); Query = nullptr; QueryPending = false; }
		}

		void** table = *(void***)device;
		if (table[82] != (void*)&DrawHook) {
			DWORD old = 0;
			if (!VirtualProtect(&table[82], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
			Next = (DrawIndexedPrimitiveFn)table[82];
			table[82] = (void*)&DrawHook;
			VirtualProtect(&table[82], sizeof(void*), old, &old);
			Hooked = true;
		}

		device->GetViewport(&SavedViewport);
		for (DWORD i = 0; i < 4; i++) if (FAILED(device->GetRenderTarget(i, &SavedTarget[i]))) SavedTarget[i] = nullptr;
		if (FAILED(device->GetDepthStencilSurface(&SavedDepth))) SavedDepth = nullptr;
		device->SetRenderTarget(0, shadows->ShadowAtlasSurface);
		for (DWORD i = 1; i < 4; i++) if (SavedTarget[i]) device->SetRenderTarget(i, nullptr);
		device->SetDepthStencilSurface(shadows->ShadowAtlasDepthSurface);
		device->SetViewport(&map.ShadowMapViewPort);
		AtlasTarget = shadows->ShadowAtlasSurface;
		AtlasDepth = shadows->ShadowAtlasDepthSurface;
		AtlasViewport = map.ShadowMapViewPort;
		if (!Query) device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &Query);
		QueryOpen = Query && !QueryPending && SUCCEEDED(Query->Issue(D3DISSUE_BEGIN));
		Active = true;
		return true;
	}

	inline void End(IDirect3DDevice9* device) {
		if (!Active) return;
		Active = false;
		if (QueryOpen) { QueryOpen = false; QueryPending = SUCCEEDED(Query->Issue(D3DISSUE_END)); }
		Unhook(device);
		device->SetRenderTarget(0, SavedTarget[0]);
		for (DWORD i = 1; i < 4; i++) if (SavedTarget[i]) device->SetRenderTarget(i, SavedTarget[i]);
		device->SetDepthStencilSurface(SavedDepth);
		device->SetViewport(&SavedViewport);
		for (DWORD i = 0; i < 4; i++) if (SavedTarget[i]) { SavedTarget[i]->Release(); SavedTarget[i] = nullptr; }
		if (SavedDepth) { SavedDepth->Release(); SavedDepth = nullptr; }
		if (Cascade == ShadowsExteriorEffect::MapNear) Frames++;
		if (Frames <= 3 || Frames == 60 || Frames == 600 || Frames % 3600 == 0) {
			char pixels[32] = "not known yet";
			if (PixelsKnown) sprintf_s(pixels, "%lu", (unsigned long)LastPixels);
			Logger::Log("UNOFFICIAL grass shadows: frame %u: %u early grass passes drawn into the %s sun shadow map (%u draws; %u skipped: not NVR's grass VS; %u put back on the map: a pass had changed the target or viewport), %u outside it%s; map pixels written (last read, near map) %s; first piece at %.3f %.3f %.3f in the map (x, y -1..1, z 0..1 to be on it)%s",
				Frames, StatPasses, CascadeNames[Cascade], StatDraws, StatSkipped, StatRetarget, StatOutside, Cascade ? " or past CastShadowsReach" : "", pixels, ProbeLight.x, ProbeLight.y, ProbeLight.z, Black ? "; CastShadowsBlack on" : "");
		}
		Cascade = ShadowsExteriorEffect::MapNear;
		StatPasses = StatOutside = StatDraws = StatSkipped = StatRetarget = 0;
	}
}
