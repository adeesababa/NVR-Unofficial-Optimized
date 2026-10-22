#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include <atomic>
#include "ClothWeights.h"
#include "ClothWalk.h"
#include "ClothStatic.h"
#include "TestTools.h"

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
		int   SkinMask = 1;
		float LegCut = 14.0f;
		float PinFraction = 0.4f;
		int   SkinSpace = 1;
		float CalmFactor = 0.6f;
		bool  LogMatches = false;
		float MotionDrag = 0.1f;
		float MotionMax = 30.0f;
		float MotionSpring = 1.5f;
		float MotionDamping = 0.25f;
		float StrideSwing = 6.0f;
		float StrideLength = 250.0f;
		float TurnLag = 0.12f;
		bool  MotionIndoors = true;
		bool  UseSkeleton = true;
		bool  UseWeights = true;
		bool  WalkSim = true;
		float WalkHang = 1.0f;
		float WalkSpring = 1.4f, WalkDamping = 0.2f, WalkDrag = 1.0f, WalkLegMargin = 1.5f, WalkMaxSwing = 25.0f;
		float WalkDragCap = 180.0f, WalkMaxAngle = 35.0f;
		float WalkHemSmooth = 1.0f;
		bool  StillWeights = true;
		bool  AutoDetect = false;
		float AutoShare = 2.0f;
		bool  LogAuto = true;
		float LimbGap = 1.0f;
		float LimbFade = 6.0f;
		struct Indoor { float Speed = 80.0f, Intensity = 2.0f, Wavelength = 400.0f, Flutter = 0.8f, FlutterAmount = 0.1f, Ripple = 0.3f, Strength = 1.0f; } In;
		std::string Keywords = "cape,cloak,duster,flag,banner,curtain,drape,tarp,poncho,robe,scarf,tent";
		std::string Exclude = "scape,probe,microbe,wardrobe,drobe,content,intent,extent,patent,potent,latent,flagpole,flagstone,pole,stone";
		std::string ExcludeParts = "hat,helmet,head,hair,glove,boot,shoe,mask,goggle,hand,body,beard,mouth,teeth,eyes";
	};

	inline Settings S;
	inline std::vector<std::string> Words, Excluded, ExcludedParts, ForcedOff;
	inline std::string WordsFrom, ExcludeFrom, ExcludePartsFrom, ForceOffFrom;
	struct Extent { uint16_t count = 0; float lo = 0.0f, hi = 0.0f; bool fromVertices = false; };
	inline std::unordered_map<const void*, Extent> Extents;
	inline UInt32 SkinnedDrawsNow = 0;
	inline std::unordered_map<const void*, std::pair<std::string, bool>> ByTexture;
	inline float WorldDir[3] = { 1.0f, 0.0f, 0.0f };
	inline float Phase[4] = {};
	inline double Cycles[4] = {};
	inline double Rate[4] = {};
	inline double ClockSeconds = 0.0;
	inline float LastHour = -1.0f;
	inline double RealStart = -1.0, ClockStart = 0.0;
	inline float SpeedNow = 220.0f, IntensityNow = 6.0f;
	inline float WavelengthNow = 260.0f, FlutterNow = 3.0f, FlutterAmountNow = 0.35f, RippleNow = 0.5f;
	inline bool Interior = false;
	inline bool IndoorWind = false;
	inline UInt32 IndoorLogs = 0;
	inline bool NonZeroSent = false;
	inline bool GrassInC248 = false;
	inline UInt32 ClothDraws = 0, Frames = 0;
	inline bool LoggedGameWind = false;
	inline bool CompiledInRead = false;
	inline bool Exterior = true;
	struct Motion {
		float pos[3] = {}, vel[3] = {};
		float offset[3] = {}, offsetVel[3] = {};
		float yaw = 0.0f, yawRate = 0.0f, turn = 0.0f, turnVel = 0.0f;
		double stride = 0.0, clock = 0.0;
		UInt32 frame = 0;
		bool valid = false;
		float out[4] = {};
	};
	inline std::unordered_map<const void*, Motion> Motions;
	inline UInt32 FrameId = 0;
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
	inline bool CurrentVSCapable = false;
	inline const char* CurrentVSName = nullptr;
	inline std::unordered_map<const void*, bool> TentativeNow;
	inline void Zero(IDirect3DDevice9* device);
	inline bool PlantsCompiledIn = false, PlantsOn = false;
	inline void NoteVertexShader(const char* name) {
		if (!CompiledInRead && TheSettingManager) { CompiledInRead = true; S.CompiledIn = TheSettingManager->GetSettingI("Shaders.Cloth.Main", "Enabled") != 0; }
		if (!S.CompiledIn && !PlantsCompiledIn) { CurrentVSCapable = false; return; }
		const bool capable = CapableName(name);
		if (!capable) { NonZeroSent = true; GrassInC248 = true; }
		if (capable && GrassInC248 && TheRenderManager) Zero(TheRenderManager->device);
		CurrentVSCapable = (S.On || PlantsOn) && capable;
		CurrentVSName = name;
	}
	inline const void* PassGeometry = nullptr;
	inline std::unordered_map<const void*, std::string> VanillaVSNow, VanillaVSLast;
	struct PassCounts { std::string name; UInt32 lit = 0, notSeen = 0, vanilla = 0, shadow = 0; };
	inline std::unordered_map<const void*, PassCounts> ClothPassCounts;
	inline void NoteShaders(const void* geometry, bool nvrVertexShader, const char* vertexShaderName = nullptr) {
		if (!(S.On || PlantsOn) || !geometry) return;
		SeenNow.insert(geometry);
		if (!nvrVertexShader) {
			VanillaNow.insert(geometry);
			if (S.LogMatches) VanillaVSNow[geometry] = vertexShaderName ? vertexShaderName : "(none)";
		}
	}

	inline std::string Norm(const std::string& in) {
		std::string o(in);
		for (char& c : o) { c = (char)tolower((unsigned char)c); if (c == '/') c = '\\'; }
		return o;
	}
	inline bool Letter(const std::string& p, size_t i) { return i < p.size() && p[i] >= 'a' && p[i] <= 'z'; }
	inline bool KeywordIn(const std::string& p, const std::string& w) {
		for (size_t at = p.find(w); at != std::string::npos; at = p.find(w, at + 1)) {
			if (w.size() > 4) return true;
			size_t end = at + w.size();
			if (end < p.size() && p[end] == 's' && !Letter(p, end + 1)) end++;
			if (!Letter(p, end)) return true;
		}
		return false;
	}
	inline bool LooksLikeCloth(const char* path) {
		const std::string p = Norm(path);
		for (const char* w : { "cloth", "flag", "banner", "cloak", "cape", "sheet", "tent", "tarp", "canvas", "rag", "curtain", "drape", "awning", "fabric", "blanket", "towel", "laundry", "pennant", "rug", "sail", "coat", "robe", "scarf" })
			if (p.find(w) != std::string::npos) return true;
		return false;
	}
	inline bool ExcludedPart(const std::string& p);
	inline bool IsForcedOff(const std::string& p) {
		for (const std::string& x : ForcedOff) if (!x.empty() && p.find(x) != std::string::npos) return true;
		return false;
	}
	inline bool IsClothTexture(const char* path) {
		if (!path || !path[0]) return false;
		const std::string p = Norm(path);
		bool hit = false;
		for (const std::string& w : Words) if (!w.empty() && KeywordIn(p, w)) { hit = true; break; }
		if (!hit) return false;
		for (const std::string& x : Excluded) if (!x.empty() && p.find(x) != std::string::npos) return false;
		return !IsForcedOff(p) && !ExcludedPart(p);
	}
	inline bool ExcludedPart(const std::string& p) {
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
					return true;
			}
			i = j;
		}
		return false;
	}
	inline void SplitList(const char* text, std::vector<std::string>& out) {
		out.clear();
		std::string w;
		for (const char* c = text; ; c++) {
			if (!*c || *c == ',') { while (!w.empty() && w.back() == ' ') w.pop_back(); if (!w.empty()) out.push_back(Norm(w)); w.clear(); if (!*c) break; }
			else if (!(w.empty() && *c == ' ')) w += *c;
		}
	}

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

	struct Skeleton {
		bool ok = false;
		bool actor = false;
		bool measured = false;
		const void* boneData = nullptr; UInt32 bones = 0;
		float pts[12][4] = {};
		float hipZ = 0.0f, kneeZ = 0.0f, groundZ = 0.0f;
		int bone[5] = { -1, -1, -1, -1, -1 };
	};
	inline std::unordered_map<const void*, Skeleton> Skeletons;
	inline UInt32 SkeletonsLogged = 0;
	inline float LimbDistance(const float* p, const float* a, const float* b) {
		const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
		const float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
		const float t = std::clamp(len2 > 1e-4f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2 : 0.0f, 0.0f, 1.0f);
		const float d[3] = { ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t };
		return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - (a[3] + (b[3] - a[3]) * t);
	}
	inline float Smooth01(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
	inline float SkeletonMask(const Skeleton& k, const float* p) {
		static const int seg[8][2] = { {0, 1}, {1, 2}, {2, 3}, {4, 5}, {5, 6}, {6, 7}, {8, 9}, {10, 11} };
		float d = 1e30f;
		for (const auto& sg : seg) d = (std::min)(d, LimbDistance(p, k.pts[sg[0]], k.pts[sg[1]]));
		const float off = Smooth01((d - S.LimbGap) / (std::max)(S.LimbFade, 0.1f));
		const float h = Smooth01((k.hipZ - p[2]) / (std::max)(k.hipZ - k.kneeZ, 1.0f));
		return off * h * Smooth01((p[2] - k.groundZ) / 6.0f);
	}
	inline void MeasureLegs(Skeleton& k, NiGeometry* geometry) {
		const NiGeometryData* gd = geometry->geomData;
		if (!gd || !(gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) || !gd->m_pkVertex) return;
		static const int seg[4][2] = { {0, 1}, {1, 2}, {4, 5}, {5, 6} };
		static const float rmin[3] = { 5.0f, 3.5f, 3.0f }, rmax[3] = { 10.0f, 8.0f, 6.5f };
		std::vector<float> samples[7];
		for (UInt16 v = 0; v < gd->m_usVertices; v++) {
			const float* p = &gd->m_pkVertex[v].x;
			if (p[2] > k.hipZ + 2.0f || p[2] < k.groundZ + 6.0f) continue;
			float best = 1e30f; int joint = -1;
			for (const auto& sg : seg) {
				const float* a = k.pts[sg[0]]; const float* b = k.pts[sg[1]];
				const float ab[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, ap[3] = { p[0] - a[0], p[1] - a[1], p[2] - a[2] };
				const float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
				const float t = std::clamp(len2 > 1e-4f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2 : 0.0f, 0.0f, 1.0f);
				const float dx = ap[0] - ab[0] * t, dy = ap[1] - ab[1] * t, dz = ap[2] - ab[2] * t;
				const float d = sqrtf(dx * dx + dy * dy + dz * dz);
				if (d < best) { best = d; joint = t < 0.5f ? sg[0] : sg[1]; }
			}
			if (joint >= 0) samples[joint].push_back(best);
		}
		for (int j : { 0, 1, 2, 4, 5, 6 }) {
			std::vector<float>& ds = samples[j];
			if (ds.size() < 20) continue;
			std::nth_element(ds.begin(), ds.begin() + ds.size() / 10, ds.end());
			const int kind = j % 4;
			k.pts[j][3] = std::clamp(ds[ds.size() / 10], rmin[kind], rmax[kind]);
		}
		k.measured = true;
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
		const UInt32 count = (std::min)(sd->Bones, (UInt32)512);
		for (UInt32 b = 0; b < count; b++) {
			const NiAVObject* bone = si->BoneObjects[b];
			const char* name = bone ? bone->m_pcName : nullptr;
			if (!name) continue;
			if (!_strnicmp(name, "bip01", 5)) k.actor = true;
			if (!_stricmp(name, "bip01 pelvis")) k.bone[0] = (int)b;
			else if (!_stricmp(name, "bip01 spine") && k.bone[0] < 0) k.bone[0] = (int)b;
			for (int n = 0; n < 12; n++) {
				if (found[n] || _stricmp(name, names[n])) continue;
				if (n == 0) k.bone[1] = (int)b; else if (n == 1) k.bone[2] = (int)b; else if (n == 4) k.bone[3] = (int)b; else if (n == 5) k.bone[4] = (int)b;
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
				if (!found[leg * 4 + 2]) { for (int i = 0; i < 3; i++) ankle[i] = knee[i] + (knee[i] - hip[i]) * 0.95f; ankle[3] = radius[2]; }
				if (!found[leg * 4 + 3]) { toe[0] = ankle[0]; toe[1] = ankle[1] + 11.6f; toe[2] = ankle[2] - 5.9f; toe[3] = radius[3]; }
				for (int i = 0; i < 3; i++) toe[i] += (toe[i] - ankle[i]) * 0.35f;
			}
			for (int arm = 0; arm < 2; arm++) {
				if (!found[8 + arm * 2] || !found[9 + arm * 2])
					for (int n = 8 + arm * 2; n < 10 + arm * 2; n++) { k.pts[n][0] = k.pts[n][1] = 0.0f; k.pts[n][2] = -1e5f; k.pts[n][3] = 0.0f; }
			}
			k.hipZ = 0.5f * (k.pts[0][2] + k.pts[4][2]);
			k.kneeZ = 0.5f * (k.pts[1][2] + k.pts[5][2]);
			k.groundZ = 0.5f * (k.pts[2][2] + k.pts[6][2]) - 6.0f;
			k.ok = k.hipZ - k.kneeZ > 5.0f;
			if (!std::isfinite(k.hipZ) || !std::isfinite(k.kneeZ) || !std::isfinite(k.groundZ) || k.hipZ - k.groundZ > (float)ClothWeights::kMaxLegs) k.ok = false;
			if (k.ok) MeasureLegs(k, geometry);
		}
		if (SkeletonsLogged < 24 || S.LogMatches) {
			SkeletonsLogged++;
			const NiGeometryData* gd = geometry->geomData;
			UInt32 moving = 0, total = 0;
			if (k.ok && gd && (gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) && gd->m_pkVertex)
				for (UInt16 v = 0; v < gd->m_usVertices; v++, total++) if (SkeletonMask(k, &gd->m_pkVertex[v].x) > 0.5f) moving++;
			Logger::Log("Cloth wind: skeleton of '%s' (%u bones%s): %s; hips z %.1f, knees z %.1f, ankles z %.1f %.1f, leg radii %s %.1f %.1f %.1f / %.1f %.1f %.1f, elbows %s%s%s; %u of %u vertices (kept) move > half",
				geometry->m_pcName ? geometry->m_pcName : "?", sd->Bones, k.actor ? ", human" : "", k.ok ? "used" : (k.actor ? "NOT used: no legs, stays still" : "NOT used (no upright legs: SkinMask)"),
				k.hipZ, k.kneeZ, k.pts[2][2], k.pts[6][2], k.measured ? "measured" : "fixed", k.pts[0][3], k.pts[1][3], k.pts[2][3], k.pts[4][3], k.pts[5][3], k.pts[6][3],
				found[8] && found[10] ? "found" : "missing", found[2] && found[6] ? "" : " (ankles guessed)", found[3] && found[7] ? "" : " (toes guessed)", moving, total);
		}
		return Skeletons[sd] = k;
	}

	struct PartBuf { const void* partition = nullptr; const UInt16* vertexMap = nullptr; UInt16 vertices = 0; UInt32 base = 0; IDirect3DVertexBuffer9* vb = nullptr; };
	struct MeshWeights {
		const void* skinData = nullptr; const void* skinPartition = nullptr; const void* partitions = nullptr; UInt32 partitionCount = 0;
		UInt16 vertices = 0;
		int state = 0;
		bool buffers = false;
		std::vector<float> w;
		std::vector<PartBuf> parts;
		float center[2] = {};
		UInt32 lastFrame = 0;
		ClothWalk::Ring ring;
	};
	inline std::unordered_map<const void*, MeshWeights> Weights;
	inline MeshWeights* ActiveWeights = nullptr;
	inline float ActiveC248[4] = {};
	inline bool DrawHookOn = false;
	inline UInt32 WeightsMadeNow = 0, WeightsLogged = 0;
	inline UInt32 StatWeighted = 0, StatFallback = 0, StatNoDecl = 0;

	inline void ReleaseBuffers(MeshWeights& m) {
		for (PartBuf& p : m.parts) if (p.vb) { p.vb->Release(); p.vb = nullptr; }
		m.parts.clear(); m.buffers = false;
	}
	inline bool FillPart(IDirect3DDevice9* device, const MeshWeights& m, PartBuf& p) {
		if (p.vb) { p.vb->Release(); p.vb = nullptr; }
		if (!p.vertexMap || !p.vertices || p.base > 65535) return false;
		const UINT bytes = (p.base + p.vertices) * 12;
		void* data = nullptr;
		if (FAILED(device->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &p.vb, NULL)) || !p.vb) { p.vb = nullptr; return false; }
		if (FAILED(p.vb->Lock(0, 0, &data, 0)) || !data) { p.vb->Release(); p.vb = nullptr; return false; }
		float* f = (float*)data;
		for (UInt32 i = 0; i < p.base * 3; i++) f[i] = 0.0f;
		const bool ring = m.ring.u.size() == m.w.size() && m.ring.swing.size() == m.w.size();
		for (UInt32 i = 0; i < p.vertices; i++) {
			const UInt16 v = p.vertexMap[i];
			f[(p.base + i) * 3] = v < m.w.size() ? m.w[v] : 0.0f;
			f[(p.base + i) * 3 + 1] = ring && v < m.w.size() ? m.ring.u[v] : 0.0f;
			f[(p.base + i) * 3 + 2] = ring && v < m.w.size() ? m.ring.swing[v] : 0.0f;
		}
		p.vb->Unlock();
		return true;
	}
	inline const char* TrianglesOf(NiGeometry* geometry, const NiSkinPartition* sp, std::vector<UInt16>& tris) {
		tris.clear();
		const NiGeometryData* gd = geometry->geomData;
		const UInt16 nv = gd->m_usVertices;
		auto addStrip = [&](const UInt16* idx, UInt32 len, const UInt16* map, UInt32 mapCount) {
			for (UInt32 k = 0; k + 2 < len; k++) {
				UInt16 a = idx[k], b = idx[k + 1], c = idx[k + 2];
				if (map) { if (a >= mapCount || b >= mapCount || c >= mapCount) return; a = map[a]; b = map[b]; c = map[c]; }
				if (a == b || b == c || a == c || a >= nv || b >= nv || c >= nv) continue;
				if (k & 1) std::swap(b, c);
				tris.push_back(a); tris.push_back(b); tris.push_back(c);
			}
		};
		const UInt32 vt = *(const UInt32*)gd;
		if (vt == 0x109DAC4) {
			const UInt16 count = *(const UInt16*)((const char*)gd + 0x40);
			const UInt16* list = *(UInt16* const*)((const char*)gd + 0x48);
			if (list && count) {
				for (UInt32 i = 0; i < (UInt32)count * 3; i += 3)
					if (list[i] < nv && list[i + 1] < nv && list[i + 2] < nv) { tris.push_back(list[i]); tris.push_back(list[i + 1]); tris.push_back(list[i + 2]); }
				if (!tris.empty()) return "the mesh's triangle list";
			}
		}
		else if (vt == 0x109D8AC) {
			const UInt16 strips = *(const UInt16*)((const char*)gd + 0x44);
			const UInt16* lengths = *(UInt16* const*)((const char*)gd + 0x48);
			const UInt16* lists = *(UInt16* const*)((const char*)gd + 0x4C);
			if (strips && lengths && lists) {
				UInt32 at = 0;
				for (UInt16 s = 0; s < strips; s++) { addStrip(lists + at, lengths[s], nullptr, 0); at += lengths[s]; }
				if (!tris.empty()) return "the mesh's strips";
			}
		}
		if (!sp || !sp->Partitions) return nullptr;
		for (UInt32 p = 0; p < sp->PartitionsCount; p++) {
			const NiSkinPartition::Partition& part = sp->Partitions[p];
			const UInt16* map = *(UInt16* const*)((const char*)&part + 0x0C);
			const UInt16* list = *(UInt16* const*)((const char*)&part + 0x14);
			const UInt16* lengths = *(UInt16* const*)((const char*)&part + 0x18);
			if (!map || !list) continue;
			if (part.Strips && lengths) {
				UInt32 at = 0;
				for (UInt16 s = 0; s < part.Strips; s++) { addStrip(list + at, lengths[s], map, part.Vertices); at += lengths[s]; }
			}
			else
				for (UInt32 i = 0; i < (UInt32)part.Triangles * 3; i += 3) {
					const UInt16 a = list[i], b = list[i + 1], c = list[i + 2];
					if (a >= part.Vertices || b >= part.Vertices || c >= part.Vertices) continue;
					const UInt16 ma = map[a], mb = map[b], mc = map[c];
					if (ma < nv && mb < nv && mc < nv) { tris.push_back(ma); tris.push_back(mb); tris.push_back(mc); }
				}
		}
		return tris.empty() ? nullptr : "the skin partitions' triangles";
	}
	inline MeshWeights* WeightsOf(IDirect3DDevice9* device, NiGeometry* geometry, const Skeleton& k, const ClothWeights::Result* pre = nullptr) {
		NiGeometryData* gd = geometry->geomData;
		const NiSkinInstance* si = geometry->skinInstance;
		const NiSkinPartition* sp = si ? si->SkinPartition : nullptr;
		if (!gd || !si || !sp || !sp->Partitions || !sp->PartitionsCount) return nullptr;
		MeshWeights& m = Weights[gd];
		if (m.state != 0 && (m.vertices != gd->m_usVertices || m.skinData != si->SkinData || m.skinPartition != sp || m.partitions != sp->Partitions || m.partitionCount != sp->PartitionsCount)) {
			ReleaseBuffers(m); m = MeshWeights();
		}
		m.lastFrame = FrameId;
		if (m.state < 0) return nullptr;
		if (m.state == 0) {
			const bool given = pre && pre->weight.size() == gd->m_usVertices && pre->label.size() == gd->m_usVertices;
			if (!given && WeightsMadeNow >= 2) return nullptr;
			if (!given) WeightsMadeNow++;
			m.vertices = gd->m_usVertices; m.skinData = si->SkinData; m.skinPartition = sp; m.partitions = sp->Partitions; m.partitionCount = sp->PartitionsCount;
			m.state = -1;
			std::vector<UInt16> tris;
			const char* from = given ? "the auto-detect measure (worker)" : ((gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) && gd->m_pkVertex ? TrianglesOf(geometry, sp, tris) : nullptr);
			ClothWeights::Input in;
			ClothWeights::Result r;
			LARGE_INTEGER q0 = {}, q1 = {}, qf = {};
			QueryPerformanceCounter(&q0);
			if (given) { r = *pre; m.w = r.weight; m.state = 1; }
			else if (from) {
				in.pos = &gd->m_pkVertex[0].x; in.vertices = gd->m_usVertices; in.tris = tris.data(); in.triangles = (uint32_t)(tris.size() / 3);
				for (int leg = 0; leg < 2; leg++) for (int j = 0; j < 3; j++) for (int c = 0; c < 4; c++) in.legs[leg][j][c] = k.pts[leg * 4 + j][c];
				in.hipZ = k.hipZ; in.kneeZ = k.kneeZ; in.groundZ = k.groundZ;
				if (ClothWeights::Compute(in, r)) { m.w = std::move(r.weight); m.state = 1; }
			}
			m.center[0] = 0.5f * (k.pts[0][0] + k.pts[4][0]); m.center[1] = 0.5f * (k.pts[0][1] + k.pts[4][1]);
			if (m.state > 0)
				ClothWalk::BuildRing(&gd->m_pkVertex[0].x, gd->m_usVertices, m.w.data(), r.label.data(), m.center, k.hipZ, m.ring);
			if (WeightsLogged < 24 || S.LogMatches) {
				WeightsLogged++;
				QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf);
				const double ms = qf.QuadPart ? (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart : 0.0;
				if (m.state > 0)
					Logger::Log("Cloth wind: cloth weights of '%s': %u vertices (%u welded): body %u, cloth %u, torso %u; cuts %u (tubes %u, pads %u, cloth %u), crotch z %.1f; from %s, %u triangles; %.2f ms; hem ring %s",
						geometry->m_pcName ? geometry->m_pcName : "?", (unsigned)gd->m_usVertices, r.welded, r.body, r.cloth, r.torso, r.cuts, r.tubes, r.pads, r.clothCuts, r.crotch, from, (unsigned)(tris.size() / 3), ms,
						m.ring.ok ? "made" : "none (no coat below the hips)");
				else
					Logger::Log("Cloth wind: cloth weights of '%s': NOT made (%s): the skeleton mask (4) stays", geometry->m_pcName ? geometry->m_pcName : "?",
						!(gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) || !gd->m_pkVertex ? "the game did not keep its vertices" : (!from ? "no triangles kept" : "the weights failed"));
			}
			if (m.state < 0) return nullptr;
		}
		if (!m.buffers) {
			if (!device) return nullptr;
			m.parts.assign(sp->PartitionsCount, PartBuf());
			bool all = true;
			for (UInt32 p = 0; p < sp->PartitionsCount; p++) {
				const NiSkinPartition::Partition& part = sp->Partitions[p];
				PartBuf& pb = m.parts[p];
				pb.partition = &part;
				pb.vertexMap = *(UInt16* const*)((const char*)&part + 0x0C);
				pb.vertices = part.Vertices;
				pb.base = part.BuffData ? part.BuffData->BaseVertexIndex : 0;
				if (!FillPart(device, m, pb)) all = false;
			}
			if (!all) {
				if (WeightsLogged < 48 || S.LogMatches) { WeightsLogged++; Logger::Log("Cloth wind: cloth weights of '%s': partition buffers NOT made (vertex map missing?): the skeleton mask (4) stays", geometry->m_pcName ? geometry->m_pcName : "?"); }
				ReleaseBuffers(m); m.state = -1;
				return nullptr;
			}
			m.buffers = true;
		}
		return &m;
	}
	struct DeclPair { IDirect3DVertexDeclaration9* orig; IDirect3DVertexDeclaration9* aug; };
	inline std::vector<DeclPair> Decls;
	inline IDirect3DVertexDeclaration9* Augmented(IDirect3DDevice9* device, IDirect3DVertexDeclaration9* orig) {
		if (!orig) return nullptr;
		for (const DeclPair& d : Decls) if (d.orig == orig) return d.aug;
		D3DVERTEXELEMENT9 el[MAXD3DDECLLENGTH + 2] = {};
		UINT n = MAXD3DDECLLENGTH + 1;
		IDirect3DVertexDeclaration9* aug = nullptr;
		if (SUCCEEDED(orig->GetDeclaration(el, &n)) && n >= 1 && n <= MAXD3DDECLLENGTH) {
			bool free = true;
			for (UINT i = 0; i + 1 < n; i++) if (el[i].Stream != 0 || (el[i].Usage == D3DDECLUSAGE_TEXCOORD && el[i].UsageIndex == 7)) free = false;
			if (free) {
				el[n - 1] = { 1, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 7 };
				el[n] = D3DDECL_END();
				if (FAILED(device->CreateVertexDeclaration(el, &aug))) aug = nullptr;
			}
		}
		orig->AddRef();
		Decls.push_back({ orig, aug });
		if (Decls.size() > 64) { if (Decls.front().aug) Decls.front().aug->Release(); Decls.front().orig->Release(); Decls.erase(Decls.begin()); }
		return aug;
	}
	struct Walker {
		ClothWalk::State st;
		UInt32 frame = 0;
		double clock = 0.0;
		const void* data = nullptr;
		bool ok = false;
		double d[ClothWalk::kRing][3] = {};
		double pelvis[3][4] = {};
		double legs[6][4] = {};
		double lastPos[3] = {};
		float hang = -1.0f;
		int pelvisBone = -1;
	};
	inline std::unordered_map<const void*, Walker> Walkers;
	inline Walker* ActiveWalk = nullptr;
	inline const NiSkinInstance* ActiveSkin = nullptr;
	inline int ActiveBonesBase = 44;
	inline UInt32 StatWalkDraws = 0, StatWalkNoSpace = 0, WalkChecksLogged = 0;

	inline bool BoneXf(const NiSkinInstance* si, int b, double out[3][4]) {
		const NiSkinData* sd = si ? si->SkinData : nullptr;
		if (b < 0 || !sd || !sd->BoneData || !si->BoneObjects || (UInt32)b >= sd->Bones || !si->BoneObjects[b]) return false;
		const NiTransform& W = si->BoneObjects[b]->m_worldTransform;
		const NiTransform& S = sd->BoneData[b].SkinToBone;
		const double sp[3] = { S.pos.x, S.pos.y, S.pos.z }, wp[3] = { W.pos.x, W.pos.y, W.pos.z };
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				double acc = 0.0;
				for (int m = 0; m < 3; m++) acc += (double)W.rot.data[i][m] * S.rot.data[m][j];
				out[i][j] = acc * W.scale * S.scale;
			}
			out[i][3] = ((double)W.rot.data[i][0] * sp[0] + (double)W.rot.data[i][1] * sp[1] + (double)W.rot.data[i][2] * sp[2]) * W.scale + wp[i];
		}
		return true;
	}
	inline double Smooth01d(double x) { x = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); return x * x * (3.0 - 2.0 * x); }

	inline Walker* WalkOf(NiGeometry* geometry, const Skeleton& k, const MeshWeights& mw) {
		const NiSkinInstance* si = geometry->skinInstance;
		if (!si || !mw.ring.ok) return nullptr;
		for (int b : k.bone) if (b < 0) return nullptr;
		Walker& w = Walkers[geometry];
		if (w.data != geometry->geomData) { w = Walker(); w.data = geometry->geomData; }
		if (w.frame == FrameId && w.ok) return &w;
		w.frame = FrameId; w.ok = false;
		double tl[3][4], cl[3][4], tr[3][4], cr[3][4];
		if (!BoneXf(si, k.bone[0], w.pelvis) || !BoneXf(si, k.bone[1], tl) || !BoneXf(si, k.bone[2], cl) || !BoneXf(si, k.bone[3], tr) || !BoneXf(si, k.bone[4], cr)) return nullptr;
		w.pelvisBone = k.bone[0];
		const double (*bone[6])[4] = { tl, tl, cl, tr, tr, cr };
		static const int joint[6] = { 0, 1, 2, 4, 5, 6 };
		for (int j = 0; j < 6; j++) {
			const float* q = k.pts[joint[j]];
			for (int i = 0; i < 3; i++) w.legs[j][i] = bone[j][i][0] * q[0] + bone[j][i][1] * q[1] + bone[j][i][2] * q[2] + bone[j][i][3];
			w.legs[j][3] = q[3];
		}
		const double dt = ClockSeconds - w.clock;
		w.clock = ClockSeconds;
		const double jx = w.pelvis[0][3] - w.lastPos[0], jy = w.pelvis[1][3] - w.lastPos[1], jz = w.pelvis[2][3] - w.lastPos[2];
		if (jx * jx + jy * jy + jz * jz > 400.0 * 400.0) w.st.valid = false;
		for (int i = 0; i < 3; i++) w.lastPos[i] = w.pelvis[i][3];
		ClothWalk::Xf x;
		for (int i = 0; i < 3; i++) { for (int j = 0; j < 3; j++) x.r[i][j] = w.pelvis[i][j]; x.t[i] = w.pelvis[i][3]; }
		ClothWalk::Params prm;
		prm.spring = S.WalkSpring; prm.damping = S.WalkDamping; prm.drag = S.WalkDrag; prm.legMargin = S.WalkLegMargin; prm.maxSwing = S.WalkMaxSwing;
		prm.dragCap = S.WalkDragCap; prm.maxAngle = S.WalkMaxAngle; prm.hemSmooth = S.WalkHemSmooth;
		if (!w.st.valid || dt > 1e-5) ClothWalk::Step(w.st, mw.ring, x, w.legs, dt, prm, w.d);
		double worst = -1.0;
		for (int leg = 0; leg < 2; leg++) {
			const float* hb = k.pts[leg * 4]; const float* kb = k.pts[leg * 4 + 1];
			const double rb[3] = { (double)kb[0] - hb[0], (double)kb[1] - hb[1], (double)kb[2] - hb[2] };
			double rest[3], cur[3];
			for (int i = 0; i < 3; i++) { rest[i] = w.pelvis[i][0] * rb[0] + w.pelvis[i][1] * rb[1] + w.pelvis[i][2] * rb[2]; cur[i] = w.legs[leg * 3 + 1][i] - w.legs[leg * 3][i]; }
			const double lr = std::sqrt(rest[0] * rest[0] + rest[1] * rest[1] + rest[2] * rest[2]), lc = std::sqrt(cur[0] * cur[0] + cur[1] * cur[1] + cur[2] * cur[2]);
			const double cosLeg = lr > 1e-6 && lc > 1e-6 ? (rest[0] * cur[0] + rest[1] * cur[1] + rest[2] * cur[2]) / (lr * lc) : 1.0;
			worst = (std::max)(worst, cosLeg);
		}
		const double angle = std::acos(std::clamp(worst, -1.0, 1.0)) * 57.29578;
		const float target = S.WalkHang * (float)(1.0 - Smooth01d((angle - 45.0) / 25.0));
		if (w.hang < 0.0f || dt > 0.25) w.hang = target;
		else if (dt > 0.0) w.hang += (target - w.hang) * (float)(1.0 - std::exp(-dt / 0.15));
		w.ok = true;
		return &w;
	}
	inline bool WalkConstants(IDirect3DDevice9* device, const NiSkinPartition::Partition* part) {
		const Walker* w = ActiveWalk;
		if (!w || !ActiveSkin || !part || !part->pBones || !part->Bones) return false;
		double M[3][4];
		if (!BoneXf(ActiveSkin, part->pBones[0], M)) return false;
		float R[12];
		if (FAILED(device->GetVertexShaderConstantF(ActiveBonesBase, R, 3))) return false;
		const double a = M[0][0], b = M[0][1], c = M[0][2], d = M[1][0], e = M[1][1], f = M[1][2], g = M[2][0], h = M[2][1], i = M[2][2];
		const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
		if (std::fabs(det) < 1e-9) return false;
		const double inv[3][3] = { { (e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det },
								   { (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det },
								   { (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det } };
		double Mi[3][4];
		for (int r = 0; r < 3; r++) { for (int q = 0; q < 3; q++) Mi[r][q] = inv[r][q]; Mi[r][3] = -(inv[r][0] * M[0][3] + inv[r][1] * M[1][3] + inv[r][2] * M[2][3]); }
		double T[3][4];
		for (int r = 0; r < 3; r++) {
			for (int q = 0; q < 4; q++) T[r][q] = R[r * 4] * Mi[0][q] + R[r * 4 + 1] * Mi[1][q] + R[r * 4 + 2] * Mi[2][q];
			T[r][3] += R[r * 4 + 3];
		}
		const double scale = std::cbrt(std::fabs(T[0][0] * (T[1][1] * T[2][2] - T[1][2] * T[2][1]) - T[0][1] * (T[1][0] * T[2][2] - T[1][2] * T[2][0]) + T[0][2] * (T[1][0] * T[2][1] - T[1][1] * T[2][0])));
		float k[26][4] = {};
		for (int r = 0; r < 3; r++) {
			for (int q = 0; q < 4; q++) k[r][q] = (float)(T[r][0] * w->pelvis[0][q] + T[r][1] * w->pelvis[1][q] + T[r][2] * w->pelvis[2][q]);
			k[r][3] += (float)T[r][3];
		}
		for (int n = 0; n < ClothWalk::kRing; n++)
			for (int r = 0; r < 3; r++) k[3 + n][r] = (float)(T[r][0] * w->d[n][0] + T[r][1] * w->d[n][1] + T[r][2] * w->d[n][2]);
		k[19][0] = 1.0f; k[19][1] = w->hang;
		for (int j = 0; j < 6; j++) {
			for (int r = 0; r < 3; r++) k[20 + j][r] = (float)(T[r][0] * w->legs[j][0] + T[r][1] * w->legs[j][1] + T[r][2] * w->legs[j][2] + T[r][3]);
			k[20 + j][3] = (float)(w->legs[j][3] * scale);
		}
		device->SetVertexShaderConstantF(200, &k[0][0], 26);
		if (S.LogMatches && WalkChecksLogged < 6) {
			for (UInt16 sl = 0; sl < part->Bones; sl++) {
				if (part->pBones[sl] != w->pelvisBone) continue;
				float P[12];
				if (SUCCEEDED(device->GetVertexShaderConstantF(ActiveBonesBase + sl * 3, P, 3))) {
					float worst = 0.0f;
					for (int r = 0; r < 3; r++) for (int q = 0; q < 4; q++) worst = (std::max)(worst, std::fabs(P[r * 4 + q] - k[r][q]));
					WalkChecksLogged++;
					Logger::Log("Cloth wind: walk space check (%s pass): the pelvis from the bone palette vs computed: largest difference %.4f (translation %.1f %.1f %.1f), hang %.2f, ring point 0 off by %.1f %.1f %.1f",
						ActiveBonesBase == 9 ? "shadow" : "lit", worst, P[3], P[7], P[11], w->hang, k[3][0], k[3][1], k[3][2]);
				}
				break;
			}
		}
		return true;
	}

	typedef void(__thiscall* DrawPartitionFn)(void*, NiGeometryBufferData*, NiSkinPartition::Partition*, void*);
	inline const DrawPartitionFn DrawPartitionOriginal = (DrawPartitionFn)0x00E6D310;
	inline void DrawPartition(void* renderer, NiGeometryBufferData* buffer, NiSkinPartition::Partition* partition, void* unused) {
		MeshWeights* m = ActiveWeights;
		IDirect3DDevice9* device = TheRenderManager ? TheRenderManager->device : nullptr;
		if (!m || !device || !partition) { DrawPartitionOriginal(renderer, buffer, partition, unused); return; }
		PartBuf* pb = nullptr;
		for (PartBuf& p : m->parts) if (p.partition == partition) { pb = &p; break; }
		if (pb && buffer && buffer->BaseVertexIndex != pb->base) {
			pb->base = buffer->BaseVertexIndex;
			if (!FillPart(device, *m, *pb)) pb = nullptr;
		}
		IDirect3DVertexDeclaration9* decl = nullptr;
		IDirect3DVertexDeclaration9* aug = nullptr;
		if (pb && pb->vb && buffer && buffer->StreamCount <= 1 && !buffer->FVF) {
			device->GetVertexDeclaration(&decl);
			aug = Augmented(device, decl);
		}
		if (!aug) {
			if (decl) decl->Release();
			if (pb) StatNoDecl++;
			StatFallback++;
			float c4[4] = { ActiveC248[0], 4.0f, ActiveC248[2], ActiveC248[3] };
			device->SetVertexShaderConstantF(248, c4, 1);
			DrawPartitionOriginal(renderer, buffer, partition, unused);
			device->SetVertexShaderConstantF(248, ActiveC248, 1);
			return;
		}
		if (ActiveWalk) {
			if (WalkConstants(device, partition)) StatWalkDraws++;
			else { StatWalkNoSpace++; const float off[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; device->SetVertexShaderConstantF(219, off, 1); }
		}
		IDirect3DVertexBuffer9* old = nullptr; UINT oldOffset = 0, oldStride = 0;
		device->GetStreamSource(1, &old, &oldOffset, &oldStride);
		device->SetStreamSource(1, pb->vb, 0, 12);
		device->SetVertexDeclaration(aug);
		DrawPartitionOriginal(renderer, buffer, partition, unused);
		device->SetVertexDeclaration(decl);
		device->SetStreamSource(1, old, oldOffset, oldStride);
		if (old) old->Release();
		decl->Release();
		StatWeighted++;
	}
	inline void __fastcall DrawPartitionHook(void* renderer, void* edx, NiGeometryBufferData* buffer, NiSkinPartition::Partition* partition, void* unused) {
		DrawPartition(renderer, buffer, partition, unused);
	}

	struct Job {
		virtual ~Job() {}
		virtual bool Run() = 0;
		bool ok = false;
		double ms = 0.0;
		std::atomic<bool> done{ false };
	};
	struct StillJob : Job {
		std::vector<float> pos, nrm;
		std::vector<uint16_t> tris;
		ClothStatic::Result r;
		bool Run() override {
			ClothStatic::Input in;
			in.pos = pos.data(); in.nrm = nrm.empty() ? nullptr : nrm.data(); in.vertices = (uint32_t)(pos.size() / 3);
			in.tris = tris.data(); in.triangles = (uint32_t)(tris.size() / 3);
			return ClothStatic::Compute(in, r);
		}
	};
	struct StillMesh {
		const void* data = nullptr; UInt16 vertices = 0;
		int state = 0;
		std::shared_ptr<StillJob> job;
		std::vector<float> w, side;
		IDirect3DVertexBuffer9* vb = nullptr; UInt32 base = 0;
		UInt32 lastFrame = 0;
		std::string name;
	};
	inline std::unordered_map<const void*, StillMesh> Stills;
	inline StillMesh* ActiveStill = nullptr;
	inline bool StillHookOn = false;
	inline UInt32 StillLogged = 0, StatStillDraws = 0, StatStillMissed = 0;
	struct ClothWorker { std::mutex m; std::condition_variable cv; std::deque<std::shared_ptr<Job>> q; bool started = false; };
	inline ClothWorker& Worker() { static ClothWorker* w = new ClothWorker(); return *w; }
	inline void Submit(const std::shared_ptr<Job>& job) {
		ClothWorker& W = Worker();
		{
			std::lock_guard<std::mutex> lock(W.m);
			W.q.push_back(job);
			if (!W.started) {
				W.started = true;
				std::thread([] {
					ClothWorker& W = Worker();
					for (;;) {
						std::shared_ptr<Job> j;
						{
							std::unique_lock<std::mutex> lock(W.m);
							W.cv.wait(lock, [&] { return !W.q.empty(); });
							j = W.q.front(); W.q.pop_front();
						}
						LARGE_INTEGER q0 = {}, q1 = {}, qf = {};
						QueryPerformanceCounter(&q0);
						try { j->ok = j->Run(); }
						catch (...) { j->ok = false; }
						QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf);
						j->ms = qf.QuadPart ? (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)qf.QuadPart : 0.0;
						j->done = true;
					}
				}).detach();
			}
		}
		W.cv.notify_one();
	}
	inline bool FillStill(IDirect3DDevice9* device, StillMesh& m, UInt32 base) {
		if (m.vb) { m.vb->Release(); m.vb = nullptr; }
		if (base > 262144 || !m.vertices) return false;
		void* data = nullptr;
		if (FAILED(device->CreateVertexBuffer((base + m.vertices) * 12, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &m.vb, NULL)) || !m.vb) { m.vb = nullptr; return false; }
		if (FAILED(m.vb->Lock(0, 0, &data, 0)) || !data) { m.vb->Release(); m.vb = nullptr; return false; }
		float* f = (float*)data;
		memset(f, 0, base * 12);
		for (UInt32 i = 0; i < m.vertices; i++) {
			f[(base + i) * 3] = i < m.w.size() ? m.w[i] : 0.0f;
			f[(base + i) * 3 + 1] = i < m.side.size() ? m.side[i] : 1.0f;
			f[(base + i) * 3 + 2] = 0.0f;
		}
		m.vb->Unlock();
		m.base = base;
		return true;
	}
	inline StillMesh* StillOf(NiGeometry* geometry, const char* texture) {
		NiGeometryData* gd = geometry->geomData;
		if (!gd) return nullptr;
		StillMesh& m = Stills[gd];
		if (m.state != 0 && m.vertices != gd->m_usVertices) {
			if (m.vb) m.vb->Release();
			m = StillMesh();
		}
		m.lastFrame = FrameId;
		if (m.state == 0) {
			m.data = gd; m.vertices = gd->m_usVertices; m.state = -1;
			m.name = std::string(geometry->m_pcName ? geometry->m_pcName : "?") + "' (" + (texture ? texture : "");
			std::vector<UInt16> tris;
			if ((gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) && gd->m_pkVertex && gd->m_usVertices >= 3 && TrianglesOf(geometry, nullptr, tris)) {
				auto job = std::make_shared<StillJob>();
				job->pos.assign(&gd->m_pkVertex[0].x, &gd->m_pkVertex[0].x + gd->m_usVertices * 3);
				if (gd->m_pkNormal) job->nrm.assign(&gd->m_pkNormal[0].x, &gd->m_pkNormal[0].x + gd->m_usVertices * 3);
				job->tris.assign(tris.begin(), tris.end());
				m.job = job; m.state = 1;
				Submit(job);
			}
			else if (StillLogged < 48 || S.LogMatches) { StillLogged++; Logger::Log("Cloth wind: still cloth '%s): NOT worked out (%s): it stays still", m.name.c_str(), !(gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) || !gd->m_pkVertex ? "the game did not keep its vertices" : "no triangles kept"); }
		}
		if (m.state == 1 && m.job && m.job->done) {
			const ClothStatic::Result& r = m.job->r;
			UInt32 moving = 0;
			if (m.job->ok && r.weight.size() == m.vertices) { m.w = r.weight; m.side = r.side; for (float v : m.w) if (v > 0.02f) moving++; }
			m.state = moving ? 2 : -1;
			if (StillLogged < 48 || S.LogMatches) {
				StillLogged++;
				if (m.job->ok)
					Logger::Log("Cloth wind: still cloth '%s): %u vertices (%u welded): %u pieces = %u poles/ropes, %u solid, %u tiny, %u cloth (%u two-sided of %u groups); cloth held by poles/ropes %u, the ground %u (at z %.1f%s), its top %u (%u flat sheets tied all round); free edges %u; %u of %u vertices move, %u > half; %.2f ms on the worker",
						m.name.c_str(), (unsigned)m.vertices, r.welded, r.pieces, r.rods, r.solids, r.tiny, r.sheets, r.layered, r.clusters, r.heldRigid, r.heldGround, r.groundZ, r.standing ? "" : ", hanging: none",
						r.heldTop, r.canopies, r.freeEdges, moving, (unsigned)m.vertices, r.moving, m.job->ms);
				else Logger::Log("Cloth wind: still cloth '%s): NOT worked out (the weights failed): it stays still", m.name.c_str());
			}
			m.job.reset();
		}
		return m.state == 2 ? &m : nullptr;
	}
	typedef void(__thiscall* OnlyRenderFn)(void*, NiGeometry*);
	inline OnlyRenderFn OnlyRenderShapeOriginal = (OnlyRenderFn)0x00E745A0, OnlyRenderStripsOriginal = (OnlyRenderFn)0x00E74840;
#if NVR_TEST_TOOLS
	inline UInt32 DiagFrame = 0, DiagLines = 0;
	inline void DiagStillDraw(IDirect3DDevice9* device, NiGeometry* geometry, const StillMesh* active) {
		if (!DiagFrame || FrameId < DiagFrame || FrameId > DiagFrame + 2 || DiagLines >= 900 || !device || !geometry || !geometry->geomData) return;
		auto it = Stills.find(geometry->geomData);
		if (it == Stills.end() || it->second.state != 2) return;
		DiagLines++;
		DWORD zenable = 0, zfunc = 0, zwrite = 0, colorwrite = 0, alphablend = 0, alphatest = 0;
		device->GetRenderState(D3DRS_ZENABLE, &zenable); device->GetRenderState(D3DRS_ZFUNC, &zfunc); device->GetRenderState(D3DRS_ZWRITEENABLE, &zwrite);
		device->GetRenderState(D3DRS_COLORWRITEENABLE, &colorwrite); device->GetRenderState(D3DRS_ALPHABLENDENABLE, &alphablend); device->GetRenderState(D3DRS_ALPHATESTENABLE, &alphatest);
		float c[4] = {};
		device->GetVertexShaderConstantF(248, c, 1);
		IDirect3DVertexShader9* vs = nullptr; device->GetVertexShader(&vs);
		IDirect3DSurface9* rt = nullptr; device->GetRenderTarget(0, &rt);
		Logger::Log("Cloth wind: still draw (frame %u) '%s): geometry %p, chosen %d, in a pass hook %d, c248 %.2f %.2f, z %u func %u write %u, colour write %X, blend %u, alpha test %u, VS %p %s (carries cloth %d), target %p | seen last frame %d, vanilla VS last frame %d",
			FrameId, it->second.name.c_str(), geometry, active == &it->second, PassGeometry == geometry, c[0], c[1], zenable, zfunc, zwrite, colorwrite, alphablend, alphatest, vs, CurrentVSName ? CurrentVSName : "?", CurrentVSCapable, rt,
			(int)SeenLast.count(geometry), (int)VanillaLast.count(geometry));
		if (vs) vs->Release();
		if (rt) rt->Release();
	}
#endif
	inline void OnlyRender(bool strips, void* renderer, NiGeometry* geometry) {
		const OnlyRenderFn original = strips ? OnlyRenderStripsOriginal : OnlyRenderShapeOriginal;
		StillMesh* m = ActiveStill;
		IDirect3DDevice9* device = TheRenderManager ? TheRenderManager->device : nullptr;
#if NVR_TEST_TOOLS
		DiagStillDraw(device, geometry, m);
#endif
		if (!m || !device || !geometry || geometry->geomData != m->data) { original(renderer, geometry); return; }
		const NiGeometryBufferData* buffer = geometry->geomData->m_pkBuffData;
		IDirect3DVertexDeclaration9* decl = nullptr;
		IDirect3DVertexDeclaration9* aug = nullptr;
		bool ready = buffer && buffer->VertCount == m->vertices && buffer->StreamCount <= 1 && !buffer->FVF;
		if (ready && (!m->vb || buffer->BaseVertexIndex != m->base)) ready = FillStill(device, *m, buffer->BaseVertexIndex);
		if (ready) { device->GetVertexDeclaration(&decl); aug = Augmented(device, decl); }
		if (!aug) {
			if (decl) decl->Release();
			StatStillMissed++;
			float c[4] = {}, zero[4] = {};
			device->GetVertexShaderConstantF(248, c, 1);
			device->SetVertexShaderConstantF(248, zero, 1);
			original(renderer, geometry);
			device->SetVertexShaderConstantF(248, c, 1);
			return;
		}
		IDirect3DVertexBuffer9* old = nullptr; UINT oldOffset = 0, oldStride = 0;
		device->GetStreamSource(1, &old, &oldOffset, &oldStride);
		device->SetStreamSource(1, m->vb, 0, 12);
		device->SetVertexDeclaration(aug);
		original(renderer, geometry);
		device->SetVertexDeclaration(decl);
		device->SetStreamSource(1, old, oldOffset, oldStride);
		if (old) old->Release();
		decl->Release();
		StatStillDraws++;
	}
	inline void __fastcall OnlyRenderShapeHook(void* renderer, void* edx, NiGeometry* geometry) { OnlyRender(false, renderer, geometry); }
	inline void __fastcall OnlyRenderStripsHook(void* renderer, void* edx, NiGeometry* geometry) { OnlyRender(true, renderer, geometry); }

	struct AutoJob : Job {
		std::vector<float> pos;
		std::vector<uint16_t> tris;
		ClothWeights::Input in;
		ClothWeights::Result r;
		bool Run() override {
			in.pos = pos.data(); in.vertices = (uint32_t)(pos.size() / 3); in.tris = tris.data(); in.triangles = (uint32_t)(tris.size() / 3);
			return ClothWeights::Compute(in, r);
		}
	};
	struct Verdict {
		int state = 0;
		UInt16 vertices = 0;
		float free = 0.0f;
		std::shared_ptr<AutoJob> job;
		ClothWeights::Result r;
		std::string name;
	};
	inline std::unordered_map<std::string, Verdict> Verdicts;
	struct VerdictRef { UInt16 vertices = 0; const void* texture = nullptr; Verdict* v = nullptr; };
	inline std::unordered_map<const void*, VerdictRef> VerdictOfData;
	inline UInt32 AutoLogged = 0;
	inline float AutoShareUsed = -1.0f;
	inline void LogVerdict(const Verdict& v, const char* why) {
		if (!S.LogAuto || AutoLogged >= 400) return;
		AutoLogged++;
		const ClothWeights::Result& r = v.state == 2 ? v.r : (v.job ? v.job->r : v.r);
		if (v.state == 2)
			Logger::Log("Cloth wind: auto: CLOTH %s: hangs free -- %.0f %% of its thigh band stands off the legs, %u of %u vertices move (%.1f %%), down to %.0f units below the crotch; %.2f ms on the worker",
				v.name.c_str(), v.free * 100.0f, r.moving, r.welded, r.welded ? 100.0 * r.moving / r.welded : 0.0, r.hemDrop, v.job ? v.job->ms : 0.0);
		else if (v.job && v.job->ok)
			Logger::Log("Cloth wind: auto: still %s: %s -- %.0f %% of its thigh band stands off the legs, %u of %u vertices would move (%.1f %%), down to %.0f units below the crotch",
				v.name.c_str(), why, v.free * 100.0f, r.moving, r.welded, r.welded ? 100.0 * r.moving / r.welded : 0.0, r.hemDrop);
		else Logger::Log("Cloth wind: auto: still %s: %s", v.name.c_str(), why);
	}
	inline const Verdict* AutoVerdictOf(NiGeometry* geometry, const NiSourceTexture* diffuse) {
		NiGeometryData* gd = geometry->geomData;
		if (!gd || !diffuse || !diffuse->ddsPath1) return nullptr;
		Verdict* v = nullptr;
		auto it = VerdictOfData.find(gd);
		if (it != VerdictOfData.end() && it->second.vertices == gd->m_usVertices && it->second.texture == diffuse) v = it->second.v;
		else {
			if (VerdictOfData.size() >= 4096) VerdictOfData.clear();
			const std::string path = Norm(diffuse->ddsPath1);
			const std::string key = path + "|" + (geometry->m_pcName ? geometry->m_pcName : "") + "|" + std::to_string(gd->m_usVertices);
			v = &Verdicts[key];
			VerdictOfData[gd] = { gd->m_usVertices, diffuse, v };
			if (v->state == 0) {
				v->vertices = gd->m_usVertices;
				v->name = std::string("'") + (geometry->m_pcName ? geometry->m_pcName : "?") + "' (" + diffuse->ddsPath1 + ", " + std::to_string(gd->m_usVertices) + " vertices)";
				v->state = -1;
				const Skeleton& k = SkeletonOf(geometry);
				std::vector<UInt16> tris;
				const NiSkinPartition* sp = geometry->skinInstance ? geometry->skinInstance->SkinPartition : nullptr;
				if (IsForcedOff(path)) LogVerdict(*v, "ForceOff");
				else if (ExcludedPart(path)) LogVerdict(*v, "its file name is a body part (ExcludeParts)");
				else if (!k.ok) LogVerdict(*v, k.actor ? "no legs in its skeleton" : "not on a human skeleton");
				else if (!(gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) || !gd->m_pkVertex || !TrianglesOf(geometry, sp, tris)) LogVerdict(*v, "the game did not keep its vertices or triangles");
				else {
					auto job = std::make_shared<AutoJob>();
					job->pos.assign(&gd->m_pkVertex[0].x, &gd->m_pkVertex[0].x + gd->m_usVertices * 3);
					job->tris.assign(tris.begin(), tris.end());
					for (int leg = 0; leg < 2; leg++) for (int j = 0; j < 3; j++) for (int c = 0; c < 4; c++) job->in.legs[leg][j][c] = k.pts[leg * 4 + j][c];
					job->in.hipZ = k.hipZ; job->in.kneeZ = k.kneeZ; job->in.groundZ = k.groundZ;
					job->in.pos = job->pos.data(); job->in.vertices = gd->m_usVertices;
					v->free = ClothWeights::FreeShare(job->in);
					if (v->free < (float)ClothWeights::kFreeFirst) {
					char why[96];
					snprintf(why, sizeof(why), "it hugs the legs (%.0f %% of its thigh band stands off them)", v->free * 100.0f);
					LogVerdict(*v, why);
				}
					else { v->job = job; v->state = 1; Submit(job); }
				}
			}
		}
		if (v->state == 1 && v->job && v->job->done) {
			const char* why = "the weights failed";
			if (v->job->ok) {
				const ClothWeights::Result& r = v->job->r;
				if (ClothWeights::HangsFree(r, S.AutoShare * 0.01)) { v->r = r; v->state = 2; why = ""; }
				else why = r.moving < ClothWeights::kAutoMoving || r.moving < S.AutoShare * 0.01 * r.welded ? "too little of it hangs free" : "it moves only at the hips";
			}
			if (v->state != 2) v->state = -1;
			LogVerdict(*v, why);
			v->job.reset();
		}
		return v;
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
		Interior = !exterior;
		IndoorWind = Interior && TheSettingManager->GetSettingI("Shaders.Cloth.Interiors", "Enabled") != 0;
		Exterior = exterior || IndoorWind;
		for (const auto& [g, capable] : TentativeNow)
			if (!SeenNow.count(g)) { SeenNow.insert(g); if (!capable) VanillaNow.insert(g); }
		TentativeNow.clear();
		SeenLast.swap(SeenNow); SeenNow.clear(); VanillaLast.swap(VanillaNow); VanillaNow.clear();
		VanillaVSLast.swap(VanillaVSNow); VanillaVSNow.clear();
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
		const std::string forceOffWas = ForceOffFrom;
		readList("ForceOff", ForceOffFrom, ForcedOff, nullptr);
		if (ForceOffFrom != forceOffWas) { VerdictOfData.clear(); Verdicts.clear(); AutoLogged = 0; }
		S.MotionDrag = std::clamp(TheSettingManager->GetSettingF(section, "MotionDrag"), 0.0f, 0.3f);
		S.MotionMax = std::clamp(TheSettingManager->GetSettingF(section, "MotionMax"), 0.0f, 80.0f);
		S.MotionSpring = std::clamp(TheSettingManager->GetSettingF(section, "MotionSpring"), 0.2f, 6.0f);
		S.MotionDamping = std::clamp(TheSettingManager->GetSettingF(section, "MotionDamping"), 0.05f, 1.0f);
		S.StrideSwing = std::clamp(TheSettingManager->GetSettingF(section, "StrideSwing"), 0.0f, 30.0f);
		S.StrideLength = std::clamp(TheSettingManager->GetSettingF(section, "StrideLength"), 40.0f, 600.0f);
		S.TurnLag = std::clamp(TheSettingManager->GetSettingF(section, "TurnLag"), 0.0f, 0.5f);
		S.MotionIndoors = TheSettingManager->GetSettingI(section, "MotionIndoors") != 0;
		S.UseSkeleton = TheSettingManager->GetSettingI(section, "UseSkeleton") != 0;
		S.UseWeights = TheSettingManager->GetSettingI(section, "UseWeights") != 0;
		S.WalkSim = TheSettingManager->GetSettingI(section, "WalkSim") != 0;
		S.WalkHang = std::clamp(TheSettingManager->GetSettingF(section, "WalkHang"), 0.0f, 1.0f);
		S.WalkSpring = std::clamp(TheSettingManager->GetSettingF(section, "WalkSpring"), 0.3f, 6.0f);
		S.WalkDamping = std::clamp(TheSettingManager->GetSettingF(section, "WalkDamping"), 0.02f, 1.0f);
		S.WalkDrag = std::clamp(TheSettingManager->GetSettingF(section, "WalkDrag"), 0.0f, 1.0f);
		S.WalkLegMargin = std::clamp(TheSettingManager->GetSettingF(section, "WalkLegMargin"), 0.0f, 10.0f);
		S.WalkMaxSwing = std::clamp(TheSettingManager->GetSettingF(section, "WalkMaxSwing"), 0.0f, 80.0f);
		S.WalkDragCap = std::clamp(TheSettingManager->GetSettingF(section, "WalkDragCap"), 20.0f, 2000.0f);
		S.WalkMaxAngle = std::clamp(TheSettingManager->GetSettingF(section, "WalkMaxAngle"), 5.0f, 89.0f);
		S.WalkHemSmooth = std::clamp(TheSettingManager->GetSettingF(section, "WalkHemSmooth"), 0.0f, 1.0f);
		S.StillWeights = TheSettingManager->GetSettingI(section, "StillWeights") != 0;
		S.AutoDetect = TheSettingManager->GetSettingI(section, "AutoDetect") != 0;
		S.AutoShare = std::clamp(TheSettingManager->GetSettingF(section, "AutoShare"), 0.0f, 50.0f);
		S.LogAuto = TheSettingManager->GetSettingI(section, "LogAuto") != 0;
		if (S.AutoShare != AutoShareUsed) { AutoShareUsed = S.AutoShare; VerdictOfData.clear(); Verdicts.clear(); AutoLogged = 0; }
		WeightsMadeNow = 0;
		S.LimbGap = std::clamp(TheSettingManager->GetSettingF(section, "LimbGap"), -5.0f, 30.0f);
		S.LimbFade = std::clamp(TheSettingManager->GetSettingF(section, "LimbFade"), 0.5f, 40.0f);
		FrameId++;
		S.SkinMask = std::clamp(TheSettingManager->GetSettingI(section, "SkinMask"), 0, 2);
		S.LegCut = std::clamp(TheSettingManager->GetSettingF(section, "LegCut"), 0.0f, 60.0f);
		S.PinFraction = std::clamp(TheSettingManager->GetSettingF(section, "PinFraction"), 0.0f, 0.9f);
		S.SkinSpace = std::clamp(TheSettingManager->GetSettingI(section, "SkinSpace"), 0, 2);
		S.CalmFactor = std::clamp(TheSettingManager->GetSettingF(section, "CalmFactor"), 0.0f, 1.7f);
		S.LogMatches = TheSettingManager->GetSettingI(section, "LogMatches") != 0;
#if NVR_TEST_TOOLS
		if (S.LogMatches && !DiagFrame) { DiagFrame = FrameId + 60; DiagLines = 0; }
#endif
		float d[3] = { S.Dir[0], S.Dir[1], S.Dir[2] };
		float speed = S.Speed, intensity = S.Intensity;
		WavelengthNow = S.Wavelength; FlutterNow = S.Flutter; FlutterAmountNow = S.FlutterAmount; RippleNow = S.Ripple;
		if (IndoorWind) {
			const char* in = "Shaders.Cloth.Interiors";
			S.In.Strength = std::clamp(TheSettingManager->GetSettingF(in, "Strength"), 0.0f, 4.0f);
			S.In.Intensity = std::clamp(TheSettingManager->GetSettingF(in, "WindIntensity"), 0.0f, 40.0f);
			S.In.Speed = std::clamp(TheSettingManager->GetSettingF(in, "WindSpeed"), 0.0f, 2000.0f);
			S.In.Wavelength = (std::max)(TheSettingManager->GetSettingF(in, "Wavelength"), 16.0f);
			S.In.Flutter = std::clamp(TheSettingManager->GetSettingF(in, "FlutterSpeed"), 0.0f, 20.0f);
			S.In.FlutterAmount = std::clamp(TheSettingManager->GetSettingF(in, "FlutterAmount"), 0.0f, 1.0f);
			S.In.Ripple = std::clamp(TheSettingManager->GetSettingF(in, "RippleAmount"), 0.0f, 1.0f);
			speed = S.In.Speed; intensity = S.In.Intensity * S.In.Strength;
			WavelengthNow = S.In.Wavelength; FlutterNow = S.In.Flutter; FlutterAmountNow = S.In.FlutterAmount; RippleNow = S.In.Ripple;
		}
		else if (S.UseGameWind && sky) {
			const float a = sky->windDirection;
			d[0] = sinf(a); d[1] = cosf(a); d[2] = 0.0f;
			const float w = std::clamp(sky->windSpeed, 0.0f, 1.0f);
			speed *= 0.4f + 1.2f * w;
			intensity *= S.CalmFactor + (1.7f - S.CalmFactor) * w;
			if (!LoggedGameWind) { LoggedGameWind = true; Logger::Log("Cloth wind: the game's wind, direction %.3f, speed %.3f", a, sky->windSpeed); }
		}
		const float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		for (int i = 0; i < 3; i++) WorldDir[i] = len > 1e-4f ? d[i] / len : (i == 0 ? 1.0f : 0.0f);
		SpeedNow = speed; IntensityNow = intensity;
		static bool wasIndoorWind = false;
		if (IndoorWind && !wasIndoorWind && IndoorLogs < 8) {
			IndoorLogs++;
			Logger::Log("Cloth wind: indoors with the indoor set: %.2f units (WindIntensity %.2f x Strength %.2f), speed %.0f, wavelength %.0f, flutter %.2f Hz x %.2f, ripple %.2f, motion indoors %s",
				intensity, S.In.Intensity, S.In.Strength, speed, WavelengthNow, FlutterNow, FlutterAmountNow, RippleNow, S.MotionIndoors ? "on" : "off");
		}
		wasIndoorWind = IndoorWind;
		const double before = ClockSeconds;
		AdvanceClock();
		const double dt = ClockSeconds - before;
		S.TimeOffset = TheSettingManager->GetSettingF(section, "TimeOffset");
		Rate[0] = speed / WavelengthNow; Rate[1] = 0.061 * (0.5 + speed / 300.0); Rate[2] = FlutterNow; Rate[3] = FlutterNow * 1.31;
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
			for (auto m = Motions.begin(); m != Motions.end();)
				m = FrameId - m->second.frame > 600 ? Motions.erase(m) : std::next(m);
			if (S.LogMatches || StatFallback) {
				Logger::Log("Cloth wind: weights: %u partition draws with weights, %u with the skeleton mask instead (%u without a declaration); %u meshes known",
					StatWeighted, StatFallback, StatNoDecl, (unsigned)Weights.size());
			}
			if (S.LogMatches && (StatWalkDraws || StatWalkNoSpace))
				Logger::Log("Cloth wind: walking: %u partition draws with the walk part, %u without (no space); %u wearers known", StatWalkDraws, StatWalkNoSpace, (unsigned)Walkers.size());
			StatWeighted = StatFallback = StatNoDecl = StatWalkDraws = StatWalkNoSpace = 0;
			for (auto w = Walkers.begin(); w != Walkers.end();)
				w = FrameId - w->second.frame > 600 ? Walkers.erase(w) : std::next(w);
			for (auto w = Weights.begin(); w != Weights.end();)
				if (FrameId - w->second.lastFrame > 1800) { ReleaseBuffers(w->second); w = Weights.erase(w); } else ++w;
			if (S.LogMatches && (StatStillDraws || StatStillMissed))
				Logger::Log("Cloth wind: still cloth: %u draws with weights, %u without (drawn still); %u meshes known", StatStillDraws, StatStillMissed, (unsigned)Stills.size());
			StatStillDraws = StatStillMissed = 0;
			for (auto m = Stills.begin(); m != Stills.end();)
				if (FrameId - m->second.lastFrame > 1800) { if (m->second.vb) m->second.vb->Release(); m = Stills.erase(m); } else ++m;
			Frames = ClothDraws = SkinnedDrawsNow = 0;
		}
	}

	inline void Zero(IDirect3DDevice9* device) {
		ActiveWeights = nullptr;
		ActiveWalk = nullptr; ActiveSkin = nullptr;
		ActiveStill = nullptr;
		if (!NonZeroSent || !device) return;
		static const float zero[4] = { 0, 0, 0, 0 };
		device->SetVertexShaderConstantF(248, zero, 1);
		NonZeroSent = false; GrassInC248 = false;
	}
	inline void AfterDraw(IDirect3DDevice9* device) { if (device) Zero(device); }

	inline const float* MotionOf(const NiAVObject* root) {
		Motion& m = Motions[root];
		if (m.valid && m.frame == FrameId) return m.out;
		const NiTransform& t = root->m_worldTransform;
		const float p[3] = { t.pos.x, t.pos.y, t.pos.z };
		const float yaw = atan2f(t.rot.data[1][0], t.rot.data[0][0]);
		const double dt = ClockSeconds - m.clock;
		const float jump = sqrtf((p[0] - m.pos[0]) * (p[0] - m.pos[0]) + (p[1] - m.pos[1]) * (p[1] - m.pos[1]) + (p[2] - m.pos[2]) * (p[2] - m.pos[2]));
		if (!m.valid || dt > 0.25 || dt < 0.0 || jump > 400.0f) {
			m = Motion();
			m.valid = true; m.frame = FrameId; m.clock = ClockSeconds; m.yaw = yaw;
			for (int i = 0; i < 3; i++) m.pos[i] = p[i];
			return m.out;
		}
		m.frame = FrameId;
		if (dt < 1e-5) return m.out;
		const float h = (float)dt, smooth = 1.0f - expf(-h / 0.1f);
		if (jump / h > 1500.0f) {
			m = Motion();
			m.valid = true; m.frame = FrameId; m.clock = ClockSeconds; m.yaw = yaw;
			for (int i = 0; i < 3; i++) m.pos[i] = p[i];
			return m.out;
		}
		m.clock = ClockSeconds;
		for (int i = 0; i < 3; i++) { m.vel[i] += ((p[i] - m.pos[i]) / h - m.vel[i]) * smooth; m.pos[i] = p[i]; }
		float dy = yaw - m.yaw;
		if (dy > 3.14159265f) dy -= 6.2831853f; else if (dy < -3.14159265f) dy += 6.2831853f;
		m.yaw = yaw;
		m.yawRate += (dy / h - m.yawRate) * smooth;
		float target[3] = { -m.vel[0], -m.vel[1], -m.vel[2] * 0.5f };
		const float tl = sqrtf(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
		const float trail = S.MotionMax > 0.0f ? S.MotionMax * (1.0f - expf(-tl * S.MotionDrag / S.MotionMax)) : 0.0f;
		for (float& v : target) v = tl > 1e-3f ? v * trail / tl : 0.0f;
		const float turnTarget = t.rot.data[2][2] > 0.8f ? std::clamp(-m.yawRate * S.TurnLag, -0.5f, 0.5f) : 0.0f;
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
		const float speed = sqrtf(m.vel[0] * m.vel[0] + m.vel[1] * m.vel[1]), pace = (std::min)(speed / 350.0f, 1.5f);
		m.stride += dt * speed / (S.StrideLength * (0.45f + 0.55f * (std::min)(pace, 1.0f))); m.stride -= floor(m.stride);
		const float swish = speed > 1.0f ? sinf(6.2831853f * (float)m.stride) * S.StrideSwing * sqrtf(pace) / speed : 0.0f;
		m.out[0] = m.offset[0] - m.vel[1] * swish;
		m.out[1] = m.offset[1] + m.vel[0] * swish;
		m.out[2] = m.offset[2];
		m.out[3] = m.turn;
		return m.out;
	}

	inline void ForGeometry(IDirect3DDevice9* device, NiGeometry* geometry, bool shadowPass = false, bool firstPerson = false) {
		if (!device) return;
		ActiveWeights = nullptr;
		if (!S.On || !geometry || firstPerson || (!Exterior && !S.MotionIndoors)) { Zero(device); return; }
		bool boundAnswer = false;
		if (!shadowPass) {
			auto t = TentativeNow.find(geometry);
			if (t == TentativeNow.end()) TentativeNow.emplace(geometry, CurrentVSCapable);
			else t->second = t->second && CurrentVSCapable;
			if (!CurrentVSCapable) VanillaNow.insert(geometry);
			boundAnswer = !SeenLast.count(geometry) && CurrentVSCapable && !VanillaLast.count(geometry);
		}
		if (!shadowPass && !boundAnswer && (!SeenLast.count(geometry) || VanillaLast.count(geometry))) {
			if (S.LogMatches) {
				auto c = ClothPassCounts.find(geometry);
				if (c != ClothPassCounts.end()) { if (!SeenLast.count(geometry)) c->second.notSeen++; else c->second.vanilla++; }
			}
			Zero(device); return;
		}
		if (shadowPass && VanillaLast.count(geometry)) { Zero(device); return; }
		BSShaderProperty* property = (BSShaderProperty*)geometry->GetProperty(NiProperty::PropertyType::kType_Shade);
		const UInt32 vtable = property ? *(UInt32*)property : 0;
		if (vtable != 0x10AE0D0 && vtable != 0x10B9910) { Zero(device); return; }
		BSShaderPPLightingProperty* lighting = (BSShaderPPLightingProperty*)property;
		const NiSourceTexture* diffuse = lighting->ppTextures[0] ? lighting->ppTextures[0][0] : nullptr;
		if (!diffuse || !diffuse->ddsPath1) { Zero(device); return; }
		const bool skinned = geometry->skinInstance != nullptr;
		if (!Exterior && !skinned) { Zero(device); return; }
		bool cloth;
		auto known = ByTexture.find(diffuse);
		if (known != ByTexture.end() && known->second.first == diffuse->ddsPath1) cloth = known->second.second;
		else {
			if (ByTexture.size() >= 4096) ByTexture.clear();
			cloth = IsClothTexture(diffuse->ddsPath1); ByTexture[diffuse] = { std::string(diffuse->ddsPath1), cloth };
			if (cloth || (S.LogMatches && (skinned || LooksLikeCloth(diffuse->ddsPath1)))) {
				const NiGeometryData* gd = geometry->geomData;
				Logger::Log("Cloth wind: %s %s (%s '%s', %u vertices%s)", cloth ? "CLOTH" : "not cloth:", diffuse->ddsPath1, skinned ? "skinned" : "static",
					geometry->m_pcName ? geometry->m_pcName : "", gd ? (unsigned)gd->m_usVertices : 0u, gd && (gd->m_ucKeepFlags & NiGeometryData::KEEP_XYZ) ? ", vertices kept" : ", vertices not kept");
			}
		}
		const Verdict* autoVerdict = nullptr;
		if (!cloth && skinned && S.AutoDetect && S.MaskMode == 2 && S.UseSkeleton && S.UseWeights && DrawHookOn) {
			autoVerdict = AutoVerdictOf(geometry, diffuse);
			if (autoVerdict && autoVerdict->state == 2) cloth = true; else autoVerdict = nullptr;
		}
		if (!cloth || (S.MaskMode == 0 && (!geometry->geomData || !geometry->geomData->m_pkColor))) { Zero(device); return; }
		float dir[3] = { WorldDir[0], WorldDir[1], WorldDir[2] };
		float scale = 1.0f;
		float mode = (float)S.MaskMode;
		float top = 0.0f, invRange = 0.0f;
		float origin[3] = { 0.0f, 0.0f, 0.0f };
		float motion[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, turn[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		if (!skinned) {
			const NiTransform& w = geometry->m_worldTransform;
			scale = w.scale > 1e-4f ? w.scale : 1.0f;
			for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			if (geometry->geomData) {
				const NiBound& b = geometry->geomData->m_kBound;
				top = b.Center.z + 0.8f * b.Radius; invRange = b.Radius > 1e-3f ? 1.0f / (1.6f * b.Radius) : 0.0f;
			}
			if (S.MaskMode == 2 && S.StillWeights && StillHookOn) {
				StillMesh* sm = StillOf(geometry, diffuse->ddsPath1);
				if (!sm) { Zero(device); return; }
				mode = 6.0f; ActiveStill = sm;
			}
		}
		else {
			const NiAVObject* root = geometry->skinInstance->RootParent ? geometry->skinInstance->RootParent : geometry;
			SkinnedDrawsNow++;
			if (S.LogMatches) {
				PassCounts& c = ClothPassCounts[geometry];
				if (c.name.empty()) c.name = geometry->m_pcName ? geometry->m_pcName : "?";
				if (shadowPass) c.shadow++; else c.lit++;
			}
			const Skeleton* skeleton = S.MaskMode == 2 && S.UseSkeleton ? &SkeletonOf(geometry) : nullptr;
			if (skeleton && !skeleton->ok && skeleton->actor) { Zero(device); return; }
			if (S.MaskMode != 2) {}
			else if (skeleton && skeleton->ok) {
				mode = 4.0f;
				float limbs[56] = {};
				memcpy(limbs, skeleton->pts, sizeof(skeleton->pts));
				limbs[48] = skeleton->hipZ; limbs[49] = 1.0f / (std::max)(skeleton->hipZ - skeleton->kneeZ, 1.0f);
				limbs[50] = S.LimbGap; limbs[51] = 1.0f / (std::max)(S.LimbFade, 0.1f);
				limbs[52] = skeleton->groundZ; limbs[53] = 1.0f / 6.0f;
				if (MeshWeights* mw = S.UseWeights && DrawHookOn ? WeightsOf(device, geometry, *skeleton, autoVerdict ? &autoVerdict->r : nullptr) : nullptr) {
					mode = 5.0f; limbs[54] = mw->center[0]; limbs[55] = mw->center[1]; ActiveWeights = mw;
					ActiveSkin = geometry->skinInstance; ActiveBonesBase = shadowPass ? 9 : 44;
					ActiveWalk = S.WalkSim && (!Interior || S.MotionIndoors) ? WalkOf(geometry, *skeleton, *mw) : nullptr;
					if (!ActiveWalk) { const float off[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; device->SetVertexShaderConstantF(219, off, 1); }
				}
				device->SetVertexShaderConstantF(226, limbs, 14);
			}
			else if (S.SkinMask == 1 && geometry->geomData) {
				mode = 3.0f + S.LegCut * 0.001f;
				const float range = S.HeightTop > S.HeightBottom ? S.HeightTop - S.HeightBottom : 1.0f;
				top = S.HeightTop; invRange = 1.0f / range;
			}
			else if (S.SkinMask == 2 && geometry->geomData) {
				const Extent& e = ExtentOf(geometry);
				const float height = e.hi - e.lo > 1.0f ? e.hi - e.lo : 1.0f;
				mode = 3.0f;
				top = e.hi - S.PinFraction * height;
				invRange = 1.0f / ((1.0f - S.PinFraction) * height);
			}
			else {
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
			if (S.SkinSpace == 0 && !shadowPass) {
				const NiTransform& w = root->m_worldTransform;
				scale = w.scale > 1e-4f ? w.scale : 1.0f;
				for (int i = 0; i < 3; i++) dir[i] = w.rot.data[0][i] * WorldDir[0] + w.rot.data[1][i] * WorldDir[1] + w.rot.data[2][i] * WorldDir[2];
			}
			else if (S.SkinSpace == 1) { origin[0] = TheRenderManager->CameraPosition.x; origin[1] = TheRenderManager->CameraPosition.y; origin[2] = TheRenderManager->CameraPosition.z; }
			static const float still[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			const float* mo = !Interior || S.MotionIndoors ? MotionOf(root) : still;
			const float th = mo[3];
			if (S.SkinSpace == 0 && !shadowPass) {
				const NiTransform& w = root->m_worldTransform;
				for (int i = 0; i < 3; i++) motion[i] = (w.rot.data[0][i] * mo[0] + w.rot.data[1][i] * mo[1] + w.rot.data[2][i] * mo[2]) / scale;
				turn[1] = th; turn[2] = -th;
			}
			else {
				for (int i = 0; i < 3; i++) motion[i] = mo[i];
				const NiMatrix33& r = root->m_worldTransform.rot;
				turn[0] = th * r.data[0][1]; turn[1] = th * r.data[1][1]; turn[2] = -th * r.data[0][0]; turn[3] = -th * r.data[1][0];
			}
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
		float ph[4] = { Phase[0], Phase[1], Phase[2], Phase[3] };
		if (origin[0] != 0.0f || origin[1] != 0.0f || origin[2] != 0.0f) {
			const double w = (double)scale / WavelengthNow, r = (double)S.RippleScale * scale;
			const double d0 = (origin[0] * dir[0] + origin[1] * dir[1] + origin[2] * dir[2]) * w;
			const double d1 = origin[0] * 0.0021 + origin[1] * 0.0027 + origin[2] * 0.0013;
			const double d2 = (origin[0] * 0.71 + origin[1] * 0.53 + origin[2] * 0.37) * r;
			const double d3 = (origin[0] * -0.43 + origin[1] * 0.81 + origin[2] * 0.59) * r * 1.7;
			auto wrap = [](double c) { return (float)(c - floor(c)); };
			ph[0] = wrap(Phase[0] - d0); ph[1] = wrap(Phase[1] + d1); ph[2] = wrap(Phase[2] + d2); ph[3] = wrap(Phase[3] - d3);
		}
		if (ActiveWalk) { for (float& v : motion) v = 0.0f; for (float& v : turn) v = 0.0f; }
		const float c[24] = {
			1.0f, mode, RippleNow, FlutterAmountNow,
			dir[0], dir[1], dir[2], scale / WavelengthNow,
			ph[0], ph[1], ph[2], ph[3],
			(Exterior ? IntensityNow : 0.0f) / scale, S.RippleScale * scale, top, invRange,
			motion[0], motion[1], motion[2], 0.0f,
			turn[0], turn[1], turn[2], turn[3] };
		device->SetVertexShaderConstantF(248, c, 6);
		if (ActiveWeights) memcpy(ActiveC248, c, sizeof(ActiveC248));
		NonZeroSent = true; GrassInC248 = false;
		ClothDraws++;
	}
}
