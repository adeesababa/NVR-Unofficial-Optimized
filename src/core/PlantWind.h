#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include "ClothWind.h"
#include "GrassWind.h"

namespace PlantWind {

	struct SettingsStruct {
		bool	Enabled = false;
		float	Bend = 6.0f;
		float	StiffHeight = 200.0f;
		float	Flutter = 0.25f;
		float	LeafGust = 1.5f;
		float	LeafFlutter = 0.12f;
		float	LeafLean = 0.0f;
		float	LeafHeight = 120.0f;
		int		Log = 0;
	};
	inline SettingsStruct S;
	inline bool CompiledIn = false, CompiledInRead = false;
	inline bool On = false;
	inline bool StaticsOn = false;
	inline std::vector<std::string> Words, Excluded;
	inline std::string WordsFrom, ExcludeFrom;
	inline std::unordered_map<const void*, std::pair<std::string, bool>> ByTexture;
	inline float Strength = 1.0f;
	inline float GrassMin = 5.0f, GrassMax = 125.0f;
	inline double ClockNow = 0.0;
	inline float K[2] = {}, Dir[2] = { 0.0f, 1.0f };
	inline D3DXVECTOR4 LeafA = { 0, 0, 0, 0 }, LeafB = { 0, 0, 0, 0 }, LeafC = { 0, 0, 0, 0 };
	inline UInt32 Frames = 0, DrawsLit = 0, DrawsShadow = 0, SkipSafety = 0, SkipShadowVanilla = 0, SkipNoRoot = 0;
	inline UInt32 MatchesLogged = 0;

	static const float kFlutterWave[3] = { 0.031f, -0.023f, 0.019f };
	static const double kTwoPi = 6.283185307179586;

	inline double Wrap(double a) { a = fmod(a, kTwoPi); return a < 0.0 ? a + kTwoPi : a; }

	inline void RegisterConstants() {
		TheShaderManager->RegisterConstant("TESR_PlantLeafWind", &LeafA);
		TheShaderManager->RegisterConstant("TESR_PlantLeafWind2", &LeafB);
		TheShaderManager->RegisterConstant("TESR_PlantLeafWind3", &LeafC);
	}

	inline void SplitList(const char* text, std::vector<std::string>& out) { ClothWind::SplitList(text, out); }
	inline bool IsPlantTexture(const char* path) {
		if (!path || !path[0]) return false;
		const std::string p = ClothWind::Norm(path);
		bool hit = false;
		for (const std::string& w : Words) if (!w.empty() && p.find(w) != std::string::npos) { hit = true; break; }
		if (!hit) return false;
		for (const std::string& x : Excluded) if (!x.empty() && p.find(x) != std::string::npos) return false;
		return true;
	}

