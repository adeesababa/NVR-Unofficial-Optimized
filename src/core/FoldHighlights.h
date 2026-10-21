#pragma once
#include "PassSums.h"
#include "PbrPop.h"

namespace FoldHighlights {

	enum Kind : UInt8 { KindNone = 0, KindLight, KindLamps };
	inline bool Checked = false, Compiled = false, On = false;
	inline UInt16 BindPass = 0xFFFF;
	inline Kind BindKind = KindNone;
	constexpr UInt32 kBaseStage = 7;
	inline IDirect3DBaseTexture9* LastBase = nullptr;
	inline unsigned long long BaseLost = 0, StageRefused = 0;
	inline UInt32 MaxStages = 0;
	inline unsigned LostLogged = 0;
	inline const NiGeometry* LastGeometry = nullptr;
	inline UInt16 LastPass = 0;

	constexpr unsigned kSlots = 8192;
	inline const void* Key[kSlots] = {};
	inline unsigned Frame[kSlots] = {};
	inline UInt8 State[kSlots] = {};
	inline unsigned FrameId = 1;
	inline unsigned long long Folded = 0, LampsFolded = 0, Skipped = 0, Broken = 0, Frames = 0;
	inline unsigned long long VcFlag = 0, VcPass = 0, VcMismatch = 0;

	inline UInt8* Slot(const void* geometry, bool insert) {
		unsigned h = (unsigned)(((uintptr_t)geometry >> 4) * 2654435761u) & (kSlots - 1);
		for (unsigned probe = 0; probe < 64; probe++, h = (h + 1) & (kSlots - 1)) {
			if (Frame[h] == FrameId && Key[h] == geometry) return &State[h];
			if (Frame[h] != FrameId) {
				if (!insert) return nullptr;
				Key[h] = geometry; Frame[h] = FrameId; State[h] = 0;
				return &State[h];
			}
		}
		return nullptr;
	}

	inline void BeginFrame() {
		if (!Checked) {
			Checked = true;
			Compiled = TheSettingManager->GetSettingI("Shaders.PBR.Main", "FoldHighlights") != 0;
			PbrPop::SpecFade::FoldCompiled = Compiled;
			Logger::Log("UNOFFICIAL FoldHighlights: %s", Compiled ? "compiled in (the highlights of shiny objects with 3+ lamps drawn with their lamps' light)" :
				"off (not compiled into the shaders)");
		}
		On = Compiled && TheSettingManager->GetSettingI("Shaders.PBR.Main", "FoldHighlights") != 0;
		FrameId++;
		if (On && ++Frames >= 600) {
			Logger::Log("UNOFFICIAL FoldHighlights: per frame %.1f light passes and %.1f extra-lamp passes folded, %.1f highlight passes skipped, %.2f objects not "
				"folded in full (highlights kept); vertex colour flag on %.1f of folded objects, texture pass with it %.1f, disagreeing %.2f; base texture replaced by the game %.2f; most texture stages of the game's extra-lamp passes %u (refused %.2f)",
				Folded / (double)Frames, LampsFolded / (double)Frames, Skipped / (double)Frames, Broken / (double)Frames,
				VcFlag / (double)Frames, VcPass / (double)Frames, VcMismatch / (double)Frames, BaseLost / (double)Frames, MaxStages, StageRefused / (double)Frames);
			Folded = LampsFolded = Skipped = Broken = Frames = VcFlag = VcPass = VcMismatch = BaseLost = StageRefused = 0;
		}
	}

	inline void OnBind(const char* ps, bool nvr, UInt32 passIndex) {
		BindPass = (UInt16)passIndex;
		BindKind = KindNone;
		if (!Compiled || !nvr || !ps || strncmp(ps, "SLS", 3)) return;
		switch (atoi(ps + 3)) {
		case 2037: case 2039: case 2041: case 2043: BindKind = KindLight; break;
		case 2045: case 2046: BindKind = KindLamps; break;
		}
	}

	inline bool Foldable(const NiGeometry* geometry) {
		const BSShaderProperty* property = PbrPop::LitProperty(geometry);
		if (!property) return false;
		const UInt32 flags = property->ulFlags[0];
		if (!(flags & BSShaderProperty::Specular)) return false;
		if (flags & (BSShaderProperty::FaceGen | BSShaderProperty::Parallax_Shader | BSShaderProperty::Model_Space_Normals | BSShaderProperty::Hair |
			BSShaderProperty::Landscape | BSShaderProperty::Tree_Billboard | BSShaderProperty::Parallax_Occlusion)) return false;
		return PbrPop::GameSpecularFade(property) > 0.0f;
	}

