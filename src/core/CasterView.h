#pragma once
#include <d3dx9.h>
#include <unordered_set>
#include "TestTools.h"
#include "LampsInOnePass.h"

extern unsigned SunCascadesDrawnMask;

#if NVR_TEST_TOOLS
namespace CasterView {

	enum Kind { KCasts = 0, KOther, KNone, KFading, KLodCasts, KLodNone, KBeyond, KCount };
	inline const float kColour[KCount][3] = {
		{ 0.10f, 0.85f, 0.15f }, { 1.00f, 0.90f, 0.05f }, { 1.00f, 0.06f, 0.04f }, { 0.95f, 0.08f, 0.90f },
		{ 0.05f, 0.85f, 0.85f }, { 1.00f, 0.45f, 0.02f }, { 0.45f, 0.45f, 0.45f } };
	inline const char* const kName[KCount] = { "casts", "casts-elsewhere", "NO-CASTER", "FADING", "lod-casts", "LOD-NO-CASTER", "beyond" };
	constexpr int kShades = 4;
	inline const float kShade[kShades] = { 0.55f, 0.70f, 0.85f, 1.00f };

	inline int Mode = 0, OnFrames = 0;
	inline bool Failed = false, Replaced = false, InPass = false;
	inline IDirect3DPixelShader9* Shader[KCount][kShades] = {};
	inline std::unordered_set<const NiGeometry*> Casts[4];
	inline const NiGeometry* Geometry = nullptr;
	inline Kind Current = KCasts;
	inline bool HaveCurrent = false;
	inline bool NeedNote = false;
	inline std::unordered_set<const void*> Logged;
	inline unsigned LogLines = 0, Counts[KCount] = {};

	inline void BeginFrame() {
		const int want = TheSettingManager->GetSettingI("Main.Main.Misc", "CasterView");
		if (want && !Mode) { OnFrames = 0; LogLines = 0; Logged.clear(); memset(Counts, 0, sizeof(Counts)); }
		if (Mode && OnFrames == 7) {
			Logger::Log("CASTER VIEW frame: objects by colour: casts %u, casts-elsewhere %u, NO-CASTER %u, FADING %u, lod-casts %u, LOD-NO-CASTER %u, beyond %u",
				Counts[KCasts], Counts[KOther], Counts[KNone], Counts[KFading], Counts[KLodCasts], Counts[KLodNone], Counts[KBeyond]);
		}
		Mode = want;
		if (Mode) OnFrames++;
		NeedNote = Mode != 0;
	}

	inline void NoteCascades(const std::vector<ShadowManager::SunCaster>* casters, unsigned mask) {
		if (!Mode) return;
		for (int c = 0; c < 4; c++) {
			if (!(mask & (1u << c))) continue;
			Casts[c].clear();
			for (const ShadowManager::SunCaster& s : casters[c]) Casts[c].insert(s.geometry);
		}
	}

