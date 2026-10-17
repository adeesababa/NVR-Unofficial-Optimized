#pragma once

// UNOFFICIAL 2026-10-16 cloth wind ([Shaders.Cloth.Main]; shaders: Includes/ClothWind.hlsl, ObjectTemplate.hlsl, ShadowMap.vso).
//
// Capes, dusters, flags, banners and curtains move in the wind, done entirely in the vertex shader by maths (a travelling smooth
// triangle wave for the billow, a slow gust, a two-wave ripple at the flutter speed), weighted per vertex by the vertex colour's
// RED channel (1 = loose ends, 0 = pinned) or the whole mesh (MaskMode 1). No physics, nothing per vertex on the CPU.
// Which objects: those whose diffuse texture path contains one of the Keywords (cached per texture). Per draw this sets the
// vertex shader constants c248-c251 (strength 0 for everything else, so other objects run no wind code at all).
// The wind: the settings' direction and speed, or the game's own (the sky's wind speed and direction, UseGameWind).
// Phases are worked out here in double precision and wrapped, so the motion never jitters after long play.
// 2026-10-17 (cloth round 2): the phases are INTEGRATED from the game's clock (a pause or menu stops them, the time scale does not
// change the speed) instead of being a speed times the hour of the day: that clock was in game seconds / 1000, i.e. 30x too slow in
// play (the billow crept at 6 units/s, the 3 Hz flutter at 0.09 Hz), it jumped back at midnight, and every change of the weather's
// wind speed made the phase jump (time x new speed). Test knob TimeOffset (seconds) shifts the pattern for time-step pictures.
// The vertex shader works in the object's space for static meshes (the wind is turned into it with the object's rotation and
// scale); for skinned ones in the skin root's space in the game's passes and in world space in NVR's shadow pass (its own bones).
// SAFETY: every pass of an object must move it the same way (lamp passes test depth EQUAL). Some passes use the game's own
// (vanilla) vertex shaders, which do not move it -- the texture pass of multi-pass objects then misses and the cloth turns white.
// So an object gets cloth only if, last frame, every one of its passes had an NVR vertex shader (SetShadersHook notes them).
// Interiors: no wind, so no cloth.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ClothWind {

	struct Settings {
		bool  CompiledIn = false;		// Enabled at startup (CLOTH_WIND_ON in the shaders)
		bool  On = false;				// Enabled, live
		int   MaskMode = 2;				// 0 vertex colour red, 1 whole mesh, 2 by height (pinned at the top)
		float HeightTop = 95.0f, HeightBottom = 25.0f;	// mask 2 on worn clothing: units above the actor's root (waist .. near the ground)
		bool  UseGameWind = true;
		float Dir[3] = { 1.0f, 0.0f, 0.0f };
		float Speed = 220.0f;			// units per second along the wind (the billow's travel)
		float Intensity = 6.0f;			// units (about 9 cm) of movement at full mask
		float Wavelength = 260.0f;		// units
		float Flutter = 3.0f;			// flutter cycles per second
		float FlutterAmount = 0.35f;
		float Ripple = 0.5f;
		float RippleScale = 0.012f;		// ripple cycles per unit
		float TimeOffset = 0.0f;		// test only: seconds the pattern is ahead
		bool  HeightFromFeet = true;	// mask 2 on worn clothing: heights from the actor's feet (the node the mesh hangs from), not the skin root
		std::string Keywords = "cape,cloak,duster,flag,banner,curtain,drape,tarp,poncho,robe,scarf,tent";
	};

	inline Settings S;
	inline std::vector<std::string> Words;
	inline std::string WordsFrom;
	inline std::unordered_map<const void*, std::pair<std::string, bool>> ByTexture;
	inline float WorldDir[3] = { 1.0f, 0.0f, 0.0f };
	inline float Phase[4] = {};			// main wave, gust, flutter 1, flutter 2 (cycles, wrapped)
	inline double Cycles[4] = {};		// the same, integrated, plus the rate each runs at now (cycles per second)
	inline double Rate[4] = {};
	inline double ClockSeconds = 0.0;	// seconds of play at the normal time scale (stops in a pause, not scaled by the time scale)
	inline float LastHour = -1.0f;
	inline double RealStart = -1.0, ClockStart = 0.0;
	inline float SpeedNow = 220.0f, IntensityNow = 6.0f;
	inline bool NonZeroSent = false;	// the device's c248 last held a cloth draw's values
	inline UInt32 ClothDraws = 0, Frames = 0;
	inline bool LoggedGameWind = false;
	inline bool CompiledInRead = false;
	inline bool Exterior = true;
	inline std::unordered_set<const void*> SeenNow, SeenLast, VanillaNow, VanillaLast;	// objects drawn / with a vanilla vertex shader
	// The vertex shaders that carry the cloth code: NVR's ObjectTemplate ones (PBR.h's table), by name, built once.
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
	// SetShadersHook, for every pass: did this object's pass use a vertex shader that carries the cloth code?
	inline void NoteShaders(const void* geometry, bool nvrVertexShader) {
		if (!S.On || !geometry) return;
		SeenNow.insert(geometry);
		if (!nvrVertexShader) VanillaNow.insert(geometry);
	}

	inline bool IsClothTexture(const char* path) {
		if (!path || !path[0]) return false;
		std::string p(path);
		for (char& c : p) c = (char)tolower((unsigned char)c);
		// A keyword counts only as a whole word of the path (a plural s allowed): "flag" finds flags but not flagstone.
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

	// Once a frame (Render.cpp, image space): the settings (live), the wind and the phases.
	inline void AdvanceClock() {
		TimeGlobals* g = TimeGlobals::Get();
		if (!g || !g->GameHour) return;
		const float hour = g->GameHour->data;
		const double scale = g->TimeScale && g->TimeScale->data > 0.0f ? (double)g->TimeScale->data : 30.0;
		if (LastHour >= 0.0f) {
			double dh = (double)hour - (double)LastHour;
			if (dh < -12.0) dh += 24.0;		// past midnight
			if (dh > 0.0) ClockSeconds += (std::min)(dh * 3600.0 / scale, 0.1);	// a wait or sleep moves on by at most a frame's worth
		}
		LastHour = hour;
	}

	inline void BeginFrame(const Sky* sky, bool exterior) {
		Exterior = exterior || TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Interiors") != 0;	// (named Exterior: 'cloth may move here')
		SeenLast.swap(SeenNow); SeenNow.clear(); VanillaLast.swap(VanillaNow); VanillaNow.clear();
		// The shaders were compiled with Enabled as it was at startup (CLOTH_WIND_ON, ShaderRecord::BuildDefines): read once.
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
		// (the whole text: GetSettingS goes through the node's 80-byte Value, which a longer list overflowed -- the game crashed)
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
			// The sky's wind (weather): direction as an angle around up, speed 0-1. Calm weather still moves cloth a little.
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
		// The clock runs only while cloth is on (BeginFrame returns above when off); the phases are integrated.
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

	// Before an object is drawn (the game's passes and NVR's shadow passes): its cloth values, or strength 0.
	inline void ForGeometry(IDirect3DDevice9* device, NiGeometry* geometry, bool shadowPass = false) {
		if (!device) return;
		if (!S.On || !Exterior || !geometry) { Zero(device); return; }
		if (!shadowPass && (!SeenLast.count(geometry) || VanillaLast.count(geometry))) { Zero(device); return; }	// see SAFETY above
		if (shadowPass && VanillaLast.count(geometry)) { Zero(device); return; }
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) { Zero(device); return; }	// BSShaderPPLightingProperty, Lighting30ShaderProperty
		BSShaderPPLightingProperty* lighting = (BSShaderPPLightingProperty*)property;
		const NiSourceTexture* diffuse = lighting->ppTextures[0] ? lighting->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) { Zero(device); return; }
		bool cloth;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) cloth = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();	// 2026-10-17: entries are per texture object and were never dropped; the answer is worked out again on demand (same result)
			cloth = IsClothTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), cloth }; if (cloth) Logger::Log("Cloth wind: %s", diffuse->ddsPath1);
		}
		// The vertex colour is the mask: without colours in the mesh, only the whole-mesh mode applies.
		if (!cloth || (S.MaskMode == 0 && (!geometry->geomData || !geometry->geomData->m_pkColor))) { Zero(device); return; }
		// Into the vertex shader's space: world for skinned meshes, the object's own for the rest.
		float dir[3] = { WorldDir[0], WorldDir[1], WorldDir[2] };
		float scale = 1.0f;
		float top = 0.0f, invRange = 0.0f;	// mask 2, in the shader's space
		if (!geometry->skinInstance) {
			const NiTransform& w = geometry->m_worldTransform;
			scale = w.scale > 1e-4f ? w.scale : 1.0f;
			for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			if (geometry->geomData) {	// hanging cloth: pinned at the top of its bound, loosest at the bottom (object space)
				const NiBound& b = geometry->geomData->m_kBound;
				top = b.Center.z + 0.8f * b.Radius; invRange = b.Radius > 1e-3f ? 1.0f / (1.6f * b.Radius) : 0.0f;
			}
		}
		else {	// worn clothing: from the actor's waist down to near the ground
			const NiAVObject* root = geometry->skinInstance->RootParent ? geometry->skinInstance->RootParent : geometry;
			const float range = S.HeightTop > S.HeightBottom ? S.HeightTop - S.HeightBottom : 1.0f;
			// 2026-10-17 (round 2, the Ranger Patrol test: shoulders moved as much as the hem, only the head stood still): the skin root
			// is the skeleton's own root (Bip01, at the hips), NOT the actor's feet, so 'units above the root' put HeightTop 95 units above
			// the hips. The feet are the origin of the node the mesh hangs from (the geometry's own world transform: its local transform
			// is the identity); the root's height above them is taken off. A reading outside 0..140 units (not an actor-like rig) falls
			// back to the old way. UNVERIFIED in game: the first run with it logs "Cloth wind: skinned ...: root N above the feet".
			float rootAbove = 0.0f;
			if (S.HeightFromFeet) {
				const float d = root->m_worldTransform.pos.z - geometry->m_worldTransform.pos.z;
				if (d > -20.0f && d < 140.0f) rootAbove = d;
				static int told = 0;
				if (told < 6) { told++; Logger::Log("Cloth wind: skinned %s: skin root %.1f above the mesh node's origin (%s)", diffuse->ddsPath1, d, rootAbove != 0.0f || d == 0.0f ? "used as the feet" : "out of range: root taken as the feet"); }
			}
			if (shadowPass) {	// NVR's shadow pass skins into world space
				top = root->m_worldTransform.pos.z - rootAbove + S.HeightTop;
				invRange = 1.0f / range;
			}
			else {	// the game's passes: the skin root's space (z up; the feet are rootAbove below the root's origin)
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
