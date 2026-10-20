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
		int   SkinMask = 1;				// worn clothing: 0 the old actor-height mask (skin root), 1 bind-pose height above the feet (HeightTop..HeightBottom, legs still below LegCut), 2 the garment's own height range (PinFraction)
		float LegCut = 14.0f;			// SkinMask 1: below this bind-pose height (boots, ankles) nothing moves, full strength 10 units higher
		float PinFraction = 0.4f;		// SkinMask 1: the top part of the garment (fraction of its height) that does not move at all
		int   SkinSpace = 1;			// worn clothing, what space the bones put vertices in: 0 the skin root's (rotated wind), 1 camera-relative world, 2 world
		float CalmFactor = 0.6f;		// UseGameWind: strength in dead calm (rising to 1.7 in a gale); it was 0.3
		bool  LogMatches = false;		// diagnostic: every skinned texture and every match goes to the log with its mesh numbers
		// 2026-10-19 worn clothing follows its wearer (see MotionOf)
		float MotionDrag = 0.1f;		// units the hem trails behind per unit/s of the wearer's speed, at low speed (walk ~100/s: ~8.5 units)
		float MotionMax = 30.0f;		// the most it trails (approached softly: a run ~350/s ~20 units)
		float MotionSpring = 1.5f;		// how fast it swings back (cycles per second)
		float MotionDamping = 0.25f;	// 0 swings on and on .. 1 settles without swinging past
		float StrideSwing = 6.0f;		// units of side-to-side swish at a run (less walking, none standing)
		float StrideLength = 250.0f;	// units run per swish (one left and one right step); shorter steps when walking
		float TurnLag = 0.12f;			// seconds the hem lags behind a turn (it flares out); 0 off
		bool  MotionIndoors = true;		// the movement part also indoors (no wind there)
		bool  UseSkeleton = true;		// 2026-10-19 worn clothing masked by its own skeleton (see SkeletonOf); off: SkinMask
		float LimbGap = 1.0f;			// units off the limb (beyond a human limb's radius) where cloth starts to move
		float LimbFade = 5.0f;			// ...and how many units further it moves fully
		std::string Keywords = "cape,cloak,duster,flag,banner,curtain,drape,tarp,poncho,robe,scarf,tent";
		std::string Exclude = "scape,probe,microbe,wardrobe,drobe,content,intent,extent,patent,potent,latent,flagpole,flagstone,pole,stone";
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
	// 2026-10-19 per wearer (the skin's root node): its movement, the hem's lag as a damped spring, the stride phase
	struct Motion {
		float pos[3] = {}, vel[3] = {};			// last position, smoothed velocity (units/s)
		float offset[3] = {}, offsetVel[3] = {};	// the hem's trailing offset (world units) and its rate
		float yaw = 0.0f, yawRate = 0.0f, turn = 0.0f, turnVel = 0.0f;	// heading, smoothed turn rate (rad/s), lag angle (rad) and its rate
		double stride = 0.0, clock = 0.0;
		UInt32 frame = 0;
		bool valid = false;
		float out[4] = {};						// this frame's offset (xyz) and turn lag (w), the same for every pass
	};
	inline std::unordered_map<const void*, Motion> Motions;
	inline UInt32 FrameId = 0;
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
	inline bool CurrentVSCapable = false;	// 2026-10-19: the vertex shader the last SetShaders bound carries the cloth code
	inline std::unordered_map<const void*, bool> TentativeNow;	// 2026-10-19: per object this frame, did the BOUND vertex shader carry it at each lit pass
	inline void NoteVertexShader(const char* name) {
		if (S.On && name && name[0] == 'G' && name[1] == 'R' && name[2] == 'A' && name[3] == 'S' && name[4] == 'S') NonZeroSent = true;
		CurrentVSCapable = S.On && CapableName(name);
	}
	// 2026-10-19 (P81 report 'cloth: only the player's SHADOW moves'): the object the render-pass hooks are drawing right now. SetShadersHook
	// used the game's 'current geometry' global, which for worn (skinned) meshes is not the mesh being drawn: their lit passes were never
	// noted, the SAFETY rule skipped every one of them (600 of 600 frames on NVRAutoTest12) while the shadow passes moved them.
	inline const void* PassGeometry = nullptr;
	inline std::unordered_map<const void*, std::string> VanillaVSNow, VanillaVSLast;
	// 2026-10-19 diagnostic (LogMatches): per worn cloth mesh, lit passes moved / skipped (not seen by SetShaders, vanilla VS), shadow passes
	struct PassCounts { std::string name; UInt32 lit = 0, notSeen = 0, vanilla = 0, shadow = 0; };
	inline std::unordered_map<const void*, PassCounts> ClothPassCounts;	// 2026-10-19 diagnostic (LogMatches): which vertex shader
	inline void NoteShaders(const void* geometry, bool nvrVertexShader, const char* vertexShaderName = nullptr) {
		if (!S.On || !geometry) return;
		SeenNow.insert(geometry);
		if (!nvrVertexShader) {
			VanillaNow.insert(geometry);
			if (S.LogMatches) VanillaVSNow[geometry] = vertexShaderName ? vertexShaderName : "(none)";
		}
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

	// 2026-10-19 (user: 'our body itself is billowing around, not just the coat -- how can we do this intelligently without a lot of
	// hand-made tweaking'): worn clothing masked by the garment's OWN skeleton. Every skinned mesh carries its bones (names) and where
	// each sat when the mesh was made (NiSkinData SkinToBone; the bone's origin in the mesh's own space = the space the vertex shader's
	// IN.position is in). From them: hip, knee, ankle and toe of each leg, elbow and wrist of each arm. The shader moves only what stands
	// OFF those limbs (distance to the nearest limb segment minus a limb radius: hip 7 -> knee 5 -> ankle 5 (boots) -> toe tip 3.5,
	// elbow 3.5 -> wrist 2.5 units, 1 unit = 1.43 cm), only below the hips (pinned at the hip joints, loosest from the knees down) and
	// nothing at ground level (fades in over the 6 units up to the ankles): legs, trousers, boots, hands and the torso stay put, a coat's
	// skirt and tails move -- with no per-armour numbers. The height mask it replaces (SkinMask 1) let everything below HeightTop 95
	// (chest height) move, hips, legs and hands included. Checked offline on vanilla NIFs (evidence\cloth3\skeleton_mask_check.py:
	// hips z 67.8, knees 35.8, ankles 7.1, toes 1.2 forward +y; legs 0.00 moving vs 0.4-0.95 before). A mesh without thigh bones (a
	// creature, a flag) keeps SkinMask. A missing ankle is put a thigh's length below the knee, a missing toe 12 units forward of the
	// ankle (+y, the bind pose's forward); missing arms are left out.
	struct Skeleton {
		bool ok = false;
		const void* boneData = nullptr; UInt32 bones = 0;	// what it was made from (the skin data can be freed and reused)
		float pts[12][4] = {};			// L hip, knee, ankle, toe tip, R hip, knee, ankle, toe tip, L elbow, wrist, R elbow, wrist (xyz, radius)
		float hipZ = 0.0f, kneeZ = 0.0f, groundZ = 0.0f;
	};
	inline std::unordered_map<const void*, Skeleton> Skeletons;	// per NiSkinData
	inline UInt32 SkeletonsLogged = 0;
	inline float LimbDistance(const float* p, const float* a, const float* b) {	// to the segment a-b, minus its radius there
		const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
		const float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
		const float t = std::clamp(len2 > 1e-4f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2 : 0.0f, 0.0f, 1.0f);
		const float d[3] = { ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t };
		return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - (a[3] + (b[3] - a[3]) * t);
	}
	inline float Smooth01(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
	// The same mask as the shader's (ClothWind.hlsl, mode 4): for the log's check that the bones and the vertices line up.
	inline float SkeletonMask(const Skeleton& k, const float* p) {
		static const int seg[8][2] = { {0, 1}, {1, 2}, {2, 3}, {4, 5}, {5, 6}, {6, 7}, {8, 9}, {10, 11} };
		float d = 1e30f;
		for (const auto& sg : seg) d = (std::min)(d, LimbDistance(p, k.pts[sg[0]], k.pts[sg[1]]));
		const float off = Smooth01((d - S.LimbGap) / (std::max)(S.LimbFade, 0.1f));
		const float h = Smooth01((k.hipZ - p[2]) / (std::max)(k.hipZ - k.kneeZ, 1.0f));
		return off * h * Smooth01((p[2] - k.groundZ) / 6.0f);
	}
	inline const Skeleton& SkeletonOf(NiGeometry* geometry) {
		static const Skeleton none;
		const NiSkinInstance* si = geometry->skinInstance;
		const NiSkinData* sd = si ? si->SkinData : nullptr;
		if (!sd || !sd->BoneData || !si->BoneObjects) return none;
		auto it = Skeletons.find(sd);
		if (it != Skeletons.end() && it->second.boneData == sd->BoneData && it->second.bones == sd->Bones) return it->second;
		if (Skeletons.size() >= 2048) Skeletons.clear();
		Skeleton k;
		k.boneData = sd->BoneData; k.bones = sd->Bones;
		static const char* const names[12] = { "bip01 l thigh", "bip01 l calf", "bip01 l foot", "bip01 l toe0", "bip01 r thigh", "bip01 r calf",
			"bip01 r foot", "bip01 r toe0", "bip01 l forearm", "bip01 l hand", "bip01 r forearm", "bip01 r hand" };
		static const float radius[12] = { 7.0f, 5.0f, 5.0f, 3.5f, 7.0f, 5.0f, 5.0f, 3.5f, 3.5f, 2.5f, 3.5f, 2.5f };
		bool found[12] = {};
		const UInt32 count = (std::min)(sd->Bones, si->Bones);
		for (UInt32 b = 0; b < count; b++) {
			const NiAVObject* bone = si->BoneObjects[b];
			const char* name = bone ? bone->m_pcName : nullptr;
			if (!name) continue;
			for (int n = 0; n < 12; n++) {
				if (found[n] || _stricmp(name, names[n])) continue;
				// the bone's origin in the mesh's space: SkinToBone maps p to s R p + t, so its origin is -R^T t / s
				const NiTransform& t = sd->BoneData[b].SkinToBone;
				const float sc = t.scale > 1e-4f ? t.scale : 1.0f;
				for (int i = 0; i < 3; i++)
					k.pts[n][i] = -(t.rot.data[0][i] * t.pos.x + t.rot.data[1][i] * t.pos.y + t.rot.data[2][i] * t.pos.z) / sc;
				k.pts[n][3] = radius[n];
				found[n] = true;
			}
		}
		k.ok = found[0] && found[1] && found[4] && found[5];
		if (k.ok) {
			for (int leg = 0; leg < 2; leg++) {
				float* hip = k.pts[leg * 4], *knee = k.pts[leg * 4 + 1], *ankle = k.pts[leg * 4 + 2], *toe = k.pts[leg * 4 + 3];
				if (!found[leg * 4 + 2]) { for (int i = 0; i < 3; i++) ankle[i] = knee[i] + (knee[i] - hip[i]) * 0.95f; ankle[3] = radius[2]; }	// a thigh's length below the knee
				if (!found[leg * 4 + 3]) { toe[0] = ankle[0]; toe[1] = ankle[1] + 11.6f; toe[2] = ankle[2] - 5.9f; toe[3] = radius[3]; }	// vanilla: 11.6 forward, 5.9 down
				for (int i = 0; i < 3; i++) toe[i] += (toe[i] - ankle[i]) * 0.35f;	// the toe bone sits at the ball of the foot: on to the tip
			}
			for (int arm = 0; arm < 2; arm++) {	// a missing arm: far away (no effect)
				if (!found[8 + arm * 2] || !found[9 + arm * 2])
					for (int n = 8 + arm * 2; n < 10 + arm * 2; n++) { k.pts[n][0] = k.pts[n][1] = 0.0f; k.pts[n][2] = -1e5f; k.pts[n][3] = 0.0f; }
			}
			k.hipZ = 0.5f * (k.pts[0][2] + k.pts[4][2]);
			k.kneeZ = 0.5f * (k.pts[1][2] + k.pts[5][2]);
			k.groundZ = 0.5f * (k.pts[2][2] + k.pts[6][2]) - 6.0f;
			k.ok = k.hipZ - k.kneeZ > 5.0f;	// an upright leg (else the space is not what we think: keep SkinMask)
		}
		if (SkeletonsLogged < 24 || S.LogMatches) {	// the check: bone heights, and how much of the mesh the mask moves
			SkeletonsLogged++;
			const NiGeometryData* gd = geometry->geomData;
			UInt32 moving = 0, total = 0;
			if (k.ok && gd && (gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) && gd->m_pkVertex)
				for (UInt16 v = 0; v < gd->m_usVertices; v++, total++) if (SkeletonMask(k, &gd->m_pkVertex[v].x) > 0.5f) moving++;
			Logger::Log("Cloth wind: skeleton of '%s' (%u bones): %s; hips z %.1f, knees z %.1f, ankles z %.1f %.1f, elbows %s%s%s; %u of %u vertices (kept) move > half",
				geometry->m_pcName ? geometry->m_pcName : "?", sd->Bones, k.ok ? "used" : "NOT used (no upright legs: SkinMask)", k.hipZ, k.kneeZ,
				k.pts[2][2], k.pts[6][2], found[8] && found[10] ? "found" : "missing", found[2] && found[6] ? "" : " (ankles guessed)",
				found[3] && found[7] ? "" : " (toes guessed)", moving, total);
		}
		return Skeletons[sd] = k;
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
		// 2026-10-19: objects whose passes the game drew without setting shaders (not noted by SetShadersHook) count by the shader bound then
		for (const auto& [g, capable] : TentativeNow)
			if (!SeenNow.count(g)) { SeenNow.insert(g); if (!capable) VanillaNow.insert(g); }
		TentativeNow.clear();
		SeenLast.swap(SeenNow); SeenNow.clear(); VanillaLast.swap(VanillaNow); VanillaNow.clear();
		VanillaVSLast.swap(VanillaVSNow); VanillaVSNow.clear();
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
		// 2026-10-19 (user: 'toggled it, restarted, couldn't see much'): a settings file saved before 2026-10-18 holds the OLD default
		// keyword list word for word (the defaults are written back into it), which names none of the vanilla coats and dusters, so
		// only flags and tarps moved. Exactly that old list now means 'the current default'.
		auto readList = [&](const char* key, std::string& from, std::vector<std::string>& out, const char* oldDefault) {
			SettingManager::Configuration::ConfigNode node;
			if (!TheSettingManager->Config.FillNode(&node, section, key)) return;
			std::string text = node.LongValue;
			if (oldDefault && text == oldDefault) {
				try { text = TheSettingManager->Config.DefaultConfig.at("_Shaders").at("Cloth").at("Main").at(key).as_string(); }
				catch (...) {}
			}
			if (from != text) { from = text; SplitList(text.c_str(), out); ByTexture.clear(); }
		};
		readList("Keywords", WordsFrom, Words, "cape,cloak,duster,flag,banner,curtain,drape,tarp,poncho,robe,scarf,tent");
		readList("Exclude", ExcludeFrom, Excluded, nullptr);
		readList("ExcludeParts", ExcludePartsFrom, ExcludedParts, nullptr);
		S.MotionDrag = std::clamp(TheSettingManager->GetSettingF(section, "MotionDrag"), 0.0f, 0.3f);
		S.MotionMax = std::clamp(TheSettingManager->GetSettingF(section, "MotionMax"), 0.0f, 80.0f);
		S.MotionSpring = std::clamp(TheSettingManager->GetSettingF(section, "MotionSpring"), 0.2f, 6.0f);
		S.MotionDamping = std::clamp(TheSettingManager->GetSettingF(section, "MotionDamping"), 0.05f, 1.0f);
		S.StrideSwing = std::clamp(TheSettingManager->GetSettingF(section, "StrideSwing"), 0.0f, 30.0f);
		S.StrideLength = std::clamp(TheSettingManager->GetSettingF(section, "StrideLength"), 40.0f, 600.0f);
		S.TurnLag = std::clamp(TheSettingManager->GetSettingF(section, "TurnLag"), 0.0f, 0.5f);
		S.MotionIndoors = TheSettingManager->GetSettingI(section, "MotionIndoors") != 0;
		S.UseSkeleton = TheSettingManager->GetSettingI(section, "UseSkeleton") != 0;
		S.LimbGap = std::clamp(TheSettingManager->GetSettingF(section, "LimbGap"), -5.0f, 30.0f);
		S.LimbFade = std::clamp(TheSettingManager->GetSettingF(section, "LimbFade"), 0.5f, 40.0f);
		FrameId++;
		S.SkinMask = std::clamp(TheSettingManager->GetSettingI(section, "SkinMask"), 0, 2);
		S.LegCut = std::clamp(TheSettingManager->GetSettingF(section, "LegCut"), 0.0f, 60.0f);
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
			if (S.LogMatches) {
				for (const auto& [g, c] : ClothPassCounts)
					Logger::Log("Cloth wind: worn mesh '%s': lit passes moved %u, skipped %u (not seen by SetShaders) + %u (vertex shader without cloth code%s%s), shadow passes %u",
						c.name.c_str(), c.lit, c.notSeen, c.vanilla, VanillaVSLast.count(g) ? ": " : "", VanillaVSLast.count(g) ? VanillaVSLast.at(g).c_str() : "", c.shadow);
				ClothPassCounts.clear();
			}
			for (auto m = Motions.begin(); m != Motions.end();)	// wearers not drawn for a while (gone, or the node freed)
				m = FrameId - m->second.frame > 600 ? Motions.erase(m) : std::next(m);
			Frames = ClothDraws = SkinnedDrawsNow = 0;
		}
	}

	inline void Zero(IDirect3DDevice9* device) {
		if (!NonZeroSent) return;
		static const float zero[4] = { 0, 0, 0, 0 };
		device->SetVertexShaderConstantF(248, zero, 1);
		NonZeroSent = false;
	}

	// 2026-10-19 (user: 'make it more dramatic for our armor so we see the trench coat moving around when we walk'): worn clothing
	// follows its wearer -- the hem trails behind as they walk or run and swings past and settles when they stop (a damped spring),
	// swishes side to side with the steps, and lags behind a turn so the coat flares out. Worked out once per wearer per frame (every
	// pass of the frame gets the same numbers: see SAFETY) from the skin root's movement in play time (a pause holds it); a jump of
	// more than 400 units or a gap of more than a quarter second (a load, a teleport, not drawn for a while) starts it again at rest.
	// The shader adds offset + lag angle x (the point's distance from the wearer's upright axis) under the wind's height mask, so the
	// shoulders stay pinned and the boots still. The spring: semi-implicit Euler in steps of at most 1/120 s.
	inline const float* MotionOf(const NiAVObject* root) {
		Motion& m = Motions[root];
		if (m.valid && m.frame == FrameId) return m.out;
		const NiTransform& t = root->m_worldTransform;
		const float p[3] = { t.pos.x, t.pos.y, t.pos.z };
		const float yaw = atan2f(t.rot.data[1][0], t.rot.data[0][0]);	// the heading of the root's x axis in the world
		const double dt = ClockSeconds - m.clock;
		const float jump = sqrtf((p[0] - m.pos[0]) * (p[0] - m.pos[0]) + (p[1] - m.pos[1]) * (p[1] - m.pos[1]) + (p[2] - m.pos[2]) * (p[2] - m.pos[2]));
		if (!m.valid || dt > 0.25 || dt < 0.0 || jump > 400.0f) {
			m = Motion();
			m.valid = true; m.frame = FrameId; m.clock = ClockSeconds; m.yaw = yaw;
			for (int i = 0; i < 3; i++) m.pos[i] = p[i];
			return m.out;
		}
		m.frame = FrameId;
		if (dt < 1e-5) return m.out;	// no play time passed (paused): hold
		m.clock = ClockSeconds;
		const float h = (float)dt, smooth = 1.0f - expf(-h / 0.1f);	// the velocity is smoothed over ~0.1 s (the animation's own bob)
		for (int i = 0; i < 3; i++) { m.vel[i] += ((p[i] - m.pos[i]) / h - m.vel[i]) * smooth; m.pos[i] = p[i]; }
		float dy = yaw - m.yaw;
		if (dy > 3.14159265f) dy -= 6.2831853f; else if (dy < -3.14159265f) dy += 6.2831853f;
		m.yaw = yaw;
		m.yawRate += (dy / h - m.yawRate) * smooth;
		// targets: trail behind the movement (half as much up and down), Drag x speed at low speed, softly up to Max (a walk shows,
		// a sprint does not throw the coat over the legs); lag behind the turn
		float target[3] = { -m.vel[0], -m.vel[1], -m.vel[2] * 0.5f };
		const float tl = sqrtf(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
		const float trail = S.MotionMax > 0.0f ? S.MotionMax * (1.0f - expf(-tl * S.MotionDrag / S.MotionMax)) : 0.0f;
		for (float& v : target) v = tl > 1e-3f ? v * trail / tl : 0.0f;
		const float turnTarget = std::clamp(-m.yawRate * S.TurnLag, -0.5f, 0.5f);
		const float w = 6.2831853f * S.MotionSpring, k = w * w, c = 2.0f * S.MotionDamping * w;
		const int steps = (std::min)((int)ceilf(h * 120.0f), 30);
		const float s = h / steps;
		for (int n = 0; n < steps; n++) {
			for (int i = 0; i < 3; i++) { m.offsetVel[i] += (k * (target[i] - m.offset[i]) - c * m.offsetVel[i]) * s; m.offset[i] += m.offsetVel[i] * s; }
			m.turnVel += (k * (turnTarget - m.turn) - c * m.turnVel) * s; m.turn += m.turnVel * s;
		}
		const float ol = sqrtf(m.offset[0] * m.offset[0] + m.offset[1] * m.offset[1] + m.offset[2] * m.offset[2]), most = 1.5f * S.MotionMax + 1.0f;
		if (ol > most) for (float& v : m.offset) v *= most / ol;
		m.turn = std::clamp(m.turn, -0.6f, 0.6f);
		// the stride swish: across the movement, one cycle per two steps -- StrideLength at a run (~350/s, 1.4 a second), shorter
		// steps when walking (~0.65 a second at 100/s) -- stronger the faster (about half at a walk, full at a run)
		const float speed = sqrtf(m.vel[0] * m.vel[0] + m.vel[1] * m.vel[1]), pace = (std::min)(speed / 350.0f, 1.5f);
		m.stride += dt * speed / (S.StrideLength * (0.45f + 0.55f * (std::min)(pace, 1.0f))); m.stride -= floor(m.stride);
		const float swish = speed > 1.0f ? sinf(6.2831853f * (float)m.stride) * S.StrideSwing * sqrtf(pace) / speed : 0.0f;
		m.out[0] = m.offset[0] - m.vel[1] * swish;
		m.out[1] = m.offset[1] + m.vel[0] * swish;
		m.out[2] = m.offset[2];
		m.out[3] = m.turn;
		return m.out;
	}

	// Before an object is drawn (the game's passes and NVR's shadow passes): its cloth values, or strength 0.
	inline void ForGeometry(IDirect3DDevice9* device, NiGeometry* geometry, bool shadowPass = false) {
		if (!device) return;
		// 2026-10-19: indoors (no wind) worn clothing still follows its wearer (MotionIndoors); everything else stays still there
		if (!S.On || !geometry || (!Exterior && !S.MotionIndoors)) { Zero(device); return; }
		// 2026-10-19 (P81 'only the player's SHADOW moves'; measured on NVRAutoTest12: the Combat Ranger coat's lit pass skipped 600 of 600
		// frames, 'not seen by SetShaders'): the game sets no shaders for a pass whose shaders are already bound (the coat follows its
		// Pip-Boy piece, same shaders), so SetShadersHook never notes it. Such a pass draws with the vertex shader bound now: judge it by
		// that one, and note it so the rule below holds for it from the next frame on (a vanilla one still keeps the object still).
		bool boundAnswer = false;
		if (!shadowPass) {
			auto t = TentativeNow.find(geometry);
			if (t == TentativeNow.end()) TentativeNow.emplace(geometry, CurrentVSCapable);
			else t->second = t->second && CurrentVSCapable;	// every pass of the object this frame
			boundAnswer = !SeenLast.count(geometry) && CurrentVSCapable && !VanillaLast.count(geometry);
		}
		if (!shadowPass && !boundAnswer && (!SeenLast.count(geometry) || VanillaLast.count(geometry))) {	// see SAFETY above
			if (S.LogMatches) {	// 2026-10-19 diagnostic: how often the safety rule skips a known worn cloth mesh, and why
				auto c = ClothPassCounts.find(geometry);
				if (c != ClothPassCounts.end()) { if (!SeenLast.count(geometry)) c->second.notSeen++; else c->second.vanilla++; }
			}
			Zero(device); return;
		}
		if (shadowPass && VanillaLast.count(geometry)) { Zero(device); return; }
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) { Zero(device); return; }	// BSShaderPPLightingProperty, Lighting30ShaderProperty
		BSShaderPPLightingProperty* lighting = (BSShaderPPLightingProperty*)property;
		const NiSourceTexture* diffuse = lighting->ppTextures[0] ? lighting->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) { Zero(device); return; }
		const bool skinned = geometry->skinInstance != nullptr;
		if (!Exterior && !skinned) { Zero(device); return; }
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
		float motion[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, axis[2] = { 0.0f, 0.0f };	// 2026-10-19 worn clothing: the wearer's movement (c252, c253)
		if (!skinned) {
			const NiTransform& w = geometry->m_worldTransform;
			scale = w.scale > 1e-4f ? w.scale : 1.0f;
			for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			if (geometry->geomData) {	// (statics have no movement part: c252/c253 stay 0)	// hanging cloth: pinned at the top of its bound, loosest at the bottom (object space)
				const NiBound& b = geometry->geomData->m_kBound;
				top = b.Center.z + 0.8f * b.Radius; invRange = b.Radius > 1e-3f ? 1.0f / (1.6f * b.Radius) : 0.0f;
			}
		}
		else {	// worn clothing
			const NiAVObject* root = geometry->skinInstance->RootParent ? geometry->skinInstance->RootParent : geometry;
			SkinnedDrawsNow++;
			if (S.LogMatches) {	// 2026-10-19 diagnostic (see ClothPassCounts)
				PassCounts& c = ClothPassCounts[geometry];
				if (c.name.empty()) c.name = geometry->m_pcName ? geometry->m_pcName : "?";
				if (shadowPass) c.shadow++; else c.lit++;
			}
			const Skeleton* skeleton = S.MaskMode == 2 && S.UseSkeleton ? &SkeletonOf(geometry) : nullptr;
			if (S.MaskMode != 2) {}	// mask 0 (vertex colour) and 1 (whole mesh) need no heights
			else if (skeleton && skeleton->ok) {	// 2026-10-19: the garment's own skeleton (SkeletonOf); c226-c239
				mode = 4.0f;
				float limbs[56] = {};
				memcpy(limbs, skeleton->pts, sizeof(skeleton->pts));
				limbs[48] = skeleton->hipZ; limbs[49] = 1.0f / (std::max)(skeleton->hipZ - skeleton->kneeZ, 1.0f);
				limbs[50] = S.LimbGap; limbs[51] = 1.0f / (std::max)(S.LimbFade, 0.1f);
				limbs[52] = skeleton->groundZ; limbs[53] = 1.0f / 6.0f;
				device->SetVertexShaderConstantF(226, limbs, 14);
			}
			else if (S.SkinMask == 1 && geometry->geomData) {
				// Actor meshes are authored with the feet at z 0 (checked on the vanilla Combat Ranger, Sheriff's duster and Patrol NIFs:
				// z 0..116): the bind-pose height IS the height above the feet. Mask as the settings say, legs and boots still.
				mode = 3.0f + S.LegCut * 0.001f;
				const float range = S.HeightTop > S.HeightBottom ? S.HeightTop - S.HeightBottom : 1.0f;
				top = S.HeightTop; invRange = 1.0f / range;
			}
			else if (S.SkinMask == 2 && geometry->geomData) {
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
			// 2026-10-19 the wearer's movement (MotionOf), in the same space: the offset, the lag angle, the wearer's upright axis
			const float* mo = MotionOf(root);
			if (S.SkinSpace == 0 && !shadowPass) {	// the skin root's space: its axis is the origin
				const NiTransform& w = root->m_worldTransform;
				for (int i = 0; i < 3; i++) motion[i] = (w.rot.data[0][i] * mo[0] + w.rot.data[1][i] * mo[1] + w.rot.data[2][i] * mo[2]) / scale;
			}
			else {
				for (int i = 0; i < 3; i++) motion[i] = mo[i];
				const bool camRel = S.SkinSpace == 1;
				axis[0] = root->m_worldTransform.pos.x - (camRel ? TheRenderManager->CameraPosition.x : 0.0f);
				axis[1] = root->m_worldTransform.pos.y - (camRel ? TheRenderManager->CameraPosition.y : 0.0f);
			}
			motion[3] = mo[3];
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
		const float c[24] = {
			1.0f, mode, S.Ripple, S.FlutterAmount,
			dir[0], dir[1], dir[2], scale / S.Wavelength,
			ph[0], ph[1], ph[2], ph[3],
			(Exterior ? IntensityNow : 0.0f) / scale, S.RippleScale * scale, top, invRange,	// (indoors: no wind, the movement part only)
			motion[0], motion[1], motion[2], motion[3],
			axis[0], axis[1], 0.0f, 0.0f };
		device->SetVertexShaderConstantF(248, c, 6);
		NonZeroSent = true;
		ClothDraws++;
	}
}
