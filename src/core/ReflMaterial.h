#pragma once
// UNOFFICIAL 2026-10-20 MaterialAware reflections ([Shaders.Reflections.Main] MaterialAware; deep/reflmat,
// bench\deep-roadmap\evidence\reflmat\RESULTS.md).
//
// The Reflections effect knew nothing of materials: it reflected surfaces that face up, with one roughness for all. The object
// shaders know each pixel's roughness and metal where an _rmaos map is bound (Includes/Object.hlsl RmaosValue). This carries it
// to the effect:
//  - D3D9 cannot draw into a second render target while the game's MSAA is on (SkinScattering.cpp measured it: nothing drawn at
//    all), so the scene's own alpha carries it. The world target is fp16; the object shaders' first passes write
//    2 + 0.25 x metal (4 steps) + 0.24 x roughness there (Object.hlsl ReflMaterialAlpha), always 2 or more, where the game writes 0-1.
//  - Only opaque, untested objects' first passes in the world scene, and only objects with their map on, write it (c212.x = 1,
//    set per pass here; the shader then takes the map's values without testing for it): a blended pass's
//    alpha weighs its colour (SRCALPHA), and an alpha-tested one is cut by it. The lamp and highlight runs (PassSums::KindOf) are
//    blended, so never.
//  - While the world scene is drawn, every blended pass leaves the target's alpha alone (separate alpha blend: ZERO x source +
//    ONE x destination), so the lamps and highlights added on top, decals and glass do not change what the first pass wrote; the
//    colour blends exactly as before (the separate factors apply to alpha only).
//  - The Reflections effect copies the alpha out into its own small buffer before any effect draws (ReflectionsEffect::
//    CaptureMaterial), and reads it there.
// Safety net: if a pass drew blended although it got c212.x = 1 (a pass kind not known above), its pass number is put on a list
// that never gets the flag again, and the log says so once.
// Off (MaterialAware 0 at startup, or the _rmaos maps off): nothing here runs and the shaders are compiled without it.
#include <d3d9.h>
#include <unordered_set>
#include "PassSums.h"

namespace ReflMaterial {
	inline int Startup = -1;					// MaterialAware > 0 and the _rmaos maps on, read once (the shaders are compiled with it)
	inline bool Writing = false;				// this frame's world scene writes the material (MaterialAware > 0 now)
	inline float Current = -1.0f;				// c212.x as last sent (-1: send on the next pass)
	inline bool StatesSet = false;
	inline DWORD SavedSeparate = FALSE, SavedSrcAlpha = D3DBLEND_ONE, SavedDstAlpha = D3DBLEND_ZERO, SavedOpAlpha = D3DBLEND_ONE;
	// Pass numbers that drew blended with the flag on (never flagged again). Seeded with the ones the first in-game run found (2026-10-21,
	// Ultra-Luxe: each drew blended once while loading): 14A 14D 14E and 248 252 254.
	inline std::unordered_set<UInt16> Refused = { 0x14A, 0x14D, 0x14E, 0x248, 0x252, 0x254 };
	inline UInt32 FlaggedPasses = 0, Frames = 0;
	inline UInt16 LastFlaggedPass = 0;
	inline bool LastFlagged = false;

	// Whether the object shaders were compiled with it (asked by ShaderRecord::BuildDefines, at the first shader load).
	inline bool CompiledIn() {
		if (Startup < 0)
			Startup = TheSettingManager->GetSettingI("Shaders.Reflections.Main", "MaterialAware") > 0 &&
				TheSettingManager->GetSettingI("Shaders.PBR.Rmaos", "Enabled") ? 1 : 0;
		return Startup > 0;
	}

	inline void Send(IDirect3DDevice9* device, float v) {
		if (v == Current) return;
		Current = v;
		const float c[4] = { v, 0.0f, 0.0f, 0.0f };
		device->SetPixelShaderConstantF(212, c, 1);
	}