	inline void BeginFrame(const Sky* sky, bool exterior) {
		const char* section = "Shaders.Plants.Wind";
		if (!CompiledInRead) { CompiledInRead = true; CompiledIn = TheSettingManager->GetSettingI(section, "Enabled") != 0; ClothWind::PlantsCompiledIn = CompiledIn; }
		S.Enabled = TheSettingManager->GetSettingI(section, "Enabled") != 0;
		On = S.Enabled && exterior;
		StaticsOn = On && CompiledIn;
		ClothWind::PlantsOn = StaticsOn;
		GrassWind::ClockWanted = S.Enabled;
		if (!S.Enabled) { LeafA = LeafB = LeafC = D3DXVECTOR4(0, 0, 0, 0); return; }
		S.Bend = std::clamp(TheSettingManager->GetSettingF(section, "Bend"), 0.0f, 100.0f);
		S.StiffHeight = std::clamp(TheSettingManager->GetSettingF(section, "StiffHeight"), 20.0f, 5000.0f);
		S.Flutter = std::clamp(TheSettingManager->GetSettingF(section, "Flutter"), 0.0f, 4.0f);
		S.LeafGust = std::clamp(TheSettingManager->GetSettingF(section, "LeafGust"), 0.0f, 10.0f);
		S.LeafFlutter = std::clamp(TheSettingManager->GetSettingF(section, "LeafFlutter"), 0.0f, 1.0f);
		S.LeafLean = std::clamp(TheSettingManager->GetSettingF(section, "LeafLean"), 0.0f, 50.0f);
		S.LeafHeight = std::clamp(TheSettingManager->GetSettingF(section, "LeafHeight"), 1.0f, 5000.0f);
		S.Log = TheSettingManager->GetSettingI(section, "Log");
		auto readList = [&](const char* key, std::string& from, std::vector<std::string>& out) {
			SettingManager::Configuration::ConfigNode node;
			if (!TheSettingManager->Config.FillNode(&node, section, key)) return;
			const std::string text = node.LongValue;
			if (from != text) { from = text; SplitList(text.c_str(), out); ByTexture.clear(); }
		};
		readList("Keywords", WordsFrom, Words);
		readList("Exclude", ExcludeFrom, Excluded);

		GrassWind::ReadSettings();
		const SandstormEffect* Sandstorm = TheShaderManager->Effects.Sandstorm;
		const float storm = Sandstorm && Sandstorm->Enabled && Sandstorm->GrassStorm[0] > 0.0f ? Sandstorm->Intensity : 0.0f;
		GrassWind::Update(sky, exterior, storm);
		if (!On) { LeafA = LeafB = LeafC = D3DXVECTOR4(0, 0, 0, 0); return; }
		ClockNow = (double)GrassWind::ClockNow;
		Dir[0] = GrassWind::WindDir[0]; Dir[1] = GrassWind::WindDir[1];
		const double k = 2.0 * 3.14159265358979323846 / GrassWind::S.GustSize;
		K[0] = (float)(Dir[0] * k); K[1] = (float)(Dir[1] * k);
		const float mn = *Pointers::Settings::GrassWindMagnitudeMin, mx = *Pointers::Settings::GrassWindMagnitudeMax;
		if (mn >= 0.0f && mx >= mn && mx > 0.0f && mx < 5000.0f) { GrassMin = mn; GrassMax = mx; }
		const float mid = GrassMin + 0.5f * (GrassMax - GrassMin);
		Strength = mid > 1e-3f ? (GrassMin + GrassWind::WindSpeed * (GrassMax - GrassMin)) / mid : 1.0f;
		LeafA = D3DXVECTOR4(K[0], K[1], (float)ClockNow, 1.0f);
		LeafB = D3DXVECTOR4(S.LeafFlutter * Strength, S.LeafLean * Strength, 1.0f / S.LeafHeight, S.LeafGust);
		LeafC = D3DXVECTOR4(Dir[0], Dir[1], 0.0f, 0.0f);
		if (S.Log && ++Frames >= 600) {
			const float* c4 = (const float*)0x01200550;
			Logger::Log("UNOFFICIAL Plant wind: %s, statics %s; wind direction (%.3f %.3f) speed %.3f -> strength %.3f (game grass wind min %.1f max %.1f, "
				"its c4 now %.3f %.3f size %.2f); clock %.3f (the grass wind's, rate %.3f rad/s); per frame: static plant draws %.1f lit, %.1f shadow, skipped "
				"%.1f (a pass without NVR's vertex shader) + %.1f (shadow, same) + %.1f (no root node); leaves: gust x%.2f, flutter %.3f rad, lean %.2f",
				On ? "on" : "off", StaticsOn ? "on" : (CompiledIn ? "off (indoors)" : "NOT compiled in (needs a restart)"), Dir[0], Dir[1], GrassWind::WindSpeed, Strength,
				GrassMin, GrassMax, c4[0], c4[1], c4[2], ClockNow, GrassWind::Rate, DrawsLit / (float)Frames, DrawsShadow / (float)Frames, SkipSafety / (float)Frames,
				SkipShadowVanilla / (float)Frames, SkipNoRoot / (float)Frames, LeafB.w, LeafB.x, LeafB.y);
			Frames = DrawsLit = DrawsShadow = SkipSafety = SkipShadowVanilla = SkipNoRoot = 0;
		}
	}

	inline const NiAVObject* RootOf(const NiAVObject* geometry) {
		const NiAVObject* o = geometry;
		for (int i = 0; i < 8 && o; i++) {
			if (*(const UInt32*)o == 0x010A8F90) return o;
			o = o->m_parent;
		}
		return nullptr;
	}