	inline bool Make(IDirect3DDevice9* device) {
		if (Shader[0][0]) return true;
		if (Failed) return false;
		for (int c = 0; c < KCount; c++) for (int s = 0; s < kShades; s++) {
			char src[160];
			sprintf_s(src, "float4 main() : COLOR0 { return float4(%.4f, %.4f, %.4f, 1.0); }", kColour[c][0] * kShade[s], kColour[c][1] * kShade[s], kColour[c][2] * kShade[s]);
			ID3DXBuffer* code = nullptr; ID3DXBuffer* errors = nullptr;
			if (FAILED(D3DXCompileShader(src, (UINT)strlen(src), NULL, NULL, "main", "ps_3_0", 0, &code, &errors, NULL)) || !code ||
				FAILED(device->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &Shader[c][s]))) {
				Logger::Log("CasterView: shader failed: %s", errors ? (const char*)errors->GetBufferPointer() : "?");
				if (errors) errors->Release();
				if (code) code->Release();
				Failed = true;
				return false;
			}
			code->Release();
		}
		return true;
	}

	inline bool Skip(const void* passPtr, bool world) {
		if (!Mode || !world || !passPtr || !TheShaderManager->GameState.isExterior) return false;
		const UInt16 e = ((const PointShadowForward::RenderPassView*)passPtr)->passEnum;
		return LampsInOnePass::Census::IsLampPass(e) || LampsInOnePass::Census::IsHighlightPass(e) || (e >= 0x14A && e <= 0x150) ||
			(e >= 0x248 && e <= 0x255);
	}

	inline Kind Classify(const NiGeometry* g, int& cascade, float& distance, float& fade, bool& lod) {
		cascade = -1; fade = 1.0f; lod = false;
		const NiNode* lodRoot = BGSTerrainManager::GetRootObjectLODNode();
		for (const NiAVObject* p = (const NiAVObject*)g->m_parent; p; p = (const NiAVObject*)p->m_parent) {
			if (p == lodRoot) lod = true;
			if (fade >= 1.0f && const_cast<NiAVObject*>(p)->IsFadeNode()) fade = static_cast<const BSFadeNode*>(p)->FadeAlpha;
		}
		const NiBound* b = const_cast<NiGeometry*>(g)->GetWorldBound();
		const D3DXVECTOR4& cam = TheRenderManager->CameraPosition;
		const float x = b ? b->Center.x - cam.x : 0.0f, y = b ? b->Center.y - cam.y : 0.0f, z = b ? b->Center.z - cam.z : 0.0f;
		distance = sqrtf(x * x + y * y + z * z);
		const ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
		for (int c = 0; c < 4 && shadows; c++) {
			const D3DXVECTOR4& s = shadows->ShadowMaps[c].ShadowMapCascadeCenterRadius;
			const float dx = x - s.x, dy = y - s.y, dz = z - s.z;
			if (dx * dx + dy * dy + dz * dz <= s.w * s.w) { cascade = c; break; }
		}
		const bool here = cascade >= 0 && Casts[cascade].count(g);
		bool elsewhere = false;
		for (int c = 0; c < 4 && !here; c++) if (Casts[c].count(g)) elsewhere = true;
		if (lod) return here || elsewhere ? KLodCasts : KLodNone;
		if (fade < TheShaderManager->Effects.ShadowsExteriors->Settings.ShadowMaps.CasterFadeMin) return KFading;
		if (cascade < 0) return KBeyond;
		return here ? KCasts : elsewhere ? KOther : KNone;
	}

	inline void Apply(IDirect3DDevice9* device) {
		if (!HaveCurrent || !Make(device)) return;
		const unsigned s = (unsigned)((((uintptr_t)Geometry >> 4) * 2654435761u) >> 30);
		device->SetPixelShader(Shader[Current][s & 3]);
		Replaced = true;
	}

	inline void BeforeDraw(IDirect3DDevice9* device, const void* passPtr, bool world) {
		HaveCurrent = false;
		if (!Mode || !world || !passPtr || !TheShaderManager->GameState.isExterior) return;
		const PointShadowForward::RenderPassView* pass = (const PointShadowForward::RenderPassView*)passPtr;
		if (!pass->geometry) return;
		if (NeedNote) { NeedNote = false; NoteCascades(TheShadowManager->SunCasters, SunCascadesDrawnMask); }
		InPass = true;
		Geometry = pass->geometry;
		int cascade; float distance, fade; bool lod;
		Current = Classify(Geometry, cascade, distance, fade, lod);
		HaveCurrent = true;
		Apply(device);
		if (OnFrames >= 6 && Logged.insert(Geometry).second) {
			Counts[Current]++;
			if (LogLines < 1500 && Current != KCasts && Current != KBeyond) {
				LogLines++;
				const NiBound* b = const_cast<NiGeometry*>(Geometry)->GetWorldBound();
				const ShadowsExteriorEffect* shadows = TheShaderManager->Effects.ShadowsExteriors;
				const NiAVObject* parent = (const NiAVObject*)Geometry->m_parent;
				unsigned lodForms = 0;
				for (int c = 0; c < 4 && shadows; c++) if (shadows->ShadowMaps[c].Forms.Lod) lodForms |= 1u << c;
				bool inLoaded = false;
				if (lod && b) {
					const NiPoint4* range = BSShaderManager::kLoadedRange;
					inLoaded = fabsf(b->Center.x - range->x) < range->z && fabsf(b->Center.y - range->y) < range->w;
				}
				Logger::Log("CASTER VIEW %s '%s' (in '%s') dist %.0f radius %.1f cascade %d MinRadius %.1f fade %.2f%s lodForms %X%s", kName[Current],
					Geometry->m_pcName ? Geometry->m_pcName : "", parent && parent->m_pcName ? parent->m_pcName : "", distance, b ? b->Radius : -1.0f,
					cascade, cascade >= 0 && shadows ? shadows->ShadowMaps[cascade].Forms.MinRadius : -1.0f, fade, lod ? " LOD" : "", lodForms,
					lod ? (inLoaded ? " in-loaded-cells" : " outside-loaded-cells") : "");
			}
		}
	}

	inline void OnBind(IDirect3DDevice9* device) { if (Mode && InPass) Apply(device); }

	inline void After(IDirect3DDevice9* device) {
		InPass = false;
		if (!Replaced) return;
		Replaced = false;
		device->SetPixelShader(TheRenderManager->renderState->GetPixelShader());
	}
}
#endif
