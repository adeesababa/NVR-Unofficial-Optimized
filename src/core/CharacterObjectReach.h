#pragma once

#include <unordered_map>

namespace CharacterObjectReach {
	inline bool Checked = false, Compiled = false, On = false;
	inline float Sent = -1.0f;
	inline unsigned Frame = 0, Logged = 0;
	inline bool LogDraws = false;
	inline std::unordered_map<const void*, bool> Verdicts;

	inline bool Contains(const char* name, const char* part) {
		if (!name) return false;
		const size_t n = strlen(part);
		for (const char* p = name; *p; p++) if (!_strnicmp(p, part, n)) return true;
		return false;
	}

	inline bool HeadBoneName(const char* name) {
		static const char* const parts[] = { "head", "neck", "hair", "ponytail", "jaw", "eye", "brow", "lip", "tongue", "hat" };
		for (const char* part : parts) if (Contains(name, part)) return true;
		return false;
	}

	inline bool Classify(const NiGeometry* geometry, const char*& why) {
		const NiSkinInstance* si = geometry->skinInstance;
		const NiSkinData* sd = si ? si->SkinData : nullptr;
		if (si && sd && si->BoneObjects) {
			const UInt32 count = (std::min)(sd->Bones, (UInt32)512);
			bool head = false;
			for (UInt32 b = 0; b < count; b++) {
				const NiAVObject* bone = si->BoneObjects[b];
				const char* name = bone ? bone->m_pcName : nullptr;
				if (!name) continue;
				if (!HeadBoneName(name)) { why = name; return false; }
				if (Contains(name, "head")) head = true;
			}
			why = head ? "skinned to head bones only" : "no head bone";
			return head;
		}
		const NiAVObject* node = geometry->m_parent;
		for (int depth = 0; node && depth < 8; depth++, node = node->m_parent) {
			const char* name = node->m_pcName;
			if (name && (!_stricmp(name, "Bip01 Head") || !_stricmp(name, "HeadAnims"))) { why = name; return true; }
		}
		why = "not under the head";
		return false;
	}

	inline void BeginFrame() {
		if (!Checked) {
			Checked = true;
			Compiled = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "CharacterObjectReach") != 0;
			Logger::Log("UNOFFICIAL CharacterObjectReach: %s", Compiled ? "on (actors' head, hair, face mask and headgear object draws take CharacterShadowReach's slack cap)" :
				"off (not compiled into the shaders)");
		}
		On = Compiled && TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "CharacterObjectReach") != 0;
		LogDraws = Compiled && TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "CharacterObjectReachLog") != 0;
		Sent = -1.0f;
		if (++Frame % 1800 == 0 || Verdicts.size() > 8192) Verdicts.clear();
	}

	inline void ForDraw(IDirect3DDevice9* device, const NiGeometry* geometry, bool world) {
		if (!Compiled) return;
		float flag = 0.0f;
		if (On && world && geometry) {
			auto it = Verdicts.find(geometry);
			if (it == Verdicts.end()) {
				const char* why = "";
				const bool head = Classify(geometry, why);
				it = Verdicts.emplace(geometry, head).first;
				if (LogDraws && Logged < 400) {
					Logged++;
					Logger::Log("CharacterObjectReach: '%s' %s (%s)", geometry->m_pcName ? geometry->m_pcName : "?", head ? "HEAD" : "no", why ? why : "");
				}
			}
			flag = it->second ? 1.0f : 0.0f;
		}
		if (flag == Sent) return;
		Sent = flag;
		const float value[4] = { flag, 0.0f, 0.0f, 0.0f };
		device->SetPixelShaderConstantF(204, value, 1);
	}
}
