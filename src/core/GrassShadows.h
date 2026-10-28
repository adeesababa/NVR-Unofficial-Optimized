#pragma once

// UNOFFICIAL 2026-10-27 grass sun shadows ([Shaders.Grass.Main] CastShadows). User: "no grass shadows are broken ... billboard bushes
// have shadows as they should". The sun's shadow maps are drawn from the scene graph (ShadowManager::WalkSunCell: the cells' land and
// references), and the grass tufts are not in it -- the game places them per draw from an instance table (TallGrassShader) -- so they
// never cast anything; only the shading ON the grass took the shadows of others (GRASS23x000TMS.pso).
//
// Now, in GrassFirst's early grass call (Render.cpp namespace GrassFirst; needs [Shaders.Grass.Main] DrawFirst, on by default), once
// the near grass is drawn the same grass passes are drawn again into the NEAR sun cascade of the shadow atlas, before the land and
// everything after it read the atlas. For that span the device's DrawIndexedPrimitive is ours (as NewGrass / GpuGrass do it): each
// grass draw gets the caster shaders (the grass VS compiled with GRASS_SHADOW_CASTER, GrassShadowCaster.pso), its depth MIN-blended
// into the map's (the shadow map's depth test where the format does not blend), and the camera's view -> shadow map transform in
// c252-c255 (xyz; the grass wind lanes in w are kept), all put back after it.
// The tufts sway, fade and stand exactly where they are drawn (same VS code, same constants); the collision push is left out.
//
// 2026-10-28: with terrain blending on GrassFirst stands aside (its depth copy would hold the grass); then Render.cpp ShadowOnlyEarly
// takes the grass passes out before the land without drawing them, they come here, and the late part draws them whole on screen.
//
// Limits: the PCF shadow maps (Mode 3, the default) only -- the variance modes filter their maps after drawing; the near cascade only;
// the grass drawn early does not take these shadows itself (it is drawn before them), the ground and everything after it does.

#include <d3dx9.h>
#include <vector>
#include "NewGrass.h"	// LastVariant: the grass VS bound last

namespace GrassShadows {
	struct Record { void* pass; UInt32 passId, a3, a4, a5; };

	inline bool Enabled = true;			// [Shaders.Grass.Main] CastShadows (live)
	inline std::vector<Record> Records;	// this frame's early grass passes drawn with NVR's grass VS (GrassFirst)
	inline bool Active = false;			// between Begin and End: the grass draws go into the shadow map
	inline bool Hooked = false;
	inline ShaderRecordVertex* VS[4] = {};	// GRASS23x000-003.vso as casters (the 1POINT VS 004-007 are 000-003 plus the lamp)
	inline ShaderRecordPixel* PS = nullptr;
	inline bool LoadTried = false, LoadFailed = false;
	inline const D3DXMATRIX* CameraToLight = nullptr;	// the near cascade's camera-relative world -> shadow map space (Begin)
	inline DWORD ZFunc = D3DCMP_LESSEQUAL;
	inline IDirect3DSurface9* SavedTarget[4] = {};
	inline IDirect3DSurface9* SavedDepth = nullptr;
	inline D3DVIEWPORT9 SavedViewport = {};
	inline D3DXVECTOR4 NearSphere;		// the near cascade's sphere (centre camera-relative, w radius)
	inline unsigned Frames = 0, StatPasses = 0, StatOutside = 0, StatDraws = 0, StatSkipped = 0, StatRetarget = 0;
	// 2026-10-27 (user, at a low sun: "there are still no shadows", the log saying the passes were drawn): the draws no longer depend
	// on the atlas's depth buffer (its contents after the cascades are not something to count on: it is created discardable, and
	// whatever ran after the cascades may have used or cleared it). Where the format blends (R32F does on every card that runs
	// this), each grass pixel is MIN-blended into the depth the map holds: the nearer of the two stays, as a depth test would keep.
	inline bool MinBlend = false, MinBlendChecked = false;
	inline bool Black = false;			// [Shaders.Grass.Main] CastShadowsBlack (live, diagnostic): the casters write depth 0, so their shadow is solid black
	inline IDirect3DSurface9* AtlasTarget = nullptr;	// Begin .. End: what every grass draw must go into (DrawHook puts it back if a pass changed it)
	inline IDirect3DSurface9* AtlasDepth = nullptr;
	inline D3DVIEWPORT9 AtlasViewport = {};
	// An occlusion query round the replay: how many shadow map pixels the grass really wrote (logged with the frame lines).
	inline IDirect3DQuery9* Query = nullptr;
	inline bool QueryOpen = false, QueryPending = false;
	inline DWORD LastPixels = 0;
	inline bool PixelsKnown = false;
	inline D3DXVECTOR3 ProbeLight = { 0.0f, 0.0f, 0.0f };	// the first near pass's bound centre in the near map's space (x, y -1..1 and z 0..1 on the map)
	inline bool ProbeSet = false;

	typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitiveFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
	inline DrawIndexedPrimitiveFn Next = nullptr;

	inline void ReadSettings() {
		Enabled = TheSettingManager->GetSettingI("Shaders.Grass.Main", "CastShadows") != 0;
		Black = TheSettingManager->GetSettingI("Shaders.Grass.Main", "CastShadowsBlack") != 0;
	}

	// GrassFirst: an early grass pass just drawn with NVR's grass VS.
	inline void Note(void* pass, UInt32 passId, UInt32 a3, UInt32 a4, UInt32 a5) {
		if (Enabled) Records.push_back({ pass, passId, a3, a4, a5 });
	}

