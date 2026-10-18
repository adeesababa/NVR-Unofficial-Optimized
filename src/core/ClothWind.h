#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ClothWind {

	struct Settings {
		bool  CompiledIn = false;
		bool  On = false;
		int   MaskMode = 2;
		float HeightTop = 95.0f, HeightBottom = 25.0f;
		bool  UseGameWind = true;
		float Dir[3] = { 1.0f, 0.0f, 0.0f };
		float Speed = 220.0f;
		float Intensity = 6.0f;
		float Wavelength = 260.0f;
		float Flutter = 3.0f;
		float FlutterAmount = 0.35f;
		float Ripple = 0.5f;
		float RippleScale = 0.012f;
		float TimeOffset = 0.0f;
		bool  HeightFromFeet = true;
		std::string Keywords = "cape,cloak,duster,flag,banner,curtain,drape,tarp,poncho,robe,scarf,tent";
	};

	inline Settings S;
	inline std::vector<std::string> Words;
	inline std::string WordsFrom;
	inline std::unordered_map<const void*, std::pair<std::string, bool>> ByTexture;
	inline float WorldDir[3] = { 1.0f, 0.0f, 0.0f };
	inline float Phase[4] = {};
	inline double Cycles[4] = {};
	inline double Rate[4] = {};
	inline double ClockSeconds = 0.0;
	inline float LastHour = -1.0f;
	inline double RealStart = -1.0, ClockStart = 0.0;
	inline float SpeedNow = 220.0f, IntensityNow = 6.0f;
	inline bool NonZeroSent = false;
	inline UInt32 ClothDraws = 0, Frames = 0;
	inline bool LoggedGameWind = false;
	inline bool CompiledInRead = false;
	inline bool Exterior = true;
	inline std::unordered_set<const void*> SeenNow, SeenLast, VanillaNow, VanillaLast;
	inline std::unordered_set<std::string> CapableNames;
	inline bool CapableBuilt = false;
	inline bool CapableName(const char* name) {
		if (!CapableBuilt && TheShaderManager && TheShaderManager->Shaders.PBR) {
			CapableBuilt = true;
			for (const auto& [n, t] : TheShaderManager->Shaders.PBR->Templates())
				if (t.Name && !strcmp(t.Name, "ObjectTemplate") && n.size() > 4 && n.substr(n.size() - 4) == ".vso") CapableNames.insert(std::string(n));
			Logger::Log("Cloth wind: %u vertex shaders carry it", (unsigned)CapableNames.size());
		}
		return name && CapableNames.count(name) != 0;
	}
	inline void NoteShaders(const void* geometry, bool nvrVertexShader) {
		if (!S.On || !geometry) return;
		SeenNow.insert(geometry);
		if (!nvrVertexShader) VanillaNow.insert(geometry);
	}

	inline bool IsClothTexture(const char* path) {
		if (!path || !path[0]) return false;
		std::string p(path);
		for (char& c : p) c = (char)tolower((unsigned char)c);
		auto letter = [&](size_t i) { return i < p.size() && p[i] >= 'a' && p[i] <= 'z'; };
		for (const std::string& w : Words) {
			if (w.empty()) continue;
			for (size_t at = p.find(w); at != std::string::npos; at = p.find(w, at + 1)) {
				size_t end = at + w.size();
				if (end < p.size() && p[end] == 's' && !letter(end + 1)) end++;
				if ((at == 0 || !letter(at - 1)) && !letter(end)) return true;
			}
		}
		return false;
	}

	inline void AdvanceClock() {
		TimeGlobals* g = TimeGlobals::Get();
		if (!g || !g->GameHour) return;
		const float hour = g->GameHour->data;
		const double scale = g->TimeScale && g->TimeScale->data > 0.0f ? (double)g->TimeScale->data : 30.0;
		if (LastHour >= 0.0f) {
			double dh = (double)hour - (double)LastHour;
			if (dh < -12.0) dh += 24.0;
			if (dh > 0.0) ClockSeconds += (std::min)(dh * 3600.0 / scale, 0.1);
		}
		LastHour = hour;
	}

	inline void BeginFrame(const Sky* sky, bool exterior) {
		Exterior = exterior || TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Interiors") != 0;
		SeenLast.swap(SeenNow); SeenNow.clear(); VanillaLast.swap(VanillaNow); VanillaNow.clear();
		if (!CompiledInRead) { CompiledInRead = true; S.CompiledIn = TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Enabled") != 0; }
		S.On = S.CompiledIn && TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Enabled");
		if (!S.On) return;
		const char* section = "Shaders.Cloth.Main";
		S.MaskMode = std::clamp(TheSettingManager->GetSettingI(section, "MaskMode"), 0, 2);
		S.HeightTop = TheSettingManager->GetSettingF(section, "HeightTop");
		S.HeightBottom = TheSettingManager->GetSettingF(section, "HeightBottom");
		S.HeightFromFeet = TheSettingManager->GetSettingI(section, "HeightFromFeet") != 0;
		S.UseGameWind = TheSettingManager->GetSettingI(section, "UseGameWind") != 0;
		S.Dir[0] = TheSettingManager->GetSettingF(section, "WindDirX"); S.Dir[1] = TheSettingManager->GetSettingF(section, "WindDirY");
		S.Dir[2] = TheSettingManager->GetSettingF(section, "WindDirZ");
		S.Speed = (std::max)(TheSettingManager->GetSettingF(section, "WindSpeed"), 0.0f);
		S.Intensity = (std::max)(TheSettingManager->GetSettingF(section, "WindIntensity"), 0.0f);
		S.Wavelength = (std::max)(TheSettingManager->GetSettingF(section, "Wavelength"), 16.0f);
		S.Flutter = (std::max)(TheSettingManager->GetSettingF(section, "FlutterSpeed"), 0.0f);
		S.FlutterAmount = std::clamp(TheSettingManager->GetSettingF(section, "FlutterAmount"), 0.0f, 1.0f);
		S.Ripple = std::clamp(TheSettingManager->GetSettingF(section, "RippleAmount"), 0.0f, 1.0f);
		S.RippleScale = (std::max)(TheSettingManager->GetSettingF(section, "RippleScale"), 0.0f);
		SettingManager::Configuration::ConfigNode wordsNode;
		const char* words = TheSettingManager->Config.FillNode(&wordsNode, section, "Keywords") ? wordsNode.LongValue.c_str() : nullptr;
		if (words && WordsFrom != words) {
			WordsFrom = words; Words.clear(); ByTexture.clear();
			std::string w;
			for (const char* c = words; ; c++) {
				if (!*c || *c == ',') { while (!w.empty() && w.back() == ' ') w.pop_back(); if (!w.empty()) Words.push_back(w); w.clear(); if (!*c) break; }
				else if (!(w.empty() && *c == ' ')) w += (char)tolower((unsigned char)*c);
			}
		}
		float d[3] = { S.Dir[0], S.Dir[1], S.Dir[2] };
		float speed = S.Speed, intensity = S.Intensity;
		if (S.UseGameWind && sky) {
			const float a = sky->windDirection;
			d[0] = sinf(a); d[1] = cosf(a); d[2] = 0.0f;
			const float w = std::clamp(sky->windSpeed, 0.0f, 1.0f);
			speed *= 0.4f + 1.2f * w;
			intensity *= 0.3f + 1.4f * w;
			if (!LoggedGameWind) { LoggedGameWind = true; Logger::Log("Cloth wind: the game's wind, direction %.3f, speed %.3f", a, sky->windSpeed); }
		}
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		for (int i = 0; i < 3; i++) WorldDir[i] = len > 1e-4f ? d[i] / len : (i == 0 ? 1.0f : 0.0f);
		SpeedNow = speed; IntensityNow = intensity;
		const double before = ClockSeconds;
		AdvanceClock();
		const double dt = ClockSeconds - before;
		S.TimeOffset = TheSettingManager->GetSettingF(section, "TimeOffset");
		Rate[0] = speed / S.Wavelength; Rate[1] = 0.061 * (0.5 + speed / 300.0); Rate[2] = S.Flutter; Rate[3] = S.Flutter * 1.31;
		for (int i = 0; i < 4; i++) {
			Cycles[i] += dt * Rate[i]; Cycles[i] -= floor(Cycles[i]);
			const double c = Cycles[i] + (double)S.TimeOffset * Rate[i];
			Phase[i] = (float)(c - floor(c));
		}
		if (RealStart < 0.0 && TheFrameRateManager) { RealStart = TheFrameRateManager->Time; ClockStart = ClockSeconds; }
		if (++Frames >= 600) {
			const double real = TheFrameRateManager ? TheFrameRateManager->Time - RealStart : 0.0;
			Logger::Log("Cloth wind: %.1f cloth draws per frame; clock %.2f s of wind per second of real time (1 = normal play, 0 = paused)", ClothDraws / (float)Frames, real > 0.0 ? (ClockSeconds - ClockStart) / real : 0.0);
			RealStart = TheFrameRateManager ? TheFrameRateManager->Time : -1.0; ClockStart = ClockSeconds;
			Frames = ClothDraws = 0;
		}
	}

	inline void Zero(IDirect3DDevice9* device) {
		if (!NonZeroSent) return;
		static const float zero[4] = { 0, 0, 0, 0 };
		device->SetVertexShaderConstantF(248, zero, 1);
		NonZeroSent = false;
	}

	inline void ForGeometry(IDirect3DDevice9* device, NiGeometry* geometry, bool shadowPass = false) {
		if (!device) return;
		if (!S.On || !Exterior || !geometry) { Zero(device); return; }
		if (!shadowPass && (!SeenLast.count(geometry) || VanillaLast.count(geometry))) { Zero(device); return; }
		if (shadowPass && VanillaLast.count(geometry)) { Zero(device); return; }
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) { Zero(device); return; }
		BSShaderPPLightingProperty* lighting = (BSShaderPPLightingProperty*)property;
		const NiSourceTexture* diffuse = lighting->ppTextures[0] ? lighting->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) { Zero(device); return; }
		bool cloth;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) cloth = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();
			cloth = IsClothTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), cloth }; if (cloth) Logger::Log("Cloth wind: %s", diffuse->ddsPath1);
		}
		if (!cloth || (S.MaskMode == 0 && (!geometry->geomData || !geometry->geomData->m_pkColor))) { Zero(device); return; }
		float dir[3] = { WorldDir[0], WorldDir[1], WorldDir[2] };
		float scale = 1.0f;
		float top = 0.0f, invRange = 0.0f;
		if (!geometry->skinInstance) {
			const NiTransform& w = geometry->m_worldTransform;
			scale = w.scale > 1e-4f ? w.scale : 1.0f;
			for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			if (geometry->geomData) {
				const NiBound& b = geometry->geomData->m_kBound;
				top = b.Center.z + 0.8f * b.Radius; invRange = b.Radius > 1e-3f ? 1.0f / (1.6f * b.Radius) : 0.0f;
			}
		}
		else {
			const NiAVObject* root = geometry->skinInstance->RootParent ? geometry->skinInstance->RootParent : geometry;
			const float range = S.HeightTop > S.HeightBottom ? S.HeightTop - S.HeightBottom : 1.0f;
			float rootAbove = 0.0f;
			if (S.HeightFromFeet) {
				const float d = root->m_worldTransform.pos.z - geometry->m_worldTransform.pos.z;
				if (d > -20.0f && d < 140.0f) rootAbove = d;
				static int told = 0;
				if (told < 6) { told++; Logger::Log("Cloth wind: skinned %s: skin root %.1f above the mesh node's origin (%s)", diffuse->ddsPath1, d, rootAbove != 0.0f || d == 0.0f ? "used as the feet" : "out of range: root taken as the feet"); }
			}
			if (shadowPass) {
				top = root->m_worldTransform.pos.z - rootAbove + S.HeightTop;
				invRange = 1.0f / range;
			}
			else {
				const NiTransform& w = root->m_worldTransform;
				scale = w.scale > 1e-4f ? w.scale : 1.0f;
				for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
				top = (S.HeightTop - rootAbove) / scale;
				invRange = scale / range;
			}
		}
		const float c[16] = {
			1.0f, (float)S.MaskMode, S.Ripple, S.FlutterAmount,
			dir[0], dir[1], dir[2], scale / S.Wavelength,
			Phase[0], Phase[1], Phase[2], Phase[3],
			IntensityNow / scale, S.RippleScale * scale, top, invRange };
		device->SetVertexShaderConstantF(248, c, 4);
		NonZeroSent = true;
		ClothDraws++;
	}
}
