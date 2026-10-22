#pragma once
#include <d3d9.h>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include "PassSums.h"

namespace ReflMaterial {
	inline int Startup = -1;
	inline int AddStartup = -1;
	inline bool Writing = false;
	inline float Current = -1.0f;
	inline float CurrentRough = -9.0f;
	inline std::vector<std::pair<std::string, float>> Overrides;
	inline std::string OverrideText = "\x01";
	inline unsigned OverrideGeneration = 1;
	inline float DrawRough = -1.0f;
	inline bool StatesSet = false;
	inline DWORD SavedSeparate = FALSE, SavedSrcAlpha = D3DBLEND_ONE, SavedDstAlpha = D3DBLEND_ZERO, SavedOpAlpha = D3DBLEND_ONE;
	inline std::unordered_set<UInt16> Refused = { 0x14A, 0x14B, 0x14C, 0x14D, 0x14E, 0x248, 0x249, 0x24A, 0x24B, 0x24C, 0x24D, 0x24E,
		0x24F, 0x250, 0x251, 0x252, 0x253, 0x254 };
	inline UInt32 FlaggedPasses = 0, Frames = 0;
	inline UInt16 LastFlaggedPass = 0;
	inline bool LastFlagged = false;
	inline DWORD SavedWrite = 0xF;
	inline bool WriteChanged = false;
	inline UInt32 DiagBound = 0, DiagChanged = 0, DiagFlagged = 0, DiagMask[16] = {};

	inline bool CompiledIn() {
		if (Startup < 0)
			Startup = TheSettingManager->GetSettingI("Shaders.Reflections.Main", "MaterialAware") > 0 &&
				TheSettingManager->GetSettingI("Shaders.Reflections.Status", "Enabled") &&
				TheSettingManager->GetSettingI("Shaders.PBR.Rmaos", "Enabled") ? 1 : 0;
		return Startup > 0;
	}

	inline int OccStartup = -1;
	inline bool OccCompiledIn() {
		if (OccStartup < 0) OccStartup = TheSettingManager->GetSettingF("Shaders.Reflections.Main", "SpecularOcclusion") > 0.0f ? 1 : 0;
		return OccStartup > 0;
	}

	inline bool AddCompiledIn() {
		if (AddStartup < 0) AddStartup = TheSettingManager->GetSettingF("Shaders.Reflections.Main", "ReflectAdd") > 0.0f ? 1 : 0;
		return AddStartup > 0;
	}

	inline void Send(IDirect3DDevice9* device, float v) {
		const float rough = v > 0.0f ? DrawRough : -1.0f;
		if (v == Current && rough == CurrentRough) return;
		Current = v; CurrentRough = rough;
		const float c[4] = { v, rough >= 0.0f ? 0.0f : 1.0f, rough >= 0.0f ? rough : 0.0f, 0.0f };
		device->SetPixelShaderConstantF(212, c, 1);
	}

	inline void ReadSettings() {
		SettingManager::Configuration::ConfigNode node;
		std::string text = TheSettingManager->Config.FillNode(&node, "Shaders.Reflections.Main", "RoughnessOverrides") ?
			(node.LongValue.empty() ? std::string(node.Value) : node.LongValue) : std::string();
		if (OverrideText == text) return;
		OverrideText = text;
		Overrides.clear();
		OverrideGeneration++;
		const std::string& s = text;
		size_t start = 0;
		while (start < s.size()) {
			size_t end = s.find(',', start);
			if (end == std::string::npos) end = s.size();
			std::string item = s.substr(start, end - start);
			start = end + 1;
			const size_t colon = item.rfind(':');
			if (colon == std::string::npos) continue;
			std::string key = item.substr(0, colon);
			key.erase(std::remove_if(key.begin(), key.end(), [](char c) { return c == ' ' || c == '\t' || c == '"'; }), key.end());
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			const float r = (float)atof(item.c_str() + colon + 1);
			if (!key.empty()) Overrides.push_back({ key, std::clamp(r, 0.02f, 1.0f) });
		}
		if (!Overrides.empty()) Logger::Log("MaterialAware reflections: %u roughness override(s) from RoughnessOverrides", (unsigned)Overrides.size());
	}

	inline float RoughFor(const std::string& file) {
		if (Overrides.empty()) return -1.0f;
		std::string f(file);
		std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		for (const auto& o : Overrides) if (f.find(o.first) != std::string::npos) {
			Logger::Log("MaterialAware reflections: roughness %.2f for %s (RoughnessOverrides '%s')", o.second, file.c_str(), o.first.c_str());
			return o.second;
		}
		return -1.0f;
	}

	inline void BeginWorld(NiDX9RenderState* state, IDirect3DDevice9* device, bool live) {
		Writing = CompiledIn() && live;
		Current = -1.0f;
		if (!Writing || !state || !device) return;
		ReadSettings();
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
			char masks[160] = ""; size_t used = 0;
			for (int m = 0; m < 16; m++) if (DiagMask[m]) used += snprintf(masks + used, sizeof(masks) - used, " %X:%u", m, DiagMask[m]);
			Logger::Log("MaterialAware reflections diagnostic: %u flagged passes drew, %u shader binds inside them, %u got alpha added; write mask at the draw's end (mask:passes)%s",
				DiagFlagged, DiagBound, DiagChanged, masks);
			DiagBound = DiagChanged = DiagFlagged = 0; memset(DiagMask, 0, sizeof(DiagMask));
			FlaggedPasses = 0; Frames = 0;
		}
	}

	inline void OnShadersBound(IDirect3DDevice9* device) {
		if (!LastFlagged || WriteChanged || !device) return;
		DiagBound++;
		device->GetRenderState(D3DRS_COLORWRITEENABLE, &SavedWrite);
		if (SavedWrite & D3DCOLORWRITEENABLE_ALPHA) return;
		device->SetRenderState(D3DRS_COLORWRITEENABLE, SavedWrite | D3DCOLORWRITEENABLE_ALPHA);
		WriteChanged = true;
		DiagChanged++;
	}

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
		if (flag) { FlaggedPasses++; LastFlagged = true; LastFlaggedPass = e; OnShadersBound(device); }
	}

	inline void AfterPass(IDirect3DDevice9* device) {
		if (!LastFlagged || !device) return;
		LastFlagged = false;
		DiagFlagged++;
		{ DWORD mask = 0; device->GetRenderState(D3DRS_COLORWRITEENABLE, &mask); DiagMask[mask & 15]++; }
		Send(device, 0.0f);
		if (WriteChanged) { device->SetRenderState(D3DRS_COLORWRITEENABLE, SavedWrite); WriteChanged = false; }
		DWORD blend = FALSE;
		device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
		if (!blend) return;
		Refused.insert(LastFlaggedPass);
		Logger::Log("[WARNING] MaterialAware reflections: pass %03X drew blended with the material flag on (its colour may have been wrong for one draw); it is left out from now on", (unsigned)LastFlaggedPass);
	}
}