	// Does a grass pass's world bound reach the near cascade's sphere? (Unknown bounds: yes.)
	inline bool InNear(void* pass) {
		NiAVObject* geometry = *(NiAVObject**)pass;
		if (!geometry) return true;
		const NiBound* bound = geometry->GetWorldBound();
		if (!bound || bound == NiBound::GetGlobalWorldBound() || !(bound->Radius > 0.0f)) return true;
		const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
		const float dx = bound->Center.x - cam.x - NearSphere.x, dy = bound->Center.y - cam.y - NearSphere.y, dz = bound->Center.z - cam.z - NearSphere.z;
		const float reach = bound->Radius + NearSphere.w;
		const bool inside = dx * dx + dy * dy + dz * dz <= reach * reach;
		if (inside && !ProbeSet && CameraToLight) {	// for the log: where the map puts this piece (CPU side, the same transform as the VS)
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

	// One grass draw while Active: into the shadow map with the caster shaders, everything it changed put back after.
	inline HRESULT STDMETHODCALLTYPE DrawHook(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT numVertices, UINT start, UINT count) {
		if (!Active) return Next(device, type, base, minIndex, numVertices, start, count);
		// The grass VS this pass bound (GrassShaders::SetForDraw): only NVR's carries the caster's twin; anything else draws nothing here.
		const char v = NewGrass::LastVariant;
		ShaderRecordVertex* vs = GrassShaders::LastBindNVR && v >= '0' && v <= '7' ? VS[(v - '0') & 3] : nullptr;
		if (!vs || !vs->ShaderHandle || !CameraToLight) { StatSkipped++; return D3D_OK; }

		// c252-c255: the inverse view matrix (rows) with the wind lanes in w -> the view -> shadow map transform, lanes kept.
		float view[4][4];
		if (FAILED(device->GetVertexShaderConstantF(252, &view[0][0], 4))) { StatSkipped++; return D3D_OK; }
		D3DXMATRIX invView;
		for (int r = 0; r < 4; r++) { for (int c = 0; c < 3; c++) invView.m[r][c] = view[r][c]; invView.m[r][3] = r == 3 ? 1.0f : 0.0f; }
		D3DXMATRIX viewToLight;
		D3DXMatrixMultiply(&viewToLight, &invView, CameraToLight);
		float light[4][4];
		for (int r = 0; r < 4; r++) { for (int c = 0; c < 3; c++) light[r][c] = viewToLight.m[r][c]; light[r][3] = view[r][3]; }
		device->SetVertexShaderConstantF(252, &light[0][0], 4);

		// MinBlend: no depth test, the nearer depth kept by the blend (BLENDOP MIN ignores the factors). Else the shadow maps' own test.
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

		// The near cascade whatever the pass bound (a pass could set its own targets or viewport): put back after the draw.
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

	// After GrassFirst's early grass: the near cascade of the shadow atlas becomes the target and the draws are ours. False: nothing to do
	// (off, no early grass, indoors, no sun shadow maps this frame, not PCF), and nothing was changed.
	inline bool Begin(IDirect3DDevice9* device) {
		Active = false;
		if (!Enabled || Records.empty() || !device || !TheShaderManager->GameState.isExterior) return false;
		ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		if (!shadows || !shadows->Enabled || !shadows->ShadowAtlasSurface || !shadows->ShadowAtlasDepthSurface) return false;
		if (shadows->Settings.ShadowMaps.Mode != 3 || shadows->ShadowAtlasSurfaceMSAA) {
			static bool told = false;
			if (!told) Logger::Log("UNOFFICIAL grass shadows: only with the PCF sun shadow maps ([Shaders.ShadowsExteriors.ShadowMaps] Mode 3, no MSAA): off");
			told = true;
			return false;
		}
		if (!(shadows->Constants.ShadowFade.y > 0.0f) || shadows->Constants.ShadowFade.x >= 1.0f) return false;	// no sun shadow maps this frame
		if (!LoadShaders()) return false;

		const ShadowsExteriorEffect::ShadowMapSettings& map = shadows->ShadowMaps[ShadowsExteriorEffect::MapNear];
		CameraToLight = &map.ShadowCameraToLight;
		NearSphere = map.ShadowMapCascadeCenterRadius;
		ProbeSet = false;
		// The depth test the shadow maps are drawn with (ShadowManager::RenderShadowMaps).
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
		// Last frame's pixel count (never waits: read when the card has it).
		if (QueryPending && Query) {
			DWORD pixels = 0;
			const HRESULT q = Query->GetData(&pixels, sizeof(pixels), 0);
			if (q == S_OK) { LastPixels = pixels; PixelsKnown = true; QueryPending = false; }
			else if (q != S_FALSE) { Query->Release(); Query = nullptr; QueryPending = false; }	// device lost or the like: made again
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
		device->SetRenderTarget(0, shadows->ShadowAtlasSurface);	// (resets the viewport: set after)
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
		Frames++;
		if (Frames <= 3 || Frames == 60 || Frames == 600 || Frames % 3600 == 0) {
			char pixels[32] = "not known yet";
			if (PixelsKnown) sprintf_s(pixels, "%lu", (unsigned long)LastPixels);
			Logger::Log("UNOFFICIAL grass shadows: frame %u: %u early grass passes drawn into the near sun shadow map (%u draws; %u skipped: not NVR's grass VS; %u put back on the map: a pass had changed the target or viewport), %u outside the near cascade; map pixels written (last read) %s; first near piece at %.3f %.3f %.3f in the map (x, y -1..1, z 0..1 to be on it)%s",
				Frames, StatPasses, StatDraws, StatSkipped, StatRetarget, StatOutside, pixels, ProbeLight.x, ProbeLight.y, ProbeLight.z, Black ? "; CastShadowsBlack on" : "");
		}
		StatPasses = StatOutside = StatDraws = StatSkipped = StatRetarget = 0;
	}
}
