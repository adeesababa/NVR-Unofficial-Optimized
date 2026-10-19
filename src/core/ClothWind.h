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
// 2026-10-18 (cloth round 3, 'cloth does nothing'): keywords match as substrings (with an exclusion list), worn clothing is masked by
// the garment's own BIND-POSE height (pinned shoulders, loose hem), the calm-weather strength is higher, and non-cloth draws can no
// longer inherit c248 from the grass shaders (they relocate their shadow matrices to c248..c255). See evidence\cloth3-LOG.md.
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
		bool  HeightFromFeet = true;	// SkinMask 0 only: heights from the actor's feet (the node the mesh hangs from), not the skin root
		int   SkinMask = 1;				// worn clothing: 0 the old actor-height mask (skin root), 1 the garment's own bind-pose height (pinned top part, loose hem)
		float PinFraction = 0.4f;		// SkinMask 1: the top part of the garment (fraction of its height) that does not move at all
		int   SkinSpace = 1;			// worn clothing, what space the bones put vertices in: 0 the skin root's (rotated wind), 1 camera-relative world, 2 world
		float CalmFactor = 0.6f;		// UseGameWind: strength in dead calm (rising to 1.7 in a gale); it was 0.3
		bool  LogMatches = false;		// diagnostic: every skinned texture and every match goes to the log with its mesh numbers
		std::string Keywords = "cape,cloak,duster,flag,banner,curtain,drape,tarp,poncho,robe,scarf,tent";
		std::string Exclude = "scape,probe,microbe,wardrobe,content,intent,extent,patent,potent,latent,flagpole,flagstone,pole,stone";
		std::string ExcludeParts = "hat,helmet,head,hair,glove,boot,shoe,mask,goggle,hand,body,beard,mouth,teeth,eyes";
	};

	inline Settings S;
	inline std::vector<std::string> Words, Excluded, ExcludedParts;
	inline std::string WordsFrom, ExcludeFrom, ExcludePartsFrom;
	struct Extent { uint16_t count = 0; float lo = 0.0f, hi = 0.0f; bool fromVertices = false; };
	inline std::unordered_map<const void*, Extent> Extents;		// bind-pose height range per skinned mesh (geometry data)
	inline UInt32 SkinnedDrawsNow = 0;
	inline std::unordered_map<const void*, std::pair<std::string, bool>> ByTexture;
	inline float WorldDir[3] = { 1.0f, 0.0f, 0.0f };
	inline float Phase[4] = {};			// main wave, gust, flutter 1, flutter 2 (cycles, wrapped)
	inline double Cycles[4] = {};		// the same, integrated, plus the rate each runs at now (cycles per second)
	inline double Rate[4] = {};
	inline double ClockSeconds = 0.0;	// seconds of play at the normal time scale (stops in a pause, not scaled by the time scale)
	inline float LastHour = -1.0f;
	inline double RealStart = -1.0, ClockStart = 0.0;
	inline float SpeedNow = 220.0f, IntensityNow = 6.0f;
	inline bool NonZeroSent = false;	// the device's c248 may hold a cloth draw's values (or the grass shaders' matrices: see NoteVertexShader)
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
	// The grass vertex shaders (GRASS23x00x) keep their shadow matrices in c248..c255: after one of them c248 is not zero, and the
	// next object with cloth code would move although it is no cloth. Mark the register as 'to be zeroed' (the next non-cloth draw does it).
	inline void NoteVertexShader(const char* name) {
		if (S.On && name && name[0] == 'G' && name[1] == 'R' && name[2] == 'A' && name[3] == 'S' && name[4] == 'S') NonZeroSent = true;
	}
	inline void NoteShaders(const void* geometry, bool nvrVertexShader) {
		if (!S.On || !geometry) return;
		SeenNow.insert(geometry);
		if (!nvrVertexShader) VanillaNow.insert(geometry);
	}

	// Keywords and exclusions: lower case, '/' written for the path separator (the game's paths use '\').
	inline std::string Norm(const std::string& in) {
		std::string o(in);
		for (char& c : o) { c = (char)tolower((unsigned char)c); if (c == '/') c = '\\'; }
		return o;
	}
	inline bool Letter(const std::string& p, size_t i) { return i < p.size() && p[i] >= 'a' && p[i] <= 'z'; }
	// A keyword is found ANYWHERE in the path ('lecloak', 'leflag', 'bosbanner', 'heavyduster' count); a short one (4 letters or
	// fewer: cape, flag, tent, robe, tarp, coat) must close a word on its right (a plural s allowed): 'flag' is not 'flagstone'.
	// Longer ones can be a path piece like 'lucassimms\outfit' (several textures of one folder).
	inline bool KeywordIn(const std::string& p, const std::string& w) {
		for (size_t at = p.find(w); at != std::string::npos; at = p.find(w, at + 1)) {
			if (w.size() > 4) return true;
			size_t end = at + w.size();
			if (end < p.size() && p[end] == 's' && !Letter(p, end + 1)) end++;
			if (!Letter(p, end)) return true;
		}
		return false;
	}
	// The path is cloth when a keyword is in it, none of the Exclude pieces is, and the FILE's own words name no body part (a hat or
	// gloves that share the garment's folder or keyword do not move: ExcludeParts, matched against the file name's letters).
	// Diagnostic (LogMatches): a static texture that looks like cloth but did not match is listed, so missed words can be found.
	inline bool LooksLikeCloth(const char* path) {
		const std::string p = Norm(path);
		for (const char* w : { "cloth", "flag", "banner", "cloak", "cape", "sheet", "tent", "tarp", "canvas", "rag", "curtain", "drape", "awning", "fabric", "blanket", "towel", "laundry", "pennant", "rug", "sail", "coat", "robe", "scarf" })
			if (p.find(w) != std::string::npos) return true;
		return false;
	}
	inline bool IsClothTexture(const char* path) {
		if (!path || !path[0]) return false;
		const std::string p = Norm(path);
		bool hit = false;
		for (const std::string& w : Words) if (!w.empty() && KeywordIn(p, w)) { hit = true; break; }
		if (!hit) return false;
		for (const std::string& x : Excluded) if (!x.empty() && p.find(x) != std::string::npos) return false;
		const size_t slash = p.find_last_of('\\');
		const std::string file = p.substr(slash == std::string::npos ? 0 : slash + 1);
		for (size_t i = 0; i < file.size();) {
			if (!Letter(file, i)) { i++; continue; }
			size_t j = i;
			while (Letter(file, j)) j++;
			const std::string word = file.substr(i, j - i);
			for (const std::string& x : ExcludedParts) {
				if (x.empty()) continue;
				if (x.size() >= 4 ? word.find(x) != std::string::npos
					: (word.size() >= x.size() && (word.compare(word.size() - x.size(), x.size(), x) == 0 || (word.back() == 's' && word.size() > x.size() && word.compare(word.size() - 1 - x.size(), x.size(), x) == 0))))
					return false;
			}
			i = j;
		}
		return true;
	}
	inline void SplitList(const char* text, std::vector<std::string>& out) {
		out.clear();
		std::string w;
		for (const char* c = text; ; c++) {
			if (!*c || *c == ',') { while (!w.empty() && w.back() == ' ') w.pop_back(); if (!w.empty()) out.push_back(Norm(w)); w.clear(); if (!*c) break; }
			else if (!(w.empty() && *c == ' ')) w += *c;
		}
	}

	// The bind-pose height range of a skinned mesh (its own vertices before skinning), once per geometry data. The vertex array
	// is read only when the game kept it (KEEP_XYZ); else the bound sphere stands in.
	inline const Extent& ExtentOf(NiGeometry* geometry) {
		NiGeometryData* d = geometry->geomData;
		auto it = Extents.find(d);
		if (it != Extents.end() && it->second.count == d->m_usVertices) return it->second;
		if (Extents.size() >= 4096) Extents.clear();
		Extent e;
		e.count = d->m_usVertices;
		if ((d->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) && d->m_pkVertex && d->m_usVertices > 3) {
			e.lo = 1e30f; e.hi = -1e30f;
			for (uint16_t i = 0; i < d->m_usVertices; i++) { const float z = d->m_pkVertex[i].z; if (z < e.lo) e.lo = z; if (z > e.hi) e.hi = z; }
			e.fromVertices = true;
		}
		else { e.lo = d->m_kBound.Center.z - d->m_kBound.Radius; e.hi = d->m_kBound.Center.z + d->m_kBound.Radius; }
		return Extents[d] = e;
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
		auto readList = [&](const char* key, std::string& from, std::vector<std::string>& out) {
			SettingManager::Configuration::ConfigNode node;
			const char* text = TheSettingManager->Config.FillNode(&node, section, key) ? node.LongValue.c_str() : nullptr;
			if (text && from != text) { from = text; SplitList(text, out); ByTexture.clear(); }
		};
		readList("Keywords", WordsFrom, Words);
		readList("Exclude", ExcludeFrom, Excluded);
		readList("ExcludeParts", ExcludePartsFrom, ExcludedParts);
		S.SkinMask = std::clamp(TheSettingManager->GetSettingI(section, "SkinMask"), 0, 1);
		S.PinFraction = std::clamp(TheSettingManager->GetSettingF(section, "PinFraction"), 0.0f, 0.9f);
		S.SkinSpace = std::clamp(TheSettingManager->GetSettingI(section, "SkinSpace"), 0, 2);
		S.CalmFactor = std::clamp(TheSettingManager->GetSettingF(section, "CalmFactor"), 0.0f, 1.7f);
		S.LogMatches = TheSettingManager->GetSettingI(section, "LogMatches") != 0;
		float d[3] = { S.Dir[0], S.Dir[1], S.Dir[2] };
		float speed = S.Speed, intensity = S.Intensity;
		if (S.UseGameWind && sky) {
			// The sky's wind (weather): direction as an angle around up, speed 0-1. Calm weather still moves cloth a little.
			const float a = sky->windDirection;
			d[0] = sinf(a); d[1] = cosf(a); d[2] = 0.0f;
			const float w = std::clamp(sky->windSpeed, 0.0f, 1.0f);
			speed *= 0.4f + 1.2f * w;
			intensity *= S.CalmFactor + (1.7f - S.CalmFactor) * w;	// (2026-10-18: calm was 0.3, i.e. 4 cm of movement in the desert: invisible)
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
			Logger::Log("Cloth wind: %.1f cloth draws per frame (%.1f worn); clock %.2f s of wind per second of real time (1 = normal play, 0 = paused)", ClothDraws / (float)Frames, SkinnedDrawsNow / (float)Frames, real > 0.0 ? (ClockSeconds - ClockStart) / real : 0.0);
			RealStart = TheFrameRateManager ? TheFrameRateManager->Time : -1.0; ClockStart = ClockSeconds;
			Frames = ClothDraws = SkinnedDrawsNow = 0;
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
		const bool skinned = geometry->skinInstance != nullptr;
		bool cloth;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) cloth = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();	// 2026-10-17: entries are per texture object and were never dropped; the answer is worked out again on demand (same result)
			cloth = IsClothTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), cloth };
			if (cloth || (S.LogMatches && (skinned || LooksLikeCloth(diffuse->ddsPath1)))) {
				const NiGeometryData* gd = geometry->geomData;
				Logger::Log("Cloth wind: %s %s (%s '%s', %u vertices%s)", cloth ? "CLOTH" : "not cloth:", diffuse->ddsPath1, skinned ? "skinned" : "static",
					geometry->m_pcName ? geometry->m_pcName : "", gd ? (unsigned)gd->m_usVertices : 0u, gd && (gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) ? ", vertices kept" : ", vertices not kept");
			}
		}
		// The vertex colour is the mask: without colours in the mesh, only the whole-mesh mode applies.
		if (!cloth || (S.MaskMode == 0 && (!geometry->geomData || !geometry->geomData->m_pkColor))) { Zero(device); return; }
		// Into the vertex shader's space: world for skinned meshes, the object's own for the rest.
		float dir[3] = { WorldDir[0], WorldDir[1], WorldDir[2] };
		float scale = 1.0f;
		float mode = (float)S.MaskMode;
		float top = 0.0f, invRange = 0.0f;	// mask 2 / 3, in the shader's space
		float origin[3] = { 0.0f, 0.0f, 0.0f };	// where the shader's space origin lies in the world (phases are shifted by it, so the pattern stays on the world)
		if (!skinned) {
			const NiTransform& w = geometry->m_worldTransform;
			scale = w.scale > 1e-4f ? w.scale : 1.0f;
			for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			if (geometry->geomData) {	// hanging cloth: pinned at the top of its bound, loosest at the bottom (object space)
				const NiBound& b = geometry->geomData->m_kBound;
				top = b.Center.z + 0.8f * b.Radius; invRange = b.Radius > 1e-3f ? 1.0f / (1.6f * b.Radius) : 0.0f;
			}
		}
		else {	// worn clothing
			const NiAVObject* root = geometry->skinInstance->RootParent ? geometry->skinInstance->RootParent : geometry;
			SkinnedDrawsNow++;
			if (S.MaskMode != 2) {}	// mask 0 (vertex colour) and 1 (whole mesh) need no heights
			else if (S.SkinMask == 1 && geometry->geomData) {
				// 2026-10-18 (round 3): the mask is the garment's own BIND-POSE height (the shader reads the vertex before skinning): the top
				// PinFraction of the garment never moves, the hem moves most. Independent of the actor's pose and of the space the bones
				// put the vertices in -- round 2's actor-height mask (heights above the skin root) put the pin ~70 units too high.
				const Extent& e = ExtentOf(geometry);
				const float height = e.hi - e.lo > 1.0f ? e.hi - e.lo : 1.0f;
				mode = 3.0f;
				top = e.hi - S.PinFraction * height;
				invRange = 1.0f / ((1.0f - S.PinFraction) * height);
			}
			else {	// the old actor-height mask (SkinMask 0): from the waist down to near the ground
				const float range = S.HeightTop > S.HeightBottom ? S.HeightTop - S.HeightBottom : 1.0f;
				float rootAbove = 0.0f;
				if (S.HeightFromFeet) {
					const float d = root->m_worldTransform.pos.z - geometry->m_worldTransform.pos.z;
					if (d > -20.0f && d < 140.0f) rootAbove = d;
				}
				if (shadowPass || S.SkinSpace != 0) {
					top = root->m_worldTransform.pos.z - rootAbove + S.HeightTop - (S.SkinSpace == 1 ? (float)TheRenderManager->CameraPosition.z : 0.0f);
					invRange = 1.0f / range;
				}
				else {
					const NiTransform& w = root->m_worldTransform;
					scale = w.scale > 1e-4f ? w.scale : 1.0f;
					top = (S.HeightTop - rootAbove) / scale;
					invRange = scale / range;
				}
			}
			// The wind direction in the space the bones put the vertices in.
			if (S.SkinSpace == 0 && !shadowPass) {	// the skin root's space (round 2's assumption)
				const NiTransform& w = root->m_worldTransform;
				scale = w.scale > 1e-4f ? w.scale : 1.0f;
				for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			}
			else if (S.SkinSpace == 1) { origin[0] = TheRenderManager->CameraPosition.x; origin[1] = TheRenderManager->CameraPosition.y; origin[2] = TheRenderManager->CameraPosition.z; }
			if (S.LogMatches) {
				static int told = 0;
				if (told < 12) {
					told++;
					const NiSkinInstance* si = geometry->skinInstance;
					const float* bm = (const float*)si->BoneMatrixes;
					const float* sw = (const float*)si->SkinToWorldWorldToSkin;
					const Extent& e = ExtentOf(geometry);
					Logger::Log("Cloth wind: skin %s: root (%.0f %.0f %.0f) geometry (%.0f %.0f %.0f) camera (%.0f %.0f %.0f) bind z %.1f..%.1f%s bound c.z %.1f r %.1f",
						diffuse->ddsPath1, root->m_worldTransform.pos.x, root->m_worldTransform.pos.y, root->m_worldTransform.pos.z,
						geometry->m_worldTransform.pos.x, geometry->m_worldTransform.pos.y, geometry->m_worldTransform.pos.z,
						TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z, e.lo, e.hi, e.fromVertices ? "" : " (bound)",
						geometry->geomData ? geometry->geomData->m_kBound.Center.z : 0.0f, geometry->geomData ? geometry->geomData->m_kBound.Radius : 0.0f);
					if (bm) Logger::Log("Cloth wind:   bone matrix 0 rows: %.2f %.2f %.2f %.1f | %.2f %.2f %.2f %.1f | %.2f %.2f %.2f %.1f", bm[0], bm[1], bm[2], bm[3], bm[4], bm[5], bm[6], bm[7], bm[8], bm[9], bm[10], bm[11]);
					if (sw) Logger::Log("Cloth wind:   skin-to-world: %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.1f %.1f %.1f %.2f", sw[0], sw[1], sw[2], sw[3], sw[4], sw[5], sw[6], sw[7], sw[8], sw[9], sw[10], sw[11], sw[12], sw[13], sw[14], sw[15]);
				}
			}
		}
		// Phases for this draw: the pattern is a function of the vertex's position in the shader's space; when that space's origin is
		// not the world's (camera-relative skinned meshes), shift the phases so the pattern stays where the wind put it.
		float ph[4] = { Phase[0], Phase[1], Phase[2], Phase[3] };
		if (origin[0] != 0.0f || origin[1] != 0.0f || origin[2] != 0.0f) {
			const double w = (double)scale / S.Wavelength, r = (double)S.RippleScale * scale;
			const double d0 = (origin[0] * dir[0] + origin[1] * dir[1] + origin[2] * dir[2]) * w;
			const double d1 = origin[0] * 0.0021 + origin[1] * 0.0027 + origin[2] * 0.0013;
			const double d2 = (origin[0] * 0.71 + origin[1] * 0.53 + origin[2] * 0.37) * r;
			const double d3 = (origin[0] * -0.43 + origin[1] * 0.81 + origin[2] * 0.59) * r * 1.7;
			auto wrap = [](double c) { return (float)(c - floor(c)); };
			ph[0] = wrap(Phase[0] - d0); ph[1] = wrap(Phase[1] + d1); ph[2] = wrap(Phase[2] + d2); ph[3] = wrap(Phase[3] - d3);
		}
		const float c[16] = {
			1.0f, mode, S.Ripple, S.FlutterAmount,
			dir[0], dir[1], dir[2], scale / S.Wavelength,
			ph[0], ph[1], ph[2], ph[3],
			IntensityNow / scale, S.RippleScale * scale, top, invRange };
		device->SetVertexShaderConstantF(248, c, 4);
		NonZeroSent = true;
		ClothDraws++;
	}
}