	inline bool Skip(const void* passPtr, bool world) {
		if (!On || !world || !passPtr) return false;
		const PointShadowForward::RenderPassView* pass = (const PointShadowForward::RenderPassView*)passPtr;
#if NVR_TEST_TOOLS
		if (pass->passEnum >= 0x14A && pass->passEnum <= 0x150) {
			const UInt8* state = Slot(pass->geometry, false);
			if (state && *state == 1) {
				const BSShaderProperty* property = PbrPop::LitProperty(pass->geometry);
				const char* name = Pointers::Functions::GetPassDescription(pass->passEnum);
				const bool flag = property && (property->ulFlags[1] & BSShaderProperty::Vertex_Colors);
				const bool vc = name && strstr(name, "_Vc");
				VcFlag += flag; VcPass += vc; VcMismatch += flag != vc;
			}
		}
#endif
		if (PassSums::KindOf(pass->passEnum) != PassSums::Highlights) return false;
		const UInt8* state = Slot(pass->geometry, false);
		if (!state || *state != 1) return false;
		Skipped++;
		return true;
	}

	inline void ForDraw(IDirect3DDevice9* device, const void* passPtr, bool world, float& fold, float& vertexColor) {
		fold = 0.0f; vertexColor = 0.0f; LastBase = nullptr;
		if (!On || !world || !passPtr) return;
		const PointShadowForward::RenderPassView* pass = (const PointShadowForward::RenderPassView*)passPtr;
		if (pass->passEnum != BindPass || BindKind == KindNone) {
			if (PassSums::KindOf(pass->passEnum) == PassSums::Lamps) {
				UInt8* state = Slot(pass->geometry, false);
				if (state && *state == 1) { *state = 2; Broken++; }
			}
			return;
		}
		NiGeometry* geometry = pass->geometry;
		if (!Foldable(geometry)) return;
		const BSShaderPPLightingProperty* property = (const BSShaderPPLightingProperty*)PbrPop::LitProperty(geometry);
		if (BindKind == KindLight) {
			UInt8* state = Slot(geometry, true);
			if (!state) return;
			*state = 1;
			Folded++;
		}
		else {
			UInt8* state = Slot(geometry, false);
			if (!state || *state != 1) return;
			const NiSourceTexture* base = property->ppTextures[0] ? property->ppTextures[0][0] : nullptr;
			IDirect3DBaseTexture9* texture = base && base->rendererData ? base->rendererData->dTexture : nullptr;
			const NiD3DPass* d3dPass = *(NiD3DPass**)0x0126F74C;
			const UInt32 stages = d3dPass ? d3dPass->StageCount : 0;
			if (stages > MaxStages) MaxStages = stages;
			if (!texture || stages > kBaseStage) { if (stages > kBaseStage) StageRefused++; *state = 2; Broken++; return; }
			device->SetTexture(kBaseStage, texture);
			TheRenderManager->renderState->NoteDeviceTexture(kBaseStage, texture);
			LastBase = texture; LastGeometry = geometry; LastPass = pass->passEnum;
			static const D3DSAMPLERSTATETYPE kStates[] = { D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER,
				D3DSAMP_MAXANISOTROPY, D3DSAMP_MIPMAPLODBIAS };
			for (D3DSAMPLERSTATETYPE type : kStates) {
				DWORD value = 0;
				device->GetSamplerState(0, type, &value);
				TheRenderManager->SetSamplerState(kBaseStage, type, value);
			}
			LampsFolded++;
		}
		fold = 1.0f;
		vertexColor = (property->ulFlags[1] & BSShaderProperty::Vertex_Colors) ? 1.0f : 0.0f;
	}

#if NVR_TEST_TOOLS
	inline void AfterDraw(IDirect3DDevice9* device) {
		if (!LastBase) return;
		IDirect3DBaseTexture9* now = nullptr;
		if (SUCCEEDED(device->GetTexture(kBaseStage, &now)) && now) now->Release();
		if (now != LastBase) {
			BaseLost++;
			if (LostLogged < 24 && LastGeometry) {
				LostLogged++;
				const BSShaderPPLightingProperty* property = (const BSShaderPPLightingProperty*)PbrPop::LitProperty(LastGeometry);
				int which = -1;
				for (int i = 0; i < 6 && property; i++) {
					const NiSourceTexture* t = property->ppTextures[i] ? property->ppTextures[i][0] : nullptr;
					if (t && t->rendererData && t->rendererData->dTexture == now) which = i;
				}
				const NiAVObject* parent = (const NiAVObject*)LastGeometry->m_parent;
				Logger::Log("FOLD BASE LOST '%s' (in '%s') pass %03X %s flags %08X %08X: sampler %u now %p (property texture %d)", LastGeometry->m_pcName ? LastGeometry->m_pcName : "",
					parent && parent->m_pcName ? parent->m_pcName : "", (unsigned)LastPass, Pointers::Functions::GetPassDescription(LastPass),
					property ? property->ulFlags[0] : 0u, property ? property->ulFlags[1] : 0u, kBaseStage, now, which);
			}
		}
		LastBase = nullptr;
	}
#endif
}