	// The world scene starts: the blended passes keep off the alpha (through the game's render state cache, so its own later
	// settings stay right), and c212 is sent again on the first pass (an effect may have used the register since).
	inline void BeginWorld(NiDX9RenderState* state, IDirect3DDevice9* device, bool live) {
		Writing = CompiledIn() && live;
		Current = -1.0f;
		if (!Writing || !state || !device) return;
		device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &SavedSeparate);
		device->GetRenderState(D3DRS_SRCBLENDALPHA, &SavedSrcAlpha);
		device->GetRenderState(D3DRS_DESTBLENDALPHA, &SavedDstAlpha);
		device->GetRenderState(D3DRS_BLENDOPALPHA, &SavedOpAlpha);
		state->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO, RenderStateArgs);
		state->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ONE, RenderStateArgs);
		state->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD, RenderStateArgs);
		state->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE, RenderStateArgs);
		StatesSet = true;
		Frames++;
	}

	inline void EndWorld(NiDX9RenderState* state, IDirect3DDevice9* device) {
		if (device && Current != 0.0f && Current != -1.0f) Send(device, 0.0f);
		if (StatesSet && state && device) {
			// Still on? (the game never sets it, as far as known; if it did, the log says so once)
			DWORD now = FALSE;
			device->GetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, &now);
			static bool warned = false;
			if (!now && !warned) { warned = true; Logger::Log("[WARNING] MaterialAware reflections: separate alpha blending was switched off during the world scene; blended draws after that point change the material buffer"); }
			state->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, SavedSeparate, RenderStateArgs);
			state->SetRenderState(D3DRS_SRCBLENDALPHA, SavedSrcAlpha, RenderStateArgs);
			state->SetRenderState(D3DRS_DESTBLENDALPHA, SavedDstAlpha, RenderStateArgs);
			state->SetRenderState(D3DRS_BLENDOPALPHA, SavedOpAlpha, RenderStateArgs);
		}
		StatesSet = false;
		Writing = false;
		if (Frames >= 1800) {
			Logger::Log("MaterialAware reflections: %u object passes per frame wrote their material (average of %u frames); %u pass kinds refused",
				FlaggedPasses / Frames, Frames, (unsigned)Refused.size());
			FlaggedPasses = 0; Frames = 0;
		}
	}

	// Before each object pass (Render.cpp RenderPassStandardHook / RenderPassSkinnedHook). world: in the world scene, with an _rmaos map.
	inline void ForPass(const void* pass, NiGeometry* geometry, bool world, IDirect3DDevice9* device) {
		LastFlagged = false;
		if (!Writing || !device) return;
		bool flag = world && pass && geometry;
		UInt16 e = 0;
		if (flag) {
			e = *(const UInt16*)((const UInt8*)pass + 0x04);
			flag = PassSums::KindOf(e) == PassSums::None && !Refused.count(e);
		}
		if (flag) {
			const NiAlphaProperty* alpha = geometry->propertyState.m_spAlphaProperty;
			flag = !alpha || !(alpha->flags & (NiAlphaProperty::ALPHA_BLEND_MASK | NiAlphaProperty::TEST_ENABLE_MASK));
		}
		Send(device, flag ? 1.0f : 0.0f);
		if (flag) { FlaggedPasses++; LastFlagged = true; LastFlaggedPass = e; }
	}

	// After the pass drew: the flag goes back to 0 at once (draws that do not come through the pass hooks, such as the sorted
	// blended ones, must never see it), and the state the pass drew with is still set: a flagged pass that blended goes on the
	// refused list.
	inline void AfterPass(IDirect3DDevice9* device) {
		if (!LastFlagged || !device) return;
		LastFlagged = false;
		Send(device, 0.0f);
		DWORD blend = FALSE;
		device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
		if (!blend) return;
		Refused.insert(LastFlaggedPass);
		Logger::Log("[WARNING] MaterialAware reflections: pass %03X drew blended with the material flag on (its colour may have been wrong for one draw); it is left out from now on", (unsigned)LastFlaggedPass);
	}
}