	inline void ForGeometry(IDirect3DDevice9* device, NiGeometry* geometry, bool shadowPass = false, bool firstPerson = false) {
		if (!StaticsOn || !device || !geometry || firstPerson || ClothWind::NonZeroSent || geometry->skinInstance) return;
		bool boundAnswer = false;
		if (!shadowPass) {
			auto t = ClothWind::TentativeNow.find(geometry);
			if (t == ClothWind::TentativeNow.end()) ClothWind::TentativeNow.emplace(geometry, ClothWind::CurrentVSCapable);
			else t->second = t->second && ClothWind::CurrentVSCapable;
			boundAnswer = !ClothWind::SeenLast.count(geometry) && ClothWind::CurrentVSCapable && !ClothWind::VanillaLast.count(geometry);
		}
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) return;
		const NiSourceTexture* diffuse = ((BSShaderPPLightingProperty*)property)->ppTextures[0] ? ((BSShaderPPLightingProperty*)property)->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) return;
		bool plant;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) plant = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();
			plant = IsPlantTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), plant };
			if (plant && (MatchesLogged < 32 || S.Log)) { MatchesLogged++; Logger::Log("UNOFFICIAL Plant wind: PLANT %s ('%s')", diffuse->ddsPath1, geometry->m_pcName ? geometry->m_pcName : ""); }
		}
		if (!plant) return;
		if (!shadowPass && !boundAnswer && (!ClothWind::SeenLast.count(geometry) || ClothWind::VanillaLast.count(geometry))) { SkipSafety++; return; }
		if (shadowPass && ClothWind::VanillaLast.count(geometry)) { SkipShadowVanilla++; return; }
		const NiAVObject* root = RootOf(geometry);
		if (!root || !root->m_kWorldBound) { SkipNoRoot++; return; }

		const NiTransform& w = geometry->m_worldTransform;
		const float s = w.scale > 1e-4f ? w.scale : 1.0f;
		const float (*R)[3] = w.rot.data;
		const double T[3] = { w.pos.x, w.pos.y, w.pos.z };
		const float base = root->m_worldTransform.pos.z;
		const float H = (std::max)(root->m_kWorldBound->Center.z + root->m_kWorldBound->Radius - base, 10.0f);
		const float lean = S.Bend * Strength * (H * 0.01f) / (1.0f + (H / S.StiffHeight) * (H / S.StiffHeight));
		auto toObj = [&](const float* v, float* o, float f) { for (int j = 0; j < 3; j++) o[j] = f * (R[0][j] * v[0] + R[1][j] * v[1] + R[2][j] * v[2]); };
		const float k1[3] = { K[0], K[1], 0.0f };
		const float k2[3] = { 0.5f * (K[0] - K[1]), 0.5f * (K[1] + K[0]), 0.0f };
		const float up[3] = { 0.0f, 0.0f, 1.0f }, dir[3] = { Dir[0], Dir[1], 0.0f };
		float c[24];
		c[0] = -1.0f; c[1] = lean; c[2] = lean * S.Flutter; c[3] = 1.0f / H;
		toObj(k1, &c[4], s);  c[7] = (float)Wrap(ClockNow * 0.25 - (T[0] * K[0] + T[1] * K[1]));
		toObj(k2, &c[8], s);  c[11] = (float)Wrap(ClockNow * 0.25 - (T[0] * k2[0] + T[1] * k2[1]));
		const double rp = sin(root->m_worldTransform.pos.x * 12.9898 + root->m_worldTransform.pos.y * 78.233) * 43758.5453;
		toObj(kFlutterWave, &c[12], s);
		c[15] = (float)Wrap(2.0 * ClockNow + (rp - floor(rp)) * kTwoPi + T[0] * kFlutterWave[0] + T[1] * kFlutterWave[1] + T[2] * kFlutterWave[2]);
		toObj(up, &c[16], s);  c[19] = (float)(T[2] - base);
		toObj(dir, &c[20], 1.0f / s);  c[23] = 0.0f;
		device->SetVertexShaderConstantF(248, c, 6);
		ClothWind::NonZeroSent = true; ClothWind::GrassInC248 = false;
		if (shadowPass) DrawsShadow++; else DrawsLit++;
	}
}
