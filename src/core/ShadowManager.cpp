#define ShadowMapFarPlane 32768;
#include "ShadowFaceCull.h"
#include "PointShadowSchedule.h"
#include "PointShadowForward.h"
#include "TestTools.h"	// UNOFFICIAL: NVR_TEST_TOOLS (SkipWhenFadedCheck)
#include "OrthoProbe.h"	// UNOFFICIAL 2026-10-17 test tools: ortho map caster dump
#include "SunShadowStats.h"	// UNOFFICIAL 2026-10-21 test tools: where the sun cascades' CPU time goes
#include "../effects/CoverMap.h"	// UNOFFICIAL 2026-10-27 Persistent wetness and snow: CoverMapOrthoFrame
#include "DisplaceShadows.h"	// UNOFFICIAL 2026-10-27 displacement shadows: the land drawn with its bumps into the sun cascades

// UNOFFICIAL SkinnedShadowFaceTest ([Main.Main.Performance], default on, 2026-10-07). An animated (skinned) mesh's own
// world bound is stale, so skinned casters used to be drawn into all six faces of every lamp cube map near them; with a
// crowd that is most of the cube-map work (FNVProbe crowd test: NVR's shadow maps 1.8 ms with 8 NPCs, 7.7 with 58, 39
// with 354). Their bound is now rebuilt each frame from the skeleton, the way Gamebryo's NiSkinInstance::UpdateModelBound
// (0x00A86530) does: each bone's sphere of the vertices it moves (NiSkinData::BoneData::Bound, bone space) is carried to
// the bone's current world transform and the spheres are merged. A skinned vertex is a weighted blend of its bones'
// transformed positions, so it lies inside that merged sphere: the faces it reaches are exactly the faces drawn before
// that it can touch, so the picture does not change. Padded a little for float safety; any missing bone or bound falls
// back to every face, as before.
struct SkinnedBoundEntry { UInt32 frame; bool ok; NiBound bound; };
static std::unordered_map<NiGeometry*, SkinnedBoundEntry> SkinnedBounds;
static UInt32 SkinnedBoundFrame = 0;	// advanced once per ShadowManager::RenderShadowMaps
static unsigned statSkinnedCasters = 0, statSkinnedFaces = 0, statSkinnedNoBound = 0, statSkinnedOutOfRange = 0;

static bool SkinnedWorldBound(NiGeometry* geometry, NiBound& out) {
	SkinnedBoundEntry& entry = SkinnedBounds[geometry];
	if (entry.frame == SkinnedBoundFrame) { out = entry.bound; return entry.ok; }
	entry.frame = SkinnedBoundFrame;
	entry.ok = false;
	NiSkinInstance* skin = geometry->skinInstance;
	NiSkinData* data = skin ? skin->SkinData : nullptr;
	if (!data || !data->BoneData || !skin->BoneObjects || data->Bones == 0) return false;
	D3DXVECTOR3 center(0.0f, 0.0f, 0.0f);
	float radius = -1.0f;
	for (UInt32 i = 0; i < data->Bones; i++) {
		NiAVObject* bone = skin->BoneObjects[i];
		if (!bone) return false;
		const NiTransform& t = bone->m_worldTransform;
		const NiBound& b = data->BoneData[i].Bound;
		if (!(b.Radius >= 0.0f && b.Radius < 1.0e6f) || !(t.scale > 0.0f && t.scale < 1.0e3f)) return false;
		const NiPoint3 scaled = { b.Center.x * t.scale, b.Center.y * t.scale, b.Center.z * t.scale };
		const NiPoint3 rotated = t.rot * scaled;
		const D3DXVECTOR3 c(rotated.x + t.pos.x, rotated.y + t.pos.y, rotated.z + t.pos.z);
		const float r = b.Radius * t.scale;
		if (radius < 0.0f) { center = c; radius = r; continue; }
		const D3DXVECTOR3 d = c - center;
		const float dist = D3DXVec3Length(&d);
		if (dist + r <= radius) continue;					// inside what we have
		if (dist + radius <= r) { center = c; radius = r; continue; }	// swallows what we have
		const float merged = (dist + radius + r) * 0.5f;
		center += d * ((merged - radius) / dist);
		radius = merged;
	}
	if (radius < 0.0f) return false;
	entry.bound.Center.x = center.x; entry.bound.Center.y = center.y; entry.bound.Center.z = center.z;
	entry.bound.Radius = radius * 1.02f + 2.0f;
	entry.ok = true;
	out = entry.bound;
	return true;
}

static bool TouchesShadowFace(NiAVObject* object, const NiPoint3* light,
                              const D3DXVECTOR3& direction) {
	NiGeometry* geometry = object->IsGeometry() ? static_cast<NiGeometry*>(object) : nullptr;
	if (geometry && geometry->skinInstance) {
		// Animated/deformed geometry may exceed the engine's current CPU bound: use the skeleton's (see above), or every face.
		NiBound skinned;
		if (!TheSettingManager->SettingsMain.Main.SkinnedShadowFaceTest || !SkinnedWorldBound(geometry, skinned)) return true;
		return ShadowSphereTouchesFace(skinned.Center.x - light->x, skinned.Center.y - light->y, skinned.Center.z - light->z,
			skinned.Radius, direction.x, direction.y, direction.z);
	}
	NiBound* bound = object->m_kWorldBound;
	if (!bound) return true;
	return ShadowSphereTouchesFace(bound->Center.x - light->x,
		bound->Center.y - light->y, bound->Center.z - light->z, bound->Radius,
		direction.x, direction.y, direction.z);
}

// Hash exactly the scene state that can change an otherwise camera-independent point-shadow cubemap.
// If the game's geometry list is unavailable the caller keeps the existing PointShadowInterval schedule.
static UInt64 HashPointShadowBytes(UInt64 hash, const void* data, size_t size) {
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ULL; }
	return hash;
}

// UNOFFICIAL 2026-10-21 FastCasterHash ([Main.Main.Performance], cpulimits; bench\deep-roadmap\CPU-LIMITS.md). The byte-wise 64-bit
// FNV above costs a call to the compiler's 64-bit multiply helper (_allmul, 32-bit build) for every byte: ~90 bytes per caster, for
// every lamp the caster is near, every frame. The CPU sampler put ~3 % of the main thread in the Ultra-Luxe here (the hash, _allmul,
// RecentlyMoved). With it on: the same bytes, four at a time, in two 32-bit lanes (FNV-1a and a Murmur-style mix) that use the CPU's
// own multiply, and a caster's hash is made once per frame however many lamps it is near (CasterMotion keeps it). Only what a lamp
// redraws on depends on the hash: a change is still caught unless both 32-bit lanes collide (about 1 in 2^64).
static bool FastCasterHashOn() { return TheSettingManager->SettingsMain.Main.FastCasterHash; }
static UInt64 HashPointShadowWords(UInt64 hash, const void* data, size_t size) {
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	UInt32 a = (UInt32)hash, b = (UInt32)(hash >> 32);
	size_t i = 0;
	for (; i + 4 <= size; i += 4) {
		UInt32 w;
		memcpy(&w, bytes + i, sizeof(w));
		a = (a ^ w) * 16777619u;
		b = (b ^ w) * 0x5BD1E995u; b ^= b >> 15;
	}
	for (; i < size; ++i) {
		a = (a ^ bytes[i]) * 16777619u;
		b = (b ^ bytes[i]) * 0x5BD1E995u; b ^= b >> 15;
	}
	return ((UInt64)b << 32) | a;
}
static UInt64 HashCasterBytes(UInt64 hash, const void* data, size_t size) {
	return FastCasterHashOn() ? HashPointShadowWords(hash, data, size) : HashPointShadowBytes(hash, data, size);
}

// UNOFFICIAL RedrawActorsOnly (2026-10-08): rigid geometry that moves -- a held gun, a hat, a door swinging, a box being
// pushed -- counts as moving while its world transform keeps changing and for MovingSettleFrames after it last changed.
// Without this, people's rigid parts were in the static layer and changed it every frame, so it was redrawn every frame.
// Each geometry is looked at once per frame (SkinnedBoundFrame); first sight counts as still.
struct CasterMotion { UInt32 seen = 0, changed = 0; UInt64 transform = 0; bool known = false; UInt32 hashFrame = 0; UInt64 casterHash = 0; bool hashKnown = false; };
static std::unordered_map<NiGeometry*, CasterMotion> CasterMotions;
static const UInt32 MovingSettleFrames = 60;

// UNOFFICIAL 2026-10-30 CasterListCache ([Main.Main.Performance], live, with FastCasterHash; ROOTPLAN round 7 step 18): the lamps' caster
// check looked every caster up in CasterMotions once per lamp, every frame (the map lookup alone ~0.7 % of the main thread in the
// Ultra-Luxe, the whole check ~3 %). Each lamp now keeps its list's (geometry, entry) pairs and only looks up a caster that is new at
// that place in its list; a caster's hash takes its transform's hash from the motion check instead of hashing the matrix again.
// Same answers (redraw or keep): the entries are the same ones. CasterMotionsGeneration drops the kept lists when entries are erased.
struct LampCasterList { std::vector<std::pair<NiGeometry*, CasterMotion*>> casters; UInt32 generation = 0; UInt32 seen = 0; };
static std::unordered_map<ShadowSceneLight*, LampCasterList> LampCasterLists;
static UInt32 CasterMotionsGeneration = 1;
static bool CasterListCacheOn() { return TheSettingManager->SettingsMain.Main.CasterListCache && TheSettingManager->SettingsMain.Main.FastCasterHash; }

static bool RecentlyMovedAt(NiGeometry* geo, CasterMotion& motion) {
	if (motion.seen != SkinnedBoundFrame || !motion.known) {
		const UInt64 transform = HashCasterBytes(1469598103934665603ULL, &geo->m_worldTransform, sizeof(geo->m_worldTransform));
		if (motion.known && transform != motion.transform) motion.changed = SkinnedBoundFrame;
		motion.transform = transform;
		motion.known = true;
		motion.seen = SkinnedBoundFrame;
	}
	return motion.changed && SkinnedBoundFrame - motion.changed < MovingSettleFrames;
}

static bool RecentlyMoved(NiGeometry* geo, CasterMotion** out = nullptr) {
	CasterMotion& motion = CasterMotions[geo];
	if (out) *out = &motion;
	if (motion.seen != SkinnedBoundFrame || !motion.known) {
		const UInt64 transform = HashCasterBytes(1469598103934665603ULL, &geo->m_worldTransform, sizeof(geo->m_worldTransform));
		if (motion.known && transform != motion.transform) motion.changed = SkinnedBoundFrame;
		motion.transform = transform;
		motion.known = true;
		motion.seen = SkinnedBoundFrame;
	}
	return motion.changed && SkinnedBoundFrame - motion.changed < MovingSettleFrames;
}

// A caster that moves: skinned geometry (people, creatures), wind-blown SpeedTree leaves (both move without their transform
// changing), and rigid geometry that moved lately (RecentlyMoved). They make a lamp's caster list dynamic, and
// RedrawActorsOnly draws them apart from the rest (RenderShadowCubeMap).
static bool IsMovingCaster(NiGeometry* geo, NiShadeProperty* shade, CasterMotion** motion = nullptr) {
	if (motion) *motion = nullptr;
	if (!geo) return false;
	if (geo->skinInstance || (shade && shade->m_eShaderType == NiShadeProperty::kProp_SpeedTreeLeaf)) return true;
	return RecentlyMoved(geo, motion);
}

static void PointShadowCasterState(ShadowSceneLight* light, UInt64& hash, bool& staticCasters) {
	// UNOFFICIAL LampGlassShadows / LampModelShadows (2026-10-20): switching them changes what a lamp's cube map holds, so they change
	// the hash (a redraw); with LampModelShadows off so does a new listing of the cell's lamp models (LampFixtures::Generation).
	const ShadowsExteriorEffect::InteriorsStruct::ForwardStruct& forwardSettings = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.Forward;
	hash = forwardSettings.LampGlassShadows ? 1469598103934665603ULL : 0x6C62272E07BB0142ULL;
	if (!forwardSettings.LampModelShadows)
		hash = (hash ^ 0x9E3779B97F4A7C15ULL ^ ((UInt64)LampFixtures::Generation << 32) ^ (UInt64)(forwardSettings.LampModelDistance * 16.0f) ^
			((UInt64)(SInt64)(LampFixtures::OwnLightDistance * 16.0f) << 16)) * 1099511628211ULL;
	// UNOFFICIAL 2026-10-29 TwoSidedCasters ([Shaders.ShadowsInteriors.Main], RenderShadowCubeMap): switching it redraws the kept maps too
	// (the first bench run switched it live and the static maps stayed as they were drawn).
	if (TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "TwoSidedCasters") != 0)
		hash = (hash ^ 0x2D5A6B1C3E4F7081ULL) * 1099511628211ULL;
	if (!LampFixtures::NoShadowWords.empty())	// UNOFFICIAL NoShadowLampModels (2026-10-27): its words and the cell's lamp list redraw too
		hash = (hash ^ (UInt64)std::hash<std::string>{}(LampFixtures::NoShadowFrom) ^ ((UInt64)LampFixtures::Generation << 40) ^
			(UInt64)(forwardSettings.LampModelDistance * 16.0f)) * 1099511628211ULL;
	staticCasters = light && light->kGeometryList.start;
	if (!staticCasters) return;
	// FastCasterHash switched (live): its hashes differ from the old ones, so every caster's motion starts again from 'first sight'
	// (counts as still) instead of looking moved for MovingSettleFrames; each lamp's set hash changes once (one redraw).
	const bool fast = FastCasterHashOn();
	static bool fastBefore = false;
	if (fast != fastBefore) { fastBefore = fast; CasterMotions.clear(); CasterMotionsGeneration++; }
	// CasterListCache switched (live): its hashes take the transform's hash, so each lamp's set hash changes once (one redraw)
	const bool listCache = fast && CasterListCacheOn();
	static bool listCacheBefore = false;
	if (listCache != listCacheBefore) { listCacheBefore = listCache; LampCasterLists.clear(); }
	LampCasterList* kept = nullptr;
	size_t keptAt = 0;
	if (listCache) {
		kept = &LampCasterLists[light];
		if (kept->generation != CasterMotionsGeneration) { kept->casters.clear(); kept->generation = CasterMotionsGeneration; }
		kept->seen = SkinnedBoundFrame;
	}

	// The hash covers only the casters that never move, so with moving ones it describes what RedrawActorsOnly keeps in
	// the static cube map.
	for (auto entry = light->kGeometryList.start; entry; entry = entry->next) {
		NiGeometry* geo = entry->data;
		NiShadeProperty* shade = geo ? static_cast<NiShadeProperty*>(geo->GetProperty(NiProperty::kType_Shade)) : nullptr;
		CasterMotion* motion = nullptr;
		bool moving;
		if (kept && geo) {	// CasterListCache: this lamp's entry for the caster at this place in its list, looked up only when it changed
			const bool alwaysMoving = geo->skinInstance || (shade && shade->m_eShaderType == NiShadeProperty::kProp_SpeedTreeLeaf);	// as IsMovingCaster: no entry
			if (keptAt < kept->casters.size() && kept->casters[keptAt].first == geo) motion = kept->casters[keptAt].second;
			else {
				motion = alwaysMoving ? nullptr : &CasterMotions[geo];
				if (keptAt < kept->casters.size()) kept->casters[keptAt] = { geo, motion };
				else kept->casters.push_back({ geo, motion });
			}
			keptAt++;
			moving = alwaysMoving || !motion || RecentlyMovedAt(geo, *motion);
			if (alwaysMoving) motion = nullptr;
		}
		else
			moving = IsMovingCaster(geo, shade, &motion);
		if (moving) {
			staticCasters = false;
			continue;
		}
		if (fast && motion && motion->hashKnown && motion->hashFrame == SkinnedBoundFrame) {	// FastCasterHash: made for another lamp this frame
			hash = PointShadowAddCasterHash(hash, motion->casterHash);
			continue;
		}
		UInt64 casterHash = 1469598103934665603ULL;
		casterHash = HashCasterBytes(casterHash, &geo, sizeof(geo));
		if (!geo) continue;
		casterHash = HashCasterBytes(casterHash, &geo->m_flags, sizeof(geo->m_flags));
		if (kept && motion && motion->seen == SkinnedBoundFrame) casterHash = HashCasterBytes(casterHash, &motion->transform, sizeof(motion->transform));	// CasterListCache: hashed this frame already
		else casterHash = HashCasterBytes(casterHash, &geo->m_worldTransform, sizeof(geo->m_worldTransform));
		if (geo->m_kWorldBound) casterHash = HashCasterBytes(casterHash, geo->m_kWorldBound, sizeof(*geo->m_kWorldBound));

		NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
		if (shade) {
			casterHash = HashCasterBytes(casterHash, &shade->m_usFlags, sizeof(shade->m_usFlags));
			casterHash = HashCasterBytes(casterHash, &shade->m_eShaderType, sizeof(shade->m_eShaderType));
		}
		if (material) casterHash = HashCasterBytes(casterHash, &material->fAlpha, sizeof(material->fAlpha));
		if (fast && motion) { motion->casterHash = casterHash; motion->hashFrame = SkinnedBoundFrame; motion->hashKnown = true; }
		hash = PointShadowAddCasterHash(hash, casterHash);
	}
	if (kept && kept->casters.size() > keptAt) kept->casters.resize(keptAt);
}

// UNOFFICIAL RedrawActorsOnly needs a min blend into the R32F cube maps (and a face copy, checked as it happens); asked
// once. A failed copy turns it off for the session (ActorsOnlyBroken); the lamps are then redrawn whole, as before.
static bool ActorsOnlyBroken = false;
static bool ActorsOnlySupported(IDirect3DDevice9* device) {
	static int supported = -1;
	if (supported < 0) {
		supported = 0;
		D3DCAPS9 caps = {};
		IDirect3D9* d3d = nullptr;
		D3DDEVICE_CREATION_PARAMETERS creation = {};
		D3DDISPLAYMODE mode = {};
		if (SUCCEEDED(device->GetDeviceCaps(&caps)) && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_BLENDOP) && SUCCEEDED(device->GetDirect3D(&d3d)) &&
			SUCCEEDED(device->GetCreationParameters(&creation)) && SUCCEEDED(device->GetDisplayMode(0, &mode)))
			supported = SUCCEEDED(d3d->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
				D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_CUBETEXTURE, D3DFMT_R32F)) ? 1 : 0;
		if (d3d) d3d->Release();
		Logger::Log(supported ? "UNOFFICIAL RedrawActorsOnly: available (min blend into R32F cube maps)"
			: "UNOFFICIAL RedrawActorsOnly: this GPU cannot min-blend into R32F cube maps; lamps are redrawn whole, as before");
	}
	return supported == 1 && !ActorsOnlyBroken;
}

// Lamps trade slots as the player walks (PointShadowSlots.h keeps them where it can). A lamp's static cube map stays with
// the lamp: when another slot's was drawn for it, the two slots swap theirs, textures and records together. The static
// cube maps are only ever copied from (never bound to a shader), so swapping the pointers is all it takes.
static void AdoptStaticCubeMap(ShadowsExteriorEffect* shadows, PointShadowSlotState* staticSlots, int slot, const void* light) {
	if (staticSlots[slot].valid && staticSlots[slot].light == light) return;
	for (int k = 0; k < ShadowCubeMapsMax; k++) {
		if (k == slot || !staticSlots[k].valid || staticSlots[k].light != light) continue;
		std::swap(shadows->Textures.ShadowCubeMapStaticTexture[slot], shadows->Textures.ShadowCubeMapStaticTexture[k]);
		for (int f = 0; f < 6; f++) std::swap(shadows->Textures.ShadowCubeMapStaticSurface[slot][f], shadows->Textures.ShadowCubeMapStaticSurface[k][f]);
		std::swap(staticSlots[slot], staticSlots[k]);
		return;
	}
}

/*
* UNOFFICIAL 2026-10-18 CubeFaceCopySkip ([Main.Main.Performance]; work\bench\deep-roadmap\evidence\intshadow-LOG.md). With
* RedrawActorsOnly a lamp with people in reach is refreshed by copying its static cube map (walls and furniture) into the live one,
* all six faces, then drawing the moving casters over it (min blend). A face no moving caster's bound reaches gets nothing drawn: it
* ends up a plain copy of the static face. If the live face already holds exactly that copy -- it was last written by such a
* copy-only refresh, of the same static texture with the same contents (StaticCubeSerials, bumped at every static-layer draw),
* into the same live texture (CubeGeneration guards against a new texture at an old address), and nothing has drawn into it since
* (a whole redraw or a refresh with people on that face clears the record) -- the copy is skipped: the face keeps the same bits.
* DIAGNOSTIC CubeFaceCopySkipCheck (test builds): reads such faces back and compares them with the static face.
*/
struct LiveFaceRecord { const void* live = nullptr; const void* staticTex = nullptr; unsigned serial = 0, generation = 0; bool pure = false; };
static LiveFaceRecord LiveFaces[ShadowCubeMapsMax][6];
static std::unordered_map<const void*, unsigned> StaticCubeSerials;
static unsigned StaticCubeSerialNext = 0;
static unsigned statFacesSkipped = 0, statFacesCopied = 0, statFacesDrawn = 0, statFacesOldPath = 0;

static void ForgetLiveFaces(int slot) {
	for (int f = 0; f < 6; f++) LiveFaces[slot][f].pure = false;
}

// UNOFFICIAL 2026-10-18 LampShadowMoveTolerance ([Main.Main.ReducedQuality], CHANGES THE IMAGE, off at 0): the point the cube map
// is drawn from when it is not the light's own position this frame (RenderShadowMaps sets it around the draw calls).
static NiPoint3* CubeEyeOverride = nullptr;
// UNOFFICIAL InvisibleShadowFromLamp (2026-10-20): while one lamp's cube map is drawn from a visible lamp instead of the light: the radius
// it covers (the light's reach plus the move) and the lamp model at its eye, left out (-1 none).
static float CubeRadiusOverride = 0.0f;
static bool CubeRelocated = false;
static int CubeSkipModel = -1;

// UNOFFICIAL 2026-10-18 CubeFaceViewCull ([Main.Main.Performance]; evidence\intshadow-LOG.md). Indoors with the forward lamp shadows,
// a lamp cube map is looked up only by the world pass and the first-person pass (PointShadowForward::SetForPass: never the water
// reflection or another camera), i.e. by pixels inside the camera's view or close to the camera; the screen-space lamp mask
// draws without cube lookups then. So a face whose pyramid (CubeFaceOutOfView: the lamp and its four corners at the cube radius,
// widened 10% for filtering across the seams; points within 100 units of the camera always count as seen, for the first-person
// model's own field of view) lies wholly outside the view is never read this frame and need not be drawn. Only for lamps that are
// redrawn every frame anyway (moving casters in reach at PointShadowInterval 1, or the lamp itself moved); such a draw is marked
// partial (PointShadowSlotState), so a lamp that turns still is redrawn in full before its cube map is kept. Static layers
// (RedrawActorsOnly) are always drawn in full. DIAGNOSTIC CubeFaceViewCullCheck: the left-out faces are filled with distance 0.1
// (a blocker at a tenth of the reach: all beyond in shadow; 0 would mean no data), so a face read after all shows as a dark patch.
static bool CubeCullAllowed = false;	// set by RenderShadowMaps around one lamp's draws
static bool ForwardWasActive = false;	// PointShadowForward::Active as Publish left it last frame (Begin clears it before the loop)
static bool CubeCulledAny = false;		// RenderShadowCubeMap left a face out
static unsigned statFacesCulled[3] = {};

// UNOFFICIAL 2026-10-18 diagnostic (intshadow): a lamp cube face that no on-screen pixel can look up -- its pyramid (the lamp and
// the face's four corners at the cube radius, widened 10% for filtering across the seams) lies wholly behind one side of the
// camera's view (left, right, top, bottom, behind; the camera as LampMaskRect reads it). Counted only, nothing skipped.
static unsigned statViewFaces[3] = {}, statViewFacesOut[3] = {};
static bool CubeFaceOutOfView(const NiPoint3* light, float radius, const D3DXVECTOR3& direction) {
	const D3DXMATRIX& V = TheRenderManager->viewMatrix;
	const D3DXMATRIX& P = TheRenderManager->projMatrix;
	const D3DXVECTOR3 R(V._11, V._21, V._31), U(V._12, V._22, V._32), F(V._13, V._23, V._33);
	const D3DXVECTOR3 planes[5] = { F, F + R * P._11, F - R * P._11, F + U * P._22, F - U * P._22 };
	const D3DXVECTOR3 L(light->x - TheRenderManager->CameraPosition.x, light->y - TheRenderManager->CameraPosition.y, light->z - TheRenderManager->CameraPosition.z);
	const D3DXVECTOR3 a = fabsf(direction.x) > 0.5f ? D3DXVECTOR3(0, 1, 0) : D3DXVECTOR3(1, 0, 0);
	D3DXVECTOR3 b; D3DXVec3Cross(&b, &direction, &a);
	D3DXVECTOR3 points[5] = { L, L, L, L, L };
	for (int c = 0; c < 4; c++) points[c + 1] = L + radius * (direction + 1.1f * (((c & 1) ? a : -a) + ((c & 2) ? b : -b)));
	for (const D3DXVECTOR3& n : planes) {
		const float margin = -100.0f * D3DXVec3Length(&n);	// points within 100 units of the camera count as seen (first-person model)
		bool allOut = true;
		for (const D3DXVECTOR3& p : points) if (D3DXVec3Dot(&p, &n) >= margin) { allOut = false; break; }
		if (allOut) return true;
	}
	return false;
}

#if NVR_TEST_TOOLS
// DIAGNOSTIC CubeFaceCopySkipCheck N (test builds): every Nth frame, up to two faces that CubeFaceCopySkip left alone are read back
// with the static face they should equal and compared bit for bit. Stalls the GPU.
static void CubeFaceSkipSelfCheck(IDirect3DDevice9* Device, IDirect3DSurface9* live, IDirect3DSurface9* still) {
	static int every = 0;
	static unsigned frameSeen = 0, checksThisFrame = 0, checks = 0, differing = 0, worstValues = 0;
	if (frameSeen != SkinnedBoundFrame) {
		frameSeen = SkinnedBoundFrame;
		checksThisFrame = 0;
		if ((SkinnedBoundFrame % 60) == 0 || every == 0) every = TheSettingManager->GetSettingI("Main.Main.Performance", "CubeFaceCopySkipCheck");
	}
	if (every <= 0 || (SkinnedBoundFrame % (unsigned)every) != 0 || checksThisFrame >= 2 || !live || !still) return;
	checksThisFrame++;
	static IDirect3DSurface9* readA = nullptr;
	static IDirect3DSurface9* readB = nullptr;
	static D3DSURFACE_DESC have = {};
	D3DSURFACE_DESC d = {};
	if (FAILED(live->GetDesc(&d)) || d.Format != D3DFMT_R32F) return;
	if (!readA || d.Width != have.Width || d.Height != have.Height) {
		if (readA) { readA->Release(); readA = nullptr; }
		if (readB) { readB->Release(); readB = nullptr; }
		if (FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readA, NULL)) ||
			FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readB, NULL))) {
			Logger::Log("CUBE FACE CHECK: could not create the read-back surfaces");
			if (readA) { readA->Release(); readA = nullptr; }
			return;
		}
		have = d;
	}
	if (FAILED(Device->GetRenderTargetData(live, readA)) || FAILED(Device->GetRenderTargetData(still, readB))) return;
	D3DLOCKED_RECT la = {}, lb = {};
	if (FAILED(readA->LockRect(&la, NULL, D3DLOCK_READONLY))) return;
	if (FAILED(readB->LockRect(&lb, NULL, D3DLOCK_READONLY))) { readA->UnlockRect(); return; }
	unsigned values = 0;
	for (UINT y = 0; y < d.Height; y++) {
		const UInt32* pa = (const UInt32*)((const UInt8*)la.pBits + (size_t)y * la.Pitch);
		const UInt32* pb = (const UInt32*)((const UInt8*)lb.pBits + (size_t)y * lb.Pitch);
		for (UINT x = 0; x < d.Width; x++) if (pa[x] != pb[x]) values++;
	}
	readB->UnlockRect();
	readA->UnlockRect();
	checks++;
	if (values) { differing++; worstValues = (std::max)(worstValues, values); }
	if (values && differing <= 10) Logger::Log("CUBE FACE CHECK: DIFFERENCE, a skipped face differs from its static face in %u of %u texels", values, d.Width * d.Height);
	if (checks % 30 == 0) Logger::Log("CUBE FACE CHECK: %u skipped faces compared with their static face, %u differ (worst %u texels)", checks, differing, worstValues);
}
#endif

/*
* Initializes the Shadow Manager by grabbing the relevant settings and shaders, and setting up map sizes.
*/
#include "GpuProfiler.h"

// Refresh period per cascade. By default this is the original schedule: every cascade every frame,
// except that LimitFrequency redraws the Lod cascade every fourth frame. StaggeredSunShadows
// (ReducedQuality, off by default) is the P21-P48 schedule: middle every 4th frame, far and Lod every
// 8th. A cached cascade follows the camera's movement, so still shadows stay put, but anything that
// moves (people, creatures, doors) keeps its old shadow until the next refresh: with the stagger,
// people walking a few metres away got shadows that stuttered and flickered (reported under DXVK).
// Resolve and blur run only on cascades updated that frame (BlurShadowAtlas mask), so cached
// cascades stay untouched either way.
// nearInterval is the ReducedQuality NearCascadeInterval switch (1 = every frame, the original).
// UNOFFICIAL 2026-10-28: the sun cascades (bit per ShadowMapTypeEnum) drawn this frame, for GrassShadows (CastShadowsReach): grass
// goes into a middle or far cascade only on the frames that cascade is redrawn.
unsigned SunCascadesDrawnMask = 0;

constexpr unsigned SunCascadeUpdatePeriod(int cascade, bool limitFrequency, int nearInterval = 1, bool staggered = false) {
	if (cascade == ShadowManager::MapNear) return nearInterval == 2 ? 2 : 1;
	if (staggered) {
		if (cascade == ShadowManager::MapMiddle) return 4;
		if (cascade == ShadowManager::MapFar || cascade == ShadowManager::MapLod) return 8;
		return 1;
	}
	if (limitFrequency && cascade == ShadowManager::MapLod) return 4;
	return 1;
}

static_assert(SunCascadeUpdatePeriod(ShadowManager::MapNear, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapFar, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapLod, true) == 4 &&
	SunCascadeUpdatePeriod(ShadowManager::MapLod, false) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapNear, true, 1, true) == 1 &&
	SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true, 1, true) == 4 &&
	SunCascadeUpdatePeriod(ShadowManager::MapFar, true, 1, true) == 8 &&
	SunCascadeUpdatePeriod(ShadowManager::MapLod, false, 1, true) == 8 &&
	SunCascadeUpdatePeriod(ShadowManager::MapNear, true, 2) == 2 && SunCascadeUpdatePeriod(ShadowManager::MapNear, false, 2) == 2 &&
	SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true, 2) == 1 && SunCascadeUpdatePeriod(ShadowManager::MapMiddle, true, 2, true) == 4,
	"Sun cascade update schedule changed unexpectedly");

// Frame offset within each cascade's period, so the infrequent cascades never refresh on the
// same frame. With StaggeredSunShadows, over the 8-frame cycle: 0 near only, 1/5 middle, 2/6 ortho
// map, 3 far, 7 LOD. With the default schedule the Lod cascade (every 4th frame) takes frames 3 and 7.
// Refresh rates are unchanged; this only removes the frame where all four used to render.
constexpr unsigned SunCascadeUpdatePhase(int cascade) {
	if (cascade == ShadowManager::MapMiddle) return 1;
	if (cascade == ShadowManager::MapFar) return 3;
	if (cascade == ShadowManager::MapLod) return 7;
	return 0;
}

constexpr bool SunCascadeUpdatesOnFrame(int cascade, unsigned frame, unsigned period) {
	return frame % period == SunCascadeUpdatePhase(cascade) % period;
}

static_assert(SunCascadeUpdatesOnFrame(ShadowManager::MapNear, 5, 1) &&
	SunCascadeUpdatesOnFrame(ShadowManager::MapMiddle, 5, 4) && !SunCascadeUpdatesOnFrame(ShadowManager::MapMiddle, 0, 4) &&
	SunCascadeUpdatesOnFrame(ShadowManager::MapFar, 3, 8) && SunCascadeUpdatesOnFrame(ShadowManager::MapLod, 7, 8) &&
	SunCascadeUpdatesOnFrame(ShadowManager::MapFar, 0, 1),
	"Sun cascade stagger changed unexpectedly");

// With StaggeredSunShadows and NearCascadeInterval 2 the near cascade takes the even frames and the
// limited ones keep the odd frames, so every frame of the 8-frame cycle draws exactly one sun cascade.
constexpr bool OneSunCascadePerFrame() {
	for (unsigned frame = 0; frame < 8; ++frame) {
		unsigned drawn = 0;
		for (int cascade = ShadowManager::MapNear; cascade < ShadowManager::MapOrtho; ++cascade)
			drawn += SunCascadeUpdatesOnFrame(cascade, frame, SunCascadeUpdatePeriod(cascade, true, 2, true)) ? 1 : 0;
		if (drawn != 1) return false;
	}
	return true;
}
static_assert(OneSunCascadePerFrame(), "StaggeredSunShadows with NearCascadeInterval 2 should leave one sun cascade per frame");

void ShadowManager::Initialize() {
	
	Logger::Log("Starting the shadows manager...");
	TheShadowManager = new ShadowManager();

	// setup the shadow render passes for the shadowmaps
	TheShadowManager->geometryPass = new ShadowRenderPass();
	TheShadowManager->alphaPass = new AlphaShadowRenderPass();
	TheShadowManager->skinnedGeoPass = new SkinnedGeoShadowRenderPass();
	TheShadowManager->speedTreePass = new SpeedTreeShadowRenderPass();
	TheShadowManager->terrainLODPass = new TerrainLODPass();

	// load the shaders
	TheShadowManager->ShadowMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowMap.vso", "Shadows\\");
	TheShadowManager->ShadowMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowMap.pso", "Shadows\\");
	TheShadowManager->ShadowCubeMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowCubeMap.vso", "Shadows\\");
	TheShadowManager->ShadowCubeMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowCubeMap.pso", "Shadows\\");
	// UNOFFICIAL (from Physically-Based-Reloaded, Fable): opaque casters get a copy of the caster shader without its
	// alpha-test discard. A shader that can discard turns early depth rejection off for every caster drawn with it,
	// and an opaque caster never reaches the discard (TESR_ShadowData.y is 0). Alpha and leaf passes keep the clip shader.
	TheShadowManager->ShadowMapOpaquePixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowMapOpaque.pso", "Shadows\\");
	TheShadowManager->ShadowCubeMapOpaquePixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowCubeMapOpaque.pso", "Shadows\\");
	// UNOFFICIAL 2026-10-14: the light grid's sun map with colours; missing, the grid's bounce takes one ground colour.
	TheShadowManager->ShadowGridColorPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowGridColor.pso", "Shadows\\");
	if (TheShadowManager->ShadowGridColorPixel) TheShadowManager->ShadowGridColorPixel->ClearSamplers = false;
	// A missing copy falls back to the clip shader: the same image, only the early-Z gain is lost.
	if (!TheShadowManager->ShadowMapOpaquePixel) {
		TheShadowManager->ShadowMapOpaquePixel = TheShadowManager->ShadowMapPixel;
		Logger::Log("[WARNING] ShadowMapOpaque.pso did not load; opaque shadow casters use the alpha-test shader.");
	}
	if (!TheShadowManager->ShadowCubeMapOpaquePixel) {
		TheShadowManager->ShadowCubeMapOpaquePixel = TheShadowManager->ShadowCubeMapPixel;
		Logger::Log("[WARNING] ShadowCubeMapOpaque.pso did not load; opaque cube casters use the alpha-test shader.");
	}

    TheShadowManager->ShadowMapBlurVertex = (ShaderRecordVertex*) ShaderRecord::LoadShader("ShadowMapBlur.vso", "Shadows\\");
    TheShadowManager->ShadowMapBlurPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapBlur.pso", "Shadows\\");

	TheShadowManager->ShadowMapClearPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapClear.pso", "Shadows\\");

	// Make sure samplers are not reset on SetCT as that causes errors. A shader whose file is missing
	// loads as null: this used to be dereferenced right here, crashing the game at startup on an install
	// without Shaders\NewVegasReloaded\Shaders\Shadows (seen in a user's log). Shadows are disabled instead.
	ShaderRecord* shadowShaders[] = { TheShadowManager->ShadowMapVertex, TheShadowManager->ShadowMapPixel,
		TheShadowManager->ShadowCubeMapVertex, TheShadowManager->ShadowCubeMapPixel, TheShadowManager->ShadowMapBlurVertex,
		TheShadowManager->ShadowMapBlurPixel, TheShadowManager->ShadowMapClearPixel, TheShadowManager->ShadowMapOpaquePixel,
		TheShadowManager->ShadowCubeMapOpaquePixel };
	TheShadowManager->ShadowShadersLoaded = true;
	for (ShaderRecord* shader : shadowShaders) {
		if (shader) shader->ClearSamplers = false;
		else TheShadowManager->ShadowShadersLoaded = false;
	}
	if (!TheShadowManager->ShadowShadersLoaded)
		Logger::Log("[ERROR]: Could not load one or more of the ShadowMap generation shaders (Shaders\\NewVegasReloaded\\Shaders\\Shadows). "
			"Shadow maps are disabled. Reinstall the mod.");

	// UNOFFICIAL contact hardening: registered before the game's shaders load, so every shader that includes
	// Shadow.hlsl finds it by name. Whether the shaders have it is decided now, at startup (CONTACT_HARDENING).
	TheShaderManager->RegisterConstant("TESR_ContactHardeningData", &TheShadowManager->ContactHardeningData);
	TheShadowManager->ContactHardeningCompiled = TheSettingManager->GetSettingI("Shaders.ContactHardening.Status", "Enabled") != 0;

	UINT ShadowCubeMapSize = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.ShadowCubeMapSize;
	TheShadowManager->ShadowCubeMapViewPort = { 0, 0, ShadowCubeMapSize, ShadowCubeMapSize, 0.0f, 1.0f };

	TheShadowManager->shadowMapsRenderTime = 0;
}


/*
* Returns the given object ref's NiNode if it passes the test for excluded form types, otherwise returns NULL.
*/
NiNode* ShadowManager::GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms) {
	
	if (!Ref) return NULL;
	if (Ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows) return NULL;

	// The form filter runs before GetNode() and the extra-data walk below, both of which are the
	// expensive part of this function. Called once per reference per cascade, so a reference the
	// filter rejects should cost only the flag test and the switch.
	TESForm* Form = Ref->baseForm;
	UInt8 TypeID = Form->formType;
	switch (TypeID) {
	case TESForm::FormType::kFormType_Land:
		return NULL; // land is handled separately
		break;
	case TESForm::FormType::kFormType_Activator:
		if (!Forms->Activators) return NULL; 
		break;
	case TESForm::FormType::kFormType_Apparatus:
		if (!Forms->Apparatus) return NULL;
		break;
	case TESForm::FormType::kFormType_Book:
		if (!Forms->Books) return NULL;
		break;
	case TESForm::FormType::kFormType_Container:
		if (!Forms->Containers) return NULL;
		break;
	case TESForm::FormType::kFormType_Door:
		if (!Forms->Doors) return NULL;
		break;
	case TESForm::FormType::kFormType_Misc:
		if (!Forms->Misc) return NULL;
		break;
	case TESForm::FormType::kFormType_Tree:
		if (!Forms->Trees) return NULL;
		break;
	case TESForm::FormType::kFormType_Furniture:
		if (!Forms->Furniture) return NULL;
		break;
	case TESForm::FormType::kFormType_NPC:
	case TESForm::FormType::kFormType_Creature:
	case TESForm::FormType::kFormType_LeveledCreature:
		if (!Forms->Actors) return NULL;  // D3D9 breaks on actors for some unknown reason
		break;
	case TESForm::FormType::kFormType_Stat:
	case TESForm::FormType::kFormType_StaticCollection:
	case TESForm::FormType::kFormType_MoveableStatic:
		//return NULL;
		if (!Forms->Statics) return NULL;
		break;
	default:
		break;
	}

	// GetNode() is a plain pointer read, so it stays here. The refraction test is NOT: it calls
	// into the engine to walk the reference's extra-data list. Callers apply IsRefracting()
	// after the cascade test instead, where only the references that survive it pay the cost --
	// measured at roughly 2 acceptances per 3400 references for the Near cascade.
	return Ref->GetNode();
}

// Split out of GetRefNode. Walks the extra-data list, so call it as late as possible.
bool ShadowManager::IsRefracting(TESObjectREFR* Ref) {
	ExtraRefractionProperty* RefractionExtraProperty = (ExtraRefractionProperty*)Ref->extraDataList.GetExtraData(BSExtraData::ExtraDataType::kExtraData_RefractionProperty);
	float Refraction = RefractionExtraProperty ? (1 - RefractionExtraProperty->refractionAmount) : 0.0f;
	return Refraction >= 0.5;
}


// Detech shader flags that we do not want.
bool ShadowManager::CheckShaderFlags(NiGeometry* Geometry) {
	BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));

	if (!shaderProp)
		return false;

	return !(shaderProp->GetFlag(BSSP_REFRACTION) ||
		shaderProp->GetFlag(BSSP_FIRE_REFRACTION) ||
		shaderProp->GetFlag(BSSP_DECAL) ||
		shaderProp->GetFlag(BSSP_DYNAMIC_DECAL));
}


// UNOFFICIAL 2026-10-17 (TB-1): true while a cell's land is being accumulated (AccumExteriorCell), so its geometry is noted
// in RenderPass::LandSet for the Terrain Blending shadow drop.
static bool AccumulatingLand = false;

// Detect which pass the object must be added to
void ShadowManager::AccumObject(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand) {
	NiGeometry* geo = static_cast<NiGeometry*>(NiObject);
#if NVR_TEST_TOOLS
	OP_LAND(0);								// land diagnostics (OrthoProbe::LandCount): 0 geometry reached
	if (OrthoProbe::Recording && OrthoProbe::CurrentLand) {	// UNOFFICIAL fixes-b #2: these checks do real work, run them only while recording
		if (!geo->shader) OP_LAND(1);			// 1 no shader
		else if (!CheckShaderFlags(geo)) OP_LAND(2);	// 2 shader flags (decal, refraction) or no shader property
		else if (!geometryPass->TestObject(geo)) OP_LAND(3);	// 3 the opaque pass refuses it (no buffer data / not a lighting property)
	}
#endif
#if NVR_TEST_TOOLS
	if (SunShadowStats::Current) SunShadowStats::Current->geometry++;
#endif
	if (!geo->shader) return; // skip Geometry without a shader

	if (!CheckShaderFlags(geo))
		return;
#if NVR_TEST_TOOLS
	OrthoProbe::NoteGeometry(geo);
#endif

	// UNOFFICIAL 2026-10-17 Skylighting v2 (CleanMap): in the ortho map, geometry that is only alpha BLENDED (no alpha test) does not
	// block the sky: effect meshes (Goodsprings' FXDustWhirlWind01 over the road), glass, smoke cards. Set only around that map.
	if (OrthoSkipBlendOnly) {
		NiAlphaProperty* alpha = (NiAlphaProperty*)geo->GetProperty(NiProperty::PropertyType::kType_Alpha);
		if (alpha && (alpha->flags & NiAlphaProperty::AlphaFlags::ALPHA_BLEND_MASK) && !(alpha->flags & NiAlphaProperty::AlphaFlags::TEST_ENABLE_MASK))
			return;
	}

#if defined(OBLIVION)
	if (geo->m_pcName && !memcmp(geo->m_pcName, "Torch", 5)) return; // No torch geo, it is too near the light and a bad square is rendered.
#endif

	if (skinnedGeoPass->AccumObject(geo)) {}
	else if (speedTreePass->AccumObject(geo)) {}
	else if (Forms->Lod && isLODLand && terrainLODPass->AccumObject(geo)) {}
	else if (Forms->AlphaEnabled && alphaPass->AccumObject(geo)) {}
	else if (AccumulatingLand && DisplaceShadows::Takes(geo) && geometryPass->TestObject(geo) == 1) {	// UNOFFICIAL 2026-10-27 (sun cascades)
		DisplaceShadows::Pass.GeometryList.push(geo);
		if (RenderPass::LandDrop > 0.0f) RenderPass::LandSet.insert(geo);
	}
	else if (geometryPass->AccumObject(geo) && AccumulatingLand && RenderPass::LandDrop > 0.0f) RenderPass::LandSet.insert(geo);
}


// UNOFFICIAL (from Physically-Based-Reloaded, Fable): object LOD inside the loaded cells. Every cascade also walks the
// loaded cells' references as full models (AccumExteriorCell), so their LOD copy cast a second shadow on top -- and a
// ghost one where the full object was swapped or disabled (Base Object Swapper), since the baked LOD never changes.
// The box is BSShaderManager::kLoadedRange (centre x/y, half size z/w), the one terrain LOD is hidden under. A geometry
// is inside when its bound centre is; a node only when its whole bound is, so a block straddling the edge is walked.
static bool IsLodInLoadedCells(NiAVObject* Object) {
#if defined(OBLIVION)
	return false;
#else
	NiBound* Bound = Object->m_kWorldBound;
	if (!Bound) return false;
	const float Reach = Object->IsGeometry() ? 0.0f : Bound->Radius;
	const NiPoint4* Range = BSShaderManager::kLoadedRange;
	return fabsf(Bound->Center.x - Range->x) + Reach < Range->z && fabsf(Bound->Center.y - Range->y) + Reach < Range->w;
#endif
}

// go through the Object children and sort the ones that will be rendered based on their properties
void ShadowManager::AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes *arPlanes) {
	if (!NiObject) return;

	const bool skipLoadedLod = isLOD && !isLand && TheShaderManager->Effects.ShadowsExteriors->Settings.ShadowMaps.LodSkipLoaded;

	std::vector<NiAVObject*>& containers = containersScratch;
	containers.clear();

	NiAVObject* child;
	NiAVObject* object;
	NiNode* Node;

	//list all objects contained, or sort the object if not a container
	if (!NiObject->IsGeometry())
		containers.push_back(NiObject);
	else
		AccumObject(NiObject, Forms, isLand && isLOD);
		

	// Gather geometry
	while (!containers.empty()) {
    	object = containers.back();
    	containers.pop_back();

		if (!object) continue;

		Node = object->IsNiNode();
#if NVR_TEST_TOOLS
		if (isLand && !isLOD) { OP_LAND(4); if (Node && (Node->m_flags & NiAVObject::NiFlags::APP_CULLED)) OP_LAND(5); }	// 4 land nodes, 5 culled
#endif
    	if (!Node || Node->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling containers
		if (!isLand && Node->GetWorldBoundRadius() < Forms->MinRadius) continue;
#if NVR_TEST_TOOLS
		if (SunShadowStats::Current) SunShadowStats::Current->nodes++;
#endif

		if (Node->IsKindOf<NiSwitchNode>()) {
			// NiSwitchNode - only render active children (if exists) to the shadow map.
			NiSwitchNode* SwitchNode = static_cast<NiSwitchNode*>(Node);
			if (SwitchNode->m_iIndex < 0 || SwitchNode->m_iIndex >= Node->m_children.end)
				continue;

			child = Node->m_children.data[SwitchNode->m_iIndex];
			if (!child || (skipLoadedLod && IsLodInLoadedCells(child)))
				continue;
			if (!child->IsGeometry())
				containers.push_back(child);
			else
				AccumObject(child, Forms, false);
			continue;
		}

		for (int i = 0; i < Node->m_children.end; i++) {
			child = Node->m_children.data[i];
#if NVR_TEST_TOOLS
			if (isLand && !isLOD) { OP_LAND(6); if (child && (child->m_flags & NiAVObject::NiFlags::APP_CULLED)) OP_LAND(7); }	// 6 land children, 7 APP_CULLED
#endif
			if (!child || child->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling children
			if (!isLand && child->GetWorldBoundRadius() < Forms->MinRadius) continue;

			// Frustum culling.
#if NVR_TEST_TOOLS
			if (SunShadowStats::Current && arPlanes) SunShadowStats::Current->childTests++;
#endif
			if (arPlanes && (isLand || isLOD)) {
				BSMultiBoundNode* multibound = child->IsMultiBoundNode();

#if NVR_TEST_TOOLS
				if (OrthoProbe::Recording && isLand && !isLOD) { if (multibound) OP_LAND(8); if (multibound && !multibound->spMultiBound->spShape->WithinFrustum(*arPlanes)) OP_LAND(9); }	// 8 multibound, 9 outside
#endif
				if (multibound && !multibound->spMultiBound->spShape->WithinFrustum(*arPlanes)) continue;
			}
			else if (arPlanes && !child->WithinFrustum(arPlanes)) continue;

#if NVR_TEST_TOOLS
			if (OrthoProbe::Recording && isLand && !isLOD && child->IsFadeNode() && static_cast<BSFadeNode*>(child)->FadeAlpha < 0.75f) OP_LAND(10);	// 10 faded
#endif
			if (child->IsFadeNode() && static_cast<BSFadeNode*>(child)->FadeAlpha < TheShaderManager->Effects.ShadowsExteriors->Settings.ShadowMaps.CasterFadeMin) continue; // stop rendering fadenodes below a certain opacity (UNOFFICIAL 2026-10-28: CasterFadeMin, 0.75 = as before)
			if (skipLoadedLod && IsLodInLoadedCells(child)) continue; // object LOD inside the loaded cells
			if (!child->IsGeometry())
				containers.push_back(child);
			else
				AccumObject(child, Forms, isLand && isLOD);
		}
	}
}

// Go through accumulations and render found objects
void ShadowManager::RenderAccums() {
	// UNOFFICIAL 2026-10-27 displacement shadows (DisplaceShadows.h): the land near the camera with its bumps, first: a piece it cannot
	// draw that way goes to the opaque pass below (empty outside the sun cascades)
	DisplaceShadows::Pass.Draw(geometryPass);
#if NVR_TEST_TOOLS
	if (SunShadowStats::Current) {	// UNOFFICIAL 2026-10-21 test tools: the same draws, each pass named for the counts
		SunShadowStats::BeginPass(SunShadowStats::kOpaque); geometryPass->RenderAccum();
		SunShadowStats::BeginPass(SunShadowStats::kLodLand); terrainLODPass->RenderAccum();
		SunShadowStats::BeginPass(SunShadowStats::kAlpha); alphaPass->RenderAccum();
		SunShadowStats::BeginPass(SunShadowStats::kSkinned); skinnedGeoPass->RenderAccum();
		SunShadowStats::BeginPass(SunShadowStats::kTree); speedTreePass->RenderAccum();
		SunShadowStats::CurrentPass = -1;
		return;
	}
#endif
	geometryPass->RenderAccum();
	terrainLODPass->RenderAccum();
	alphaPass->RenderAccum();
	skinnedGeoPass->RenderAccum();
	speedTreePass->RenderAccum();
}


static float SunLandDropScale = 0.0f;	// UNOFFICIAL fixes-b #9: set by the sun cascade loop around its RenderShadowMap call, 0 elsewhere

// UNOFFICIAL 2026-10-17 (TB-1): with Terrain Blending on, the land goes into the sun map BlendDistance * ShadowDrop lower
// (RenderPass::LandDrop), so the part of an object the faded land reveals is not in the land's own shadow.
// fixes-b #9: only the sun cascades drop the land (SunLandDropScale > 0 only around their RenderShadowMap call: not the
// rain / ortho map or the light grid's sun map), and less at a low sun: a lowered ridge casts a shadow shorter by
// drop / tan(elevation), so the drop fades x min(1, 2 sin(elevation)) (full above 30 degrees).
static float ShadowMapLandDrop() {
	TerrainShaders* terrain = TheShaderManager->Shaders.Terrain;
	const bool drop = terrain && terrain->BlendingSettings.Enabled && TheTextureManager->TerrainBlendDepthTexture;
	return drop ? terrain->BlendingSettings.BlendDistance * terrain->BlendingSettings.ShadowDrop * SunLandDropScale : 0.0f;
}

void ShadowManager::BeginShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj, bool keepLandSet) {
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	ShadowMap->ShadowCameraToLight = (*ViewProj);
	TheCameraManager->SetFrustum(&ShadowMap->ShadowMapFrustum, ViewProj);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_DEPTHBIAS, (DWORD)0.0f, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, (DWORD)0.0f, RenderStateArgs);

	RenderPass::LandDrop = ShadowMapLandDrop();
	if (!keepLandSet) RenderPass::LandSet.clear();
}

void ShadowManager::WalkShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap) {
#if NVR_TEST_TOOLS
	SunShadowStats::Cascade* stats = SunShadowStats::Current;
	unsigned long long t0 = stats ? SunShadowStats::Now() : 0;
#endif
	if (ShadowMap->Forms.Lod) {
		AccumChildren(BGSTerrainManager::GetRootLandLODNode(), &ShadowMap->Forms, true, true, &ShadowMap->ShadowMapFrustumPlanes);
		AccumChildren(BGSTerrainManager::GetRootObjectLODNode(), &ShadowMap->Forms, false, true, &ShadowMap->ShadowMapFrustumPlanes);
	}
#if NVR_TEST_TOOLS
	unsigned long long t1 = stats ? SunShadowStats::Now() : 0;
#endif

	if (Player->GetWorldSpace()) {
		GridCellArray* CellArray = Tes->gridCellArray;
		UInt32 CellArraySize = CellArray->size * CellArray->size;

		for (UInt32 i = 0; i < CellArraySize; i++) {
			AccumExteriorCell(CellArray->GetCell(i), ShadowMap);
		}
	}
	else {
		AccumExteriorCell(Player->parentCell, ShadowMap);
	}
#if NVR_TEST_TOOLS
	if (stats) { const unsigned long long t2 = SunShadowStats::Now(); stats->tWalkLod += t1 - t0; stats->tWalkCells += t2 - t1; }
#endif
}

void ShadowManager::DrawShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, bool keepLandSet) {
	IDirect3DDevice9* Device = TheRenderManager->device;
#if NVR_TEST_TOOLS
	SunShadowStats::Cascade* stats = SunShadowStats::Current;
	unsigned long long t2 = stats ? SunShadowStats::Now() : 0;
#endif

	Device->SetViewport(&ShadowMap->ShadowMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	if (ShadowMap->CustomClearRequired)
		ClearShadowCascade(&ShadowMap->ShadowMapViewPort, &ShadowMap->ClearColor);

	RenderPass::TwoSided = TheSettingManager->GetSettingI("Shaders.ShadowsExteriors.Main", "TwoSidedCasters") != 0;	// UNOFFICIAL
#if NVR_TEST_TOOLS
	unsigned long long t3 = stats ? SunShadowStats::Now() : 0;
#endif
	RenderAccums();
#if NVR_TEST_TOOLS
	if (stats) {
		const unsigned long long t4 = SunShadowStats::Now();
		stats->tClear += t3 - t2; stats->tDraws += t4 - t3;
	}
#endif
	RenderPass::TwoSided = false;
	RenderPass::LandDrop = 0.0f;
	if (!keepLandSet) RenderPass::LandSet.clear();
}

void ShadowManager::RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj) {
	BeginShadowMap(ShadowMap, ViewProj);
	WalkShadowMap(ShadowMap);
	DrawShadowMap(ShadowMap);
}


void ShadowManager::AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap) {
	if (!Cell || Cell->IsInterior())
		return;

	if (ShadowMap->Forms.Terrain) {
		AccumulatingLand = true;
#if NVR_TEST_TOOLS
		OrthoProbe::CurrentLand = true; OrthoProbe::CurrentRef = nullptr;
		if (OrthoProbe::Recording) { OrthoProbe::LandCount[11]++; if (!Cell->GetChildNode(TESObjectCELL::kCellNode_Land)) OrthoProbe::LandCount[12]++; }	// 11 cells, 12 without a land node
#endif
		AccumChildren(Cell->GetChildNode(TESObjectCELL::kCellNode_Land), &ShadowMap->Forms, true, false, &ShadowMap->ShadowMapFrustumPlanes);
		AccumulatingLand = false;
#if NVR_TEST_TOOLS
		OrthoProbe::CurrentLand = false;
#endif
	}

	TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &ShadowMap->Forms);
#if NVR_TEST_TOOLS
		if (SunShadowStats::Current) { SunShadowStats::Current->refs++; if (RefNode) SunShadowStats::Current->refNodes++; }
#endif

		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		if (RefNode->WithinFrustum(&ShadowMap->ShadowMapFrustumPlanes) && !IsRefracting(Entry->item)) {
#if NVR_TEST_TOOLS
			OrthoProbe::CurrentRef = Entry->item;
			if (SunShadowStats::Current) { SunShadowStats::Current->refsIn++; SunShadowStats::CheckRef(RefNode, Entry->item); }
#endif
			AccumChildren(RefNode, &ShadowMap->Forms, false, false, &ShadowMap->ShadowMapFrustumPlanes);
#if NVR_TEST_TOOLS
			OrthoProbe::CurrentRef = nullptr;
#endif
		}

		Entry = Entry->next;
	}
}


// UNOFFICIAL 2026-10-21 (sunshadow) SunCascadeOneWalk. Measured (evidence\sunshadow\LOG.md): every sun cascade walked every
// reference of the loaded cells again (0.04-0.08 ms per cascade on a fast PC, 25-30% of the cascades' CPU time in open country).
// The walk below visits each reference and node once and tests it against every cascade in 'mask' (bit c = cascade c), with each
// cascade's own forms, size limit (MinRadius) and cull planes, exactly as AccumExteriorCell / AccumChildren / AccumObject test it
// for one cascade. A subtree is left the moment no cascade wants it. The order is that of the old walk: the same sections (land
// LOD, object LOD, then per cell its land and its references), and within a reference the same depth-first order (containers on
// a stack, popped last-in first, geometry taken as met), so every cascade's casters come out in the order its own walk found them:
// pushed into the passes in that order (PushSunCasters), every cascade draws exactly what and as it did. SunCascadeVerify 1 checks
// that in game, frame by frame.
static inline unsigned ForEachSunCascade(unsigned mask, unsigned& bit) {	// lowest set bit index, cleared from mask
	unsigned long index;
	_BitScanForward(&index, mask);
	bit = index;
	return mask & (mask - 1);
}

void ShadowManager::WalkSunCascades(unsigned mask) {
	for (int c = 0; c < 4; c++) SunCasters[c].clear();
	if (!mask) return;
	unsigned lodMask = 0;
	for (unsigned m = mask, c; m; ) { m = ForEachSunCascade(m, c); if (SunForms[c]->Lod) lodMask |= 1u << c; }
	if (lodMask) {
		WalkSunChildren(BGSTerrainManager::GetRootLandLODNode(), lodMask, true, true);
		WalkSunChildren(BGSTerrainManager::GetRootObjectLODNode(), lodMask, false, true);
	}
	if (Player->GetWorldSpace()) {
		GridCellArray* CellArray = Tes->gridCellArray;
		const UInt32 CellArraySize = CellArray->size * CellArray->size;
		for (UInt32 i = 0; i < CellArraySize; i++) WalkSunCell(CellArray->GetCell(i), mask);
	}
	else WalkSunCell(Player->parentCell, mask);
}

void ShadowManager::WalkSunCell(TESObjectCELL* Cell, unsigned mask) {
	if (!Cell || Cell->IsInterior()) return;

	unsigned landMask = 0;
	for (unsigned m = mask, c; m; ) { m = ForEachSunCascade(m, c); if (SunForms[c]->Terrain) landMask |= 1u << c; }
	if (landMask) {
		AccumulatingLand = true;
#if NVR_TEST_TOOLS
		OrthoProbe::CurrentLand = true; OrthoProbe::CurrentRef = nullptr;
#endif
		WalkSunChildren(Cell->GetChildNode(TESObjectCELL::kCellNode_Land), landMask, true, false);
		AccumulatingLand = false;
#if NVR_TEST_TOOLS
		OrthoProbe::CurrentLand = false;
#endif
	}

	for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
		// GetRefNode per cascade: its form filter is the cascade's own (the node, when there is one, is the same).
		NiNode* RefNode = nullptr;
		unsigned inMask = 0;
		for (unsigned m = mask, c; m; ) {
			m = ForEachSunCascade(m, c);
			NiNode* node = GetRefNode(Entry->item, SunForms[c]);
			if (!node) continue;
			RefNode = node;
			if (node->WithinFrustum(SunPlanes[c])) inMask |= 1u << c;
		}
		if (!inMask || IsRefracting(Entry->item)) continue;
#if NVR_TEST_TOOLS
		OrthoProbe::CurrentRef = Entry->item;
#endif
		WalkSunChildren(RefNode, inMask, false, false);
#if NVR_TEST_TOOLS
		OrthoProbe::CurrentRef = nullptr;
#endif
	}
}

void ShadowManager::WalkSunChildren(NiAVObject* NiObject, unsigned mask, bool isLand, bool isLOD) {
	if (!NiObject) return;

	const bool skipLoadedLod = isLOD && !isLand && TheShaderManager->Effects.ShadowsExteriors->Settings.ShadowMaps.LodSkipLoaded;
	// The cascades of 'm' whose size limit this object's bound reaches (land has none).
	auto bigEnough = [this, isLand](NiAVObject* object, unsigned m) {
		if (isLand) return m;
		const float radius = object->GetWorldBoundRadius();
		unsigned out = 0;
		for (unsigned c; m; ) { m = ForEachSunCascade(m, c); if (radius >= SunForms[c]->MinRadius) out |= 1u << c; }
		return out;
	};

	std::vector<std::pair<NiAVObject*, unsigned>>& containers = SunContainers;
	containers.clear();

	if (!NiObject->IsGeometry())
		containers.emplace_back(NiObject, mask);
	else
		WalkSunObject(NiObject, mask, isLand && isLOD);

	while (!containers.empty()) {
		NiAVObject* object = containers.back().first;
		unsigned m = containers.back().second;
		containers.pop_back();

		if (!object) continue;
		NiNode* Node = object->IsNiNode();
		if (!Node || Node->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling containers
		m = bigEnough(Node, m);
		if (!m) continue;

		if (Node->IsKindOf<NiSwitchNode>()) {
			// NiSwitchNode - only render active children (if exists) to the shadow map.
			NiSwitchNode* SwitchNode = static_cast<NiSwitchNode*>(Node);
			if (SwitchNode->m_iIndex < 0 || SwitchNode->m_iIndex >= Node->m_children.end)
				continue;

			NiAVObject* child = Node->m_children.data[SwitchNode->m_iIndex];
			if (!child || (skipLoadedLod && IsLodInLoadedCells(child)))
				continue;
			if (!child->IsGeometry())
				containers.emplace_back(child, m);
			else
				WalkSunObject(child, m, false);
			continue;
		}

		for (int i = 0; i < Node->m_children.end; i++) {
			NiAVObject* child = Node->m_children.data[i];
			if (!child || child->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling children
			unsigned cm = bigEnough(child, m);
			if (!cm) continue;

			// Frustum culling, per cascade.
			if (isLand || isLOD) {
				BSMultiBoundNode* multibound = child->IsMultiBoundNode();
				if (multibound) {
					unsigned in = 0;
					for (unsigned t = cm, c; t; ) { t = ForEachSunCascade(t, c); if (multibound->spMultiBound->spShape->WithinFrustum(*SunPlanes[c])) in |= 1u << c; }
					cm = in;
				}
			}
			else {
				unsigned in = 0;
				for (unsigned t = cm, c; t; ) { t = ForEachSunCascade(t, c); if (child->WithinFrustum(SunPlanes[c])) in |= 1u << c; }
				cm = in;
			}
			if (!cm) continue;

			if (child->IsFadeNode() && static_cast<BSFadeNode*>(child)->FadeAlpha < TheShaderManager->Effects.ShadowsExteriors->Settings.ShadowMaps.CasterFadeMin) continue; // stop rendering fadenodes below a certain opacity (UNOFFICIAL 2026-10-28: CasterFadeMin, 0.75 = as before)
			if (skipLoadedLod && IsLodInLoadedCells(child)) continue; // object LOD inside the loaded cells
			if (!child->IsGeometry())
				containers.emplace_back(child, cm);
			else
				WalkSunObject(child, cm, isLand && isLOD);
		}
	}
}

// AccumObject for the cascades of 'mask': the tests that do not depend on the cascade once, the pass chosen per cascade (its forms
// decide the terrain LOD and alpha passes), in AccumObject's order.
void ShadowManager::WalkSunObject(NiAVObject* NiObject, unsigned mask, bool isLODLand) {
	NiGeometry* geo = static_cast<NiGeometry*>(NiObject);
	if (!geo->shader) return; // skip Geometry without a shader
	if (!CheckShaderFlags(geo)) return;
#if NVR_TEST_TOOLS
	OrthoProbe::NoteGeometry(geo);
#endif
	auto add = [this](unsigned m, NiGeometry* g, UInt8 pass) {
		for (unsigned c; m; ) { m = ForEachSunCascade(m, c); SunCasters[c].push_back({ g, pass }); }
	};
	const int skinned = skinnedGeoPass->TestObject(geo);
	if (skinned) { if (skinned == 1) add(mask, geo, kSunSkinned); return; }
	if (speedTreePass->TestObject(geo)) { add(mask, geo, kSunTree); return; }

	int lodLand = -1, alpha = -1, opaque = -1;	// -1: not tested yet
	unsigned lodMask = 0, alphaMask = 0, opaqueMask = 0;
	for (unsigned m = mask, c; m; ) {
		m = ForEachSunCascade(m, c);
		const ShadowsExteriorEffect::FormsStruct* Forms = SunForms[c];
		if (Forms->Lod && isLODLand) {
			if (lodLand < 0) lodLand = terrainLODPass->TestObject(geo);
			if (lodLand) { lodMask |= 1u << c; continue; }
		}
		if (Forms->AlphaEnabled) {
			if (alpha < 0) alpha = alphaPass->TestObject(geo);
			if (alpha) { alphaMask |= 1u << c; continue; }
		}
		if (opaque < 0) opaque = geometryPass->TestObject(geo);
		if (opaque) opaqueMask |= 1u << c;
	}
	// One push per cascade, as AccumObject's (the pass order within a geometry does not matter: one geometry, one pass per cascade).
	// UNOFFICIAL 2026-10-27: cell land near the camera goes to the displaced land's pass (DisplaceShadows.h) instead of the opaque one.
	const UInt8 opaquePass = opaqueMask && AccumulatingLand && DisplaceShadows::Takes(geo) ? kSunDisplaceLand : kSunOpaque;
	for (unsigned m = mask, c; m; ) {
		m = ForEachSunCascade(m, c);
		const unsigned bit = 1u << c;
		if (lodMask & bit) SunCasters[c].push_back({ geo, kSunLodLand });
		else if (alphaMask & bit) SunCasters[c].push_back({ geo, kSunAlpha });
		else if (opaqueMask & bit) SunCasters[c].push_back({ geo, opaquePass });
	}
	if (opaqueMask && AccumulatingLand && RenderPass::LandDrop > 0.0f) RenderPass::LandSet.insert(geo);
}

RenderPass* ShadowManager::SunPassOf(UInt8 pass) {
	switch (pass) {
	case kSunOpaque: return geometryPass;
	case kSunLodLand: return terrainLODPass;
	case kSunAlpha: return alphaPass;
	case kSunSkinned: return skinnedGeoPass;
	case kSunDisplaceLand: return &DisplaceShadows::Pass;	// UNOFFICIAL 2026-10-27
	default: return speedTreePass;
	}
}

void ShadowManager::PushSunCasters(int cascade) {
	for (const SunCaster& caster : SunCasters[cascade]) SunPassOf(caster.pass)->GeometryList.push(caster.geometry);
}

// UNOFFICIAL 2026-10-21 (sunshadow) SunCascadeSkipFlat: the cascade's projection maps every point onto one line (or one point): the
// x or y column of its view-projection is all zero. That happens when the sun is exactly overhead, (0, 0, 1) -- QuantizeSun rounds
// any sun within half a PitchStepSize of the zenith to that -- because GetCascadeViewProj's view matrix takes (0, 0, 1) as its up
// direction: parallel to the sun, the view's side axes come out as zero vectors. Every caster then turns into zero-area triangles
// (the shadow vertex shader puts every caster through that matrix): nothing can be drawn, the map stays as cleared, and its cull
// planes are zero too (the side planes cull nothing: every cascade took nearly every object of the loaded cells).
static bool FlatCascade(const D3DXMATRIX& m) {
	return (m._11 == 0.0f && m._21 == 0.0f && m._31 == 0.0f) || (m._12 == 0.0f && m._22 == 0.0f && m._32 == 0.0f);
}

#if NVR_TEST_TOOLS
// SunCascadeVerify 2: one cascade's region of the atlas read back (resolved first when the atlas is multisampled).
static unsigned BytesPerTexel(D3DFORMAT format) {
	switch (format) {
	case D3DFMT_R16F: case D3DFMT_L16: return 2;
	case D3DFMT_A16B16G16R16F: case D3DFMT_A16B16G16R16: case D3DFMT_G32R32F: return 8;
	case D3DFMT_A32B32G32R32F: return 16;
	default: return 4;	// R32F, G16R16F, G16R16, A8R8G8B8, ...
	}
}
static bool ReadCascadeRegion(ShadowsExteriorEffect* Shadows, const D3DVIEWPORT9& viewport, std::vector<UInt8>& out) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	static IDirect3DSurface9* resolved = nullptr;
	static IDirect3DSurface9* readback = nullptr;
	static D3DSURFACE_DESC have = {};
	if (!Shadows->ShadowAtlasSurface) return false;
	D3DSURFACE_DESC d = {};
	Shadows->ShadowAtlasSurface->GetDesc(&d);
	if (!resolved || have.Width != d.Width || have.Height != d.Height || have.Format != d.Format) {
		if (resolved) { resolved->Release(); resolved = nullptr; }
		if (readback) { readback->Release(); readback = nullptr; }
		if (FAILED(Device->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, NULL)) ||
			FAILED(Device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &readback, NULL))) {
			if (resolved) { resolved->Release(); resolved = nullptr; }
			return false;
		}
		have = d;
	}
	RECT r = { (LONG)viewport.X, (LONG)viewport.Y, (LONG)(viewport.X + viewport.Width), (LONG)(viewport.Y + viewport.Height) };
	IDirect3DSurface9* source = Shadows->ShadowAtlasSurfaceMSAA ? Shadows->ShadowAtlasSurfaceMSAA : Shadows->ShadowAtlasSurface;
	if (FAILED(Device->StretchRect(source, &r, resolved, &r, D3DTEXF_NONE))) return false;
	if (FAILED(Device->GetRenderTargetData(resolved, readback))) return false;
	D3DLOCKED_RECT locked = {};
	if (FAILED(readback->LockRect(&locked, &r, D3DLOCK_READONLY))) return false;
	const unsigned row = viewport.Width * BytesPerTexel(d.Format);
	out.resize((size_t)row * viewport.Height);
	for (UINT y = 0; y < viewport.Height; y++) memcpy(&out[(size_t)y * row], (const UInt8*)locked.pBits + (size_t)y * locked.Pitch, row);
	readback->UnlockRect();
	return true;
}

// SunCascadeVerify: what was compared, summed and logged every 240 checked frames.
struct SunVerifyStats { unsigned frames, cascades, listSame, listDiff, mapSame, mapDiff, mapFailed, flat; unsigned long long casters; };
static SunVerifyStats SunVerify = {};
#endif


void ShadowManager::RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex) {
	NiSpotLight* pNiLight = Lights[LightIndex];
	if (pNiLight == NULL || !pNiLight->CastShadows) return;

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	D3DXMATRIX View, Proj;

	NiPoint3* LightPos = &pNiLight->m_worldTransform.pos;
	float Radius = pNiLight->Spec.r;

#if defined(OBLIVION)
	if (pNiLight->CanCarry)
		Radius = 256.0f;
#endif

	D3DXVECTOR3 Up = D3DXVECTOR3(0, 0, 1);
	D3DXVECTOR3 Eye = LightPos->toD3DXVEC3();
	Eye.x -= TheRenderManager->CameraPosition.x;
	Eye.y -= TheRenderManager->CameraPosition.y;
	Eye.z -= TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(pNiLight->OuterSpotAngle * 2), 1.0f, 0.1f, Radius);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, 0, RenderStateArgs);

	D3DXVECTOR3 CameraDirection = D3DXVECTOR3(pNiLight->m_worldTransform.rot.data[0][0], pNiLight->m_worldTransform.rot.data[1][0], pNiLight->m_worldTransform.rot.data[2][0]);
	D3DXVECTOR3 At = Eye + CameraDirection;

	TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms);
		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		// Detect if the object is in front of the light in the direction of the current face
		// TODO: improve to base on frustum
		D3DXVECTOR3 ObjectPos = RefNode->m_worldTransform.pos.toD3DXVEC3();
		D3DXVECTOR3 ObjectToLight = ObjectPos - LightPos->toD3DXVEC3();

		D3DXVec3Normalize(&ObjectToLight, &ObjectToLight);
		bool inFront = D3DXVec3Dot(&ObjectToLight, &CameraDirection) > 0;
		if (inFront && RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius()) 
			AccumChildren(RefNode, &Settings->Forms, false, false);

		Entry = Entry->next;
	}

	D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
	Shadows->Constants.ShadowViewProj = View * Proj;
	TheShaderManager->SpotLightWorldToLightMatrix[LightIndex] = (Shadows->Constants.ShadowViewProj);

	Device->SetRenderTarget(0, Shadows->Textures.ShadowSpotlightSurface[LightIndex]);
	Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

	Device->SetViewport(&ShadowCubeMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	RenderAccums();
}


// UNOFFICIAL [Shaders.ShadowsInteriors.Main] PlayerInsideLamp: where a lamp casts the player's own shadow from when its
// light point is inside the player's body (or within PlayerLampMargin of it). A point inside a model is blocked on every
// side, so the player threw room-sized shadows onto every wall, often where the game places a light with no fixture and
// the player walks straight through it. The player's shadow is cast instead from a point just outside the body, chosen
// by where the light is: entering from the side, the point keeps the light's direction from the body and rises up the
// side as the light goes deeper, then moves in over the head; with the light in the middle it is straight above the head.
// So a lamp walked into from the front casts the player's shadow behind, then a shorter one, then a pool round the feet,
// then one in front as it leaves behind: no jump anywhere. The body is an upright cylinder round the player's feet, 32
// units each way times the player's scale plus the margin, up to the top of the model's bound plus the margin; at its top
// and bottom the move eases in over the margin. Returns false for a lamp outside it (nothing changes); offset: the cast
// point minus the light.
static bool PlayerLampOffset(const ShadowSceneLight* light, const NiPoint3* lightPos, D3DXVECTOR3& offset) {
	offset = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	bool now = false;
	const ShadowsExteriorEffect::InteriorsStruct& interiors = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors;
	if (Player && light && lightPos && interiors.PlayerInsideLamp) {
		const float scale = Player->scale > 0.0f ? Player->scale : 1.0f;
		const float margin = interiors.PlayerLampMargin;
		const NiPoint3& feet = Player->pos;
		float top = feet.z + 128.0f * scale;
		if (NiNode* node = Player->GetNode())
			if (NiBound* bound = node->GetWorldBound())
				top = (std::min)(bound->Center.z + bound->Radius, feet.z + 200.0f * scale);
		const float zTop = top + margin, zBottom = feet.z - margin, reach = 32.0f * scale + margin;
		const float dx = lightPos->x - feet.x, dy = lightPos->y - feet.y, d = sqrtf(dx * dx + dy * dy);
		if (d < reach && lightPos->z > zBottom && lightPos->z < zTop) {
			now = true;
			auto smooth = [](float a, float b, float x) { const float s = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return s * s * (3.0f - 2.0f * s); };
			const float depth = 1.0f - d / reach;									// 0 at the side, 1 in the middle
			const float across = reach * (1.0f - smooth(0.5f, 1.0f, depth));		// the cast point's distance from the axis
			const float height = lightPos->z + (zTop - lightPos->z) * smooth(0.0f, 0.5f, depth);
			const float ux = d > 0.001f ? dx / d : 0.0f, uy = d > 0.001f ? dy / d : 0.0f;
			const float ends = (std::min)(std::clamp((zTop - lightPos->z) / margin, 0.0f, 1.0f), std::clamp((lightPos->z - zBottom) / margin, 0.0f, 1.0f));
			offset = D3DXVECTOR3(feet.x + ux * across - lightPos->x, feet.y + uy * across - lightPos->y, height - lightPos->z) * ends;
		}
	}
	// LogLamps: when a lamp starts and stops being inside.
	static const ShadowSceneLight* inside[ShadowCubeMapsMax] = {};
	int found = -1, empty = -1;
	for (int i = 0; i < ShadowCubeMapsMax; i++) {
		if (inside[i] == light) found = i;
		else if (!inside[i] && empty < 0) empty = i;
	}
	if (now && found < 0 && empty >= 0) inside[empty] = light;
	if (!now && found >= 0) inside[found] = nullptr;
	if (now != (found >= 0) && lightPos && interiors.Forward.LogLamps)
		Logger::Log("UNOFFICIAL PlayerInsideLamp: lamp %p at %.0f %.0f %.0f %s", (const void*)light, lightPos->x, lightPos->y, lightPos->z,
			now ? "is inside the player: the player is left out of its cube map" : "is outside the player again");
	return now;
}

bool ShadowManager::RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex, CubeLayer Layer) {
	if (Lights[LightIndex] == NULL) return true; // No light at current index
	
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	// UNOFFICIAL RedrawActorsOnly: the moving layer starts from the static cube map, copied face by face (same size and
	// format). A failed copy draws nothing; the caller redraws the lamp whole.
	// UNOFFICIAL CubeFaceCopySkip (above): the copies are made after the caster walk, face by face, where needed.
	const bool cullFaces = CubeCullAllowed && Layer != CubeLayer::StaticOnly;	// UNOFFICIAL CubeFaceViewCull (above)
	const bool faceSkip = Layer == CubeLayer::MovingOnto && (TheSettingManager->SettingsMain.Main.CubeFaceCopySkip || cullFaces);
	const bool copySkip = TheSettingManager->SettingsMain.Main.CubeFaceCopySkip;
	if (Layer == CubeLayer::StaticOnly)
		StaticCubeSerials[Shadows->Textures.ShadowCubeMapStaticTexture[LightIndex]] = ++StaticCubeSerialNext;	// its contents change now
	if (!faceSkip) ForgetLiveFaces(LightIndex);	// a whole redraw, or the copies and draws below without the records
	if (Layer == CubeLayer::MovingOnto && !faceSkip) {
		for (int Face = 0; Face < 6; Face++) {
			IDirect3DSurface9* from = Shadows->Textures.ShadowCubeMapStaticSurface[LightIndex][Face];
			IDirect3DSurface9* to = Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face];
			if (!from || !to || FAILED(Device->StretchRect(from, NULL, to, NULL, D3DTEXF_NONE))) return false;
		}
	}
	float Radius = 0.0f;
	float MinRadius = Settings->Forms.MinRadius;
	NiPoint3* LightPos = NULL;
	D3DXMATRIX View, Proj;
	D3DXVECTOR3 Eye, At, Up, CameraDirection;

	NiPointLight* pNiLight = Lights[LightIndex]->sourceLight;

	LightPos = CubeEyeOverride ? CubeEyeOverride : &pNiLight->m_worldTransform.pos;	// UNOFFICIAL LampShadowMoveTolerance (null: as before)
	Radius = CubeRadiusOverride > 0.0f ? CubeRadiusOverride : pNiLight->Spec.r * Shadows->Settings.Interiors.LightRadiusMult;	// (InvisibleShadowFromLamp)
#if defined(OBLIVION)
	if (pNiLight->CanCarry)
		Radius = 256.0f;
#endif
	// UNOFFICIAL (2026-10-07): New Vegas sets NiDynamicEffect::CanCarry itself for every light whose form has the "can be
	// carried" flag (NVR's hook that set it is off here), and the Oblivion torch rule above gave those lamps a 256-unit
	// cube map whatever their radius: everything they lit further out than 256 units had its shadow end on a circle,
	// with the light going on. The cube now covers Spec.r x LightRadiusMult for every lamp, the radius the lookups use.
	Eye.x = LightPos->x - TheRenderManager->CameraPosition.x;
	Eye.y = LightPos->y - TheRenderManager->CameraPosition.y;
	Eye.z = LightPos->z - TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(90.0f), 1.0f, 0.1f, Radius);
	// UNOFFICIAL CubeFaceViewCull: the faces that can be seen this frame (all six unless culling applies to this draw)
	unsigned char viewFaces = 63;
	if (cullFaces) {
		static const D3DXVECTOR3 CullDirection[6] = { D3DXVECTOR3(1.0f, 0.0f, 0.0f), D3DXVECTOR3(-1.0f, 0.0f, 0.0f),
			D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, -1.0f, 0.0f), D3DXVECTOR3(0.0f, 0.0f, -1.0f), D3DXVECTOR3(0.0f, 0.0f, 1.0f) };
		for (int face = 0; face < 6; face++)
			if (CubeFaceOutOfView(LightPos, Radius, CullDirection[face])) viewFaces &= (unsigned char)~(1 << face);
		if (viewFaces != 63) CubeCulledAny = true;
	}
	D3DXVECTOR3 playerOffset;
	const bool lampInsidePlayer = PlayerLampOffset(Lights[LightIndex], LightPos, playerOffset);	// UNOFFICIAL, see above

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	// UNOFFICIAL 2026-10-29 [Shaders.ShadowsInteriors.Main] TwoSidedCasters (live; user: lamp shadows "that seem to trace the inside
	// of a mesh", rings under cups and glasses): each caster drew with its own cull mode (RenderPass::RenderGeometry), and the cube
	// faces are mirrored (lookup (1, 1, -1), ShadowCubeMap.vso), so the side kept was the one turned AWAY from the lamp: the map held
	// a cup's inner walls and the underside of its bottom (at the table's height, lost in the bias), and the shadow came out as a ring.
	// Double-sided, as the sun's maps (TwoSidedCasters there): the face nearest the lamp wins the depth test. Off: as before.
	struct TwoSidedScope {
		explicit TwoSidedScope(bool on) { RenderPass::TwoSided = on; }
		~TwoSidedScope() { RenderPass::TwoSided = false; }
	} twoSidedScope(TheSettingManager->GetSettingI("Shaders.ShadowsInteriors.Main", "TwoSidedCasters") != 0);
	
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	// UNOFFICIAL (2026-10-07 review): the light's caster list is walked ONCE for all six faces; it used to be walked once
	// per face, with the same property lookups and tests each time. Every test but the face test is the same for every
	// face, so each caster that passes them remembers which faces its bound touches and which pass draws it, and the face
	// loop only pushes. The old form-based walk (no caster list from the game) stays per face as it was.
	static const D3DXVECTOR3 FaceDirection[6] = { D3DXVECTOR3(1.0f, 0.0f, 0.0f), D3DXVECTOR3(-1.0f, 0.0f, 0.0f),
		D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, -1.0f, 0.0f), D3DXVECTOR3(0.0f, 0.0f, -1.0f), D3DXVECTOR3(0.0f, 0.0f, 1.0f) };
	static const D3DXVECTOR3 FaceUp[6] = { D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, 1.0f, 0.0f),
		D3DXVECTOR3(0.0f, 0.0f, 1.0f), D3DXVECTOR3(0.0f, 0.0f, -1.0f), D3DXVECTOR3(0.0f, 1.0f, 0.0f), D3DXVECTOR3(0.0f, 1.0f, 0.0f) };
	const bool useGeometryList = Lights[LightIndex]->kGeometryList.start != nullptr;
	cubeCasters.clear();
	std::unordered_map<int, int> lampModelCache;	// UNOFFICIAL LampModelShadows: this light's answer per lamp model (LampFixtures::NearLampModel)
	std::unordered_map<int, int> noShadowCache;		// UNOFFICIAL NoShadowLampModels (2026-10-27): the same for LampFixtures::NoShadowLampModel
	if (useGeometryList) {
		// Since this is pure geometry, getting reference data will be difficult (read: slow)
		for (auto iter = Lights[LightIndex]->kGeometryList.start; iter; iter = iter->next) {
			NiGeometry* geo = iter->data;
			if (!geo || geo->m_flags & NiAVObject::APP_CULLED)
				continue;

			BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(geo->GetProperty(NiProperty::kType_Shade));
			NiMaterialProperty* matProp = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));

			if (!shaderProp)
				continue;

			// UNOFFICIAL RedrawActorsOnly: the static layer takes what never moves, the moving layer the rest.
			if (Layer != CubeLayer::All && IsMovingCaster(geo, shaderProp) != (Layer == CubeLayer::MovingOnto))
				continue;

			// Skip refraction and fire refraction.
			if (!CheckShaderFlags(geo))
				continue;

			// UNOFFICIAL LampGlassShadows false (2026-10-20): a lamp's glowing glass, bulb or glow sprite is a light source, not a
			// blocker; drawn solid it threw the whole shade outline of a sconce or chandelier onto the wall (LampFixtures::LampPart).
			if (!Settings->Forward.LampGlassShadows && LampFixtures::LampPart(geo))
				continue;

			// UNOFFICIAL InvisibleShadowFromLamp (2026-10-20): drawn from a lamp, that lamp (its model, and every glowing lamp part) is
			// the light source here, not a blocker.
			if (CubeRelocated && ((CubeSkipModel >= 0 && LampFixtures::ModelIndexOf(geo) == CubeSkipModel) || LampFixtures::LampPart(geo)))
				continue;

			// UNOFFICIAL LampModelShadows false (2026-10-20): the lamp models near this light (whole models: a sconce, a chandelier, a ceiling
			// fan, an arm lamp) cast nothing into its map. The level designers put their invisible lights right next to the lamps, and from
			// there a lamp threw its whole outline, hard and room-sized; every other shadow of the light stays (LampFixtures::NearLampModel).
			if (!Settings->Forward.LampModelShadows && !TheShaderManager->GameState.isExterior &&
				LampFixtures::NearLampModel(geo, pNiLight, LightPos, Settings->Forward.LampModelDistance, lampModelCache))
				continue;
			// UNOFFICIAL NoShadowLampModels (2026-10-27): lamp models named there (default: lanterns) cast no shadow for the lights in or
			// near them, their own included (a lantern's frame threw a moving cage of shadows round the room). Other lamps keep theirs.
			if (!TheShaderManager->GameState.isExterior &&
				LampFixtures::NoShadowLampModel(geo, LightPos, Settings->Forward.LampModelDistance, noShadowCache))
				continue;

			bool isFirstPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kFirstPerson);
			bool isThirdPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kThirdPerson);

			// Skip objects if they are barely visible.
			if ((matProp && matProp->fAlpha < 0.05f))
				continue;

			// Also skip viewmodel due to issues, and render player's model only in 3rd person
			if (isFirstPerson) continue;

			if (!Player->isThirdPerson && !Settings->PlayerShadowFirstPerson && isThirdPerson)
				continue;

			if (Player->isThirdPerson && !Settings->PlayerShadowThirdPerson && isThirdPerson)
				continue;

			// UNOFFICIAL PlayerInsideLamp (v3, 2026-10-07): a lamp inside the player's body leaves the player out of its
			// cube map. v2 drew the player into the same cube map from a cast point outside the body, but the apply pass
			// and the forward path read the cube by direction from the lamp itself, so what they saw was a body displaced
			// by up to its own height: a half-disc of shadow on the floor round the lamp (user's screenshot, Rivet City).
			if (lampInsidePlayer && isThirdPerson) continue;

			// The faces the caster's bound reaches (TouchesShadowFace: every face for skinned or unbounded geometry); only
			// lighting-property geometry is face-tested, as before. SpeedTree shaders move vertices beyond their static bounds.
			unsigned char faces = 63;
			if (shaderProp->IsLightingProperty()) {
				// UNOFFICIAL SkinnedShadowFaceTest: a skinned caster whose skeleton bound lies wholly beyond the cube map's far
				// plane (the lamp's radius) cannot reach any face; the game's per-light list was built from its stale bound.
				if (geo->skinInstance && TheSettingManager->SettingsMain.Main.SkinnedShadowFaceTest) {
					NiBound skinned;
					if (SkinnedWorldBound(geo, skinned)) {
						const float dx = skinned.Center.x - LightPos->x, dy = skinned.Center.y - LightPos->y, dz = skinned.Center.z - LightPos->z;
						if (sqrtf(dx * dx + dy * dy + dz * dz) - skinned.Radius > Radius) { statSkinnedOutOfRange++; continue; }
					}
					else statSkinnedNoBound++;
				}
				faces = 0;
				for (int face = 0; face < 6; face++)
					if (TouchesShadowFace(geo, LightPos, FaceDirection[face])) faces |= (unsigned char)(1 << face);
				if (!faces) continue;
			}
			if (geo->skinInstance) {
				statSkinnedCasters++;
				for (int face = 0; face < 6; face++) if (faces & (1 << face)) statSkinnedFaces++;
			}

			// The pass that draws it: the AccumObject chain of before, without the pushes (RenderPass::TestObject).
			RenderPass* pass = nullptr;
			int test = skinnedGeoPass->TestObject(geo);
			if (test == 1) pass = skinnedGeoPass;
			if (!test) { test = speedTreePass->TestObject(geo); if (test == 1) pass = speedTreePass; }
			if (!test && Settings->Forms.AlphaEnabled) { test = alphaPass->TestObject(geo); if (test == 1) pass = alphaPass; }
			if (!test) { test = geometryPass->TestObject(geo); if (test == 1) pass = geometryPass; }
			if (!pass) continue;
			cubeCasters.push_back({ geo, pass, faces });
		}
	}

	// UNOFFICIAL CubeFaceCopySkip: the faces the moving casters reach are copied and drawn; the others are copied, unless they still
	// hold the copy (above). A failed copy draws nothing more; the caller redraws the lamp whole (which forgets the records).
	unsigned char drawFaces = 63;
	if (faceSkip) {
		drawFaces = 0;
		for (const CubeCaster& caster : cubeCasters) drawFaces |= caster.faces;
		const void* liveTex = Shadows->Textures.ShadowCubeMapTexture[LightIndex];
		const void* staticTex = Shadows->Textures.ShadowCubeMapStaticTexture[LightIndex];
		const auto serial = StaticCubeSerials.find(staticTex);
		const unsigned staticSerial = serial != StaticCubeSerials.end() ? serial->second : 0;
		for (int Face = 0; Face < 6; Face++) {
			LiveFaceRecord& record = LiveFaces[LightIndex][Face];
			const bool holdsCopy = record.pure && staticSerial && record.live == liveTex && record.staticTex == staticTex &&
				record.serial == staticSerial && record.generation == Shadows->CubeGeneration;
			const bool draws = (drawFaces >> Face) & 1;
			if (!((viewFaces >> Face) & 1)) {	// UNOFFICIAL CubeFaceViewCull: out of view, left as it is (stale, never read)
				record.pure = false;
				continue;
			}
			if (!draws && holdsCopy && copySkip) {
				statFacesSkipped++;
#if NVR_TEST_TOOLS
				CubeFaceSkipSelfCheck(Device, Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face], Shadows->Textures.ShadowCubeMapStaticSurface[LightIndex][Face]);
#endif
				continue;
			}
			IDirect3DSurface9* from = Shadows->Textures.ShadowCubeMapStaticSurface[LightIndex][Face];
			IDirect3DSurface9* to = Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face];
			record.pure = false;
			if (!from || !to || !staticSerial || FAILED(Device->StretchRect(from, NULL, to, NULL, D3DTEXF_NONE))) { ForgetLiveFaces(LightIndex); return false; }
			if (draws) statFacesDrawn++;
			else {
				statFacesCopied++;
				record.live = liveTex; record.staticTex = staticTex; record.serial = staticSerial; record.generation = Shadows->CubeGeneration;
				record.pure = true;
			}
		}
	}
	else if (Layer == CubeLayer::MovingOnto) statFacesOldPath += 6;
	if (cullFaces) {
		drawFaces &= viewFaces;
		const int kind = Layer == CubeLayer::All ? 0 : 2;
		for (int face = 0; face < 6; face++) if (!((viewFaces >> face) & 1)) statFacesCulled[kind]++;
#if NVR_TEST_TOOLS
		static int poison = 0;
		static unsigned poisonFrame = 0;
		if (poisonFrame != SkinnedBoundFrame && (SkinnedBoundFrame % 30) == 0) poison = TheSettingManager->GetSettingI("Main.Main.Performance", "CubeFaceViewCullCheck");
		poisonFrame = SkinnedBoundFrame;
		if (poison > 0) {	// DIAGNOSTIC CubeFaceViewCullCheck: the left-out faces say "all in shadow"
			for (int face = 0; face < 6; face++) {
				if ((viewFaces >> face) & 1) continue;
				Device->SetRenderTarget(0, Shadows->Textures.ShadowCubeMapSurface[LightIndex][face]);
				Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);
				Device->SetViewport(&ShadowCubeMapViewPort);
				Device->Clear(0L, NULL, D3DCLEAR_TARGET, D3DXCOLOR(0.1f, 0.1f, 0.1f, 0.1f), 1.0f, 0L);	// 0.1: a blocker at a tenth of the reach (0 would read as no data)
			}
		}
#endif
	}

	// UNOFFICIAL RedrawActorsOnly: the moving casters go over the copied static distances; a min blend keeps the nearer.
	if (Layer == CubeLayer::MovingOnto) {
		RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_MIN, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE, RenderStateArgs);
	}

	// UNOFFICIAL LogLamps diagnostic (2026-10-08): in the TOPS casino a ceiling fill light's forward shadow blacked out whole
	// floor pieces. What a lamp's cube map draws close to the lamp decides that, so the first time each lamp's static (or
	// whole) cube map is drawn, the casters within a fifth of its reach of it are logged (CASTERS lines): name, shader type,
	// alpha blending/testing, material alpha, which pass draws it, how far its bound is from the lamp (negative: around it).
	if (Settings->Forward.LogLamps && useGeometryList && Layer != CubeLayer::MovingOnto) {
		static std::unordered_map<const void*, bool> loggedLamps;
		static unsigned casterLines = 0;
		const ShadowSceneLight* lamp = Lights[LightIndex];
		if (casterLines < 600 && !loggedLamps[lamp]) {
			loggedLamps[lamp] = true;
			const float nearLimit = (std::max)(48.0f, 0.2f * pNiLight->Spec.r);
			int nearCount = 0;
			for (const CubeCaster& caster : cubeCasters) {
				const NiBound* b = caster.geometry->m_kWorldBound;
				if (!b) continue;
				const float dx = b->Center.x - LightPos->x, dy = b->Center.y - LightPos->y, dz = b->Center.z - LightPos->z;
				if (sqrtf(dx * dx + dy * dy + dz * dz) - b->Radius < nearLimit) nearCount++;
			}
			casterLines++;
			Logger::Log("CASTERS lamp %p at %.0f %.0f %.0f, reach %.0f, cube radius %.0f, %s: %u casters, %d within %.0f of the lamp:",
				(const void*)lamp, LightPos->x, LightPos->y, LightPos->z, pNiLight->Spec.r, Radius,
				Layer == CubeLayer::StaticOnly ? "static layer" : "whole", (unsigned)cubeCasters.size(), nearCount, nearLimit);
			int listed = 0;
			for (const CubeCaster& caster : cubeCasters) {
				NiGeometry* geo = caster.geometry;
				const NiBound* b = geo->m_kWorldBound;
				if (!b || listed >= 14 || casterLines >= 600) continue;
				const float dx = b->Center.x - LightPos->x, dy = b->Center.y - LightPos->y, dz = b->Center.z - LightPos->z;
				const float gap = sqrtf(dx * dx + dy * dy + dz * dz) - b->Radius;
				if (gap >= nearLimit) continue;
				NiShadeProperty* shade = static_cast<NiShadeProperty*>(geo->GetProperty(NiProperty::kType_Shade));
				NiAlphaProperty* alpha = static_cast<NiAlphaProperty*>(geo->GetProperty(NiProperty::kType_Alpha));
				NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
				const char* passName = caster.pass == skinnedGeoPass ? "skinned" : caster.pass == speedTreePass ? "speedtree" : caster.pass == alphaPass ? "alpha-tested" : "opaque";
				listed++;
				casterLines++;
				Logger::Log("CASTERS   %s %p: shader type %d, alpha %s%s, material alpha %.2f, drawn as %s, bound radius %.0f, %.0f from the lamp%s",
					geo->m_pcName ? geo->m_pcName : "(unnamed)", (const void*)geo, shade ? (int)shade->m_eShaderType : -1,
					alpha && (alpha->flags & NiAlphaProperty::ALPHA_BLEND_MASK) ? "blended" : "not blended",
					alpha && (alpha->flags & NiAlphaProperty::TEST_ENABLE_MASK) ? ", tested" : "", material ? material->fAlpha : -1.0f,
					passName, b->Radius, gap, gap < 0.0f ? " (around the lamp)" : "");
			}
		}
	}

	// UNOFFICIAL (Fable): the form-based fallback selects the same references for every face (its old per-face
	// "in front" test was computed and never used), so they are collected once per light instead of six times.
	fallbackRefsScratch.clear();
	if (!useGeometryList) {
		for (TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First; Entry; Entry = Entry->next) {
			if (NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms)) {
				if (RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius() && !IsRefracting(Entry->item))
					fallbackRefsScratch.push_back(RefNode);
			}
		}
	}

	for (int Face = 0; Face < 6; Face++) {
		if (!((drawFaces >> Face) & 1)) continue;	// UNOFFICIAL CubeFaceCopySkip: nothing reaches it (it holds the static copy)
		if (GpuTimer::Enabled) {	// UNOFFICIAL diagnostic (intshadow), see CubeFaceOutOfView
			const int kind = Layer == CubeLayer::All ? 0 : Layer == CubeLayer::StaticOnly ? 1 : 2;
			statViewFaces[kind]++;
			if (CubeFaceOutOfView(LightPos, Radius, FaceDirection[Face])) statViewFacesOut[kind]++;
		}
		CameraDirection = FaceDirection[Face];
		Up = FaceUp[Face];
		At = Eye + CameraDirection;

		if (useGeometryList) {
			const unsigned char bit = (unsigned char)(1 << Face);
			for (const CubeCaster& caster : cubeCasters)
				if (caster.faces & bit) caster.pass->GeometryList.push(caster.geometry);
		}
		else {
			// old form based geo accumulation when the one perform by the game has not handled this light
			// (references gathered once per light, above the face loop)
			for (NiNode* RefNode : fallbackRefsScratch)
				AccumChildren(RefNode, &Settings->Forms, false, false);
		}



		D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
		Shadows->Constants.ShadowViewProj = View * Proj;

		Device->SetRenderTarget(0, Layer == CubeLayer::StaticOnly ? Shadows->Textures.ShadowCubeMapStaticSurface[LightIndex][Face]
			: Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face]);
		Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

		Device->SetViewport(&ShadowCubeMapViewPort);
		// (MovingOnto keeps the copied distances: only the depth buffer starts empty)
		Device->Clear(0L, NULL, Layer == CubeLayer::MovingOnto ? D3DCLEAR_ZBUFFER : D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

		RenderAccums();
	}

	if (Layer == CubeLayer::MovingOnto) {
		RenderState->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD, RenderStateArgs);
		RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	}
	return true;
}


static void FlagShaderPropertyRecurse(NiAVObject* apObject, UInt32 auiFlags, bool abSet) {
	if (!apObject)
		return;

	if (apObject->IsGeometry()) {
		NiGeometry* pGeometry = static_cast<NiGeometry*>(apObject);
		NiShadeProperty* shaderProperty = static_cast<NiShadeProperty*>(pGeometry->GetProperty(NiProperty::kType_Shade));
		if (shaderProperty) {
			if (abSet)
				shaderProperty->m_usFlags.Set(auiFlags);
			else
				shaderProperty->m_usFlags.Clear(auiFlags);
		}

	}
	else if (apObject->IsNiNode()) {
		NiNode* pNiNode = static_cast<NiNode*>(apObject);
		for (UInt32 i = 0; i < pNiNode->m_children.end; i++) {
			FlagShaderPropertyRecurse(pNiNode->m_children.data[i], auiFlags, abSet);
		}
	}
}

static SInt32 frames = -1;
static void FlagPlayerGeometry() {
	frames++;

	// Run this function every 50 frames, or on launch
	if (frames > 50 || frames == -1) {
		if (Player->firstPersonNiNode)
			FlagShaderPropertyRecurse(Player->firstPersonNiNode, NiShadeProperty::kFirstPerson, true);

		NiNode* node = Player->GetNode();
		if (node)
			FlagShaderPropertyRecurse(node, NiShadeProperty::kThirdPerson, true);

		frames = 0;
	}
}

void ShadowManager::RecalculateBillboardVectors(D3DXVECTOR3* SunDir) {
	D3DXVECTOR3 WorldUp = D3DXVECTOR3(0.0f, 0.0f, 1.0f);

	// Calculate BillboardRight as perpendicular to SunDir and WorldUp
	D3DXVECTOR3 BillboardRightVec;
	D3DXVec3Cross(&BillboardRightVec, &WorldUp, SunDir);

	// Handle case where sun is directly above/below
	if (D3DXVec3LengthSq(&BillboardRightVec) < 0.0001f) {
		BillboardRightVec = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
	}
	D3DXVec3Normalize(&BillboardRightVec, &BillboardRightVec);

	// Calculate BillboardUp perpendicular to both SunDir and BillboardRightVec
	D3DXVECTOR3 BillboardUpVec;
	D3DXVec3Cross(&BillboardUpVec, SunDir, &BillboardRightVec);
	D3DXVec3Normalize(&BillboardUpVec, &BillboardUpVec);

	// Set shader constants
	BillboardRight = NiVector4(BillboardRightVec.x, BillboardRightVec.y, BillboardRightVec.z, 0.0f);
	BillboardUp = NiVector4(BillboardUpVec.x, BillboardUpVec.y, BillboardUpVec.z, 0.0f);
}

/*
* Renders the different shadow maps: Near, Far, Ortho.
*/
// CachedDistantShadows: the characters (and the player) in the loaded cells, as world bound spheres.
void ShadowManager::CollectMovers(const D3DXVECTOR3& SunDir) {
	FrameMovers.clear();
	// How far a shadow reaches sideways per unit of height, for the current sun (capped for a sun near the horizon).
	MoverShadowStretch = min(sqrtf(SunDir.x * SunDir.x + SunDir.y * SunDir.y) / max(SunDir.z, 0.1f), 10.0f);
	auto add = [this](NiNode* node) {
		if (!node || node->m_flags & NiAVObject::APP_CULLED) return;
		NiBound* bound = node->GetWorldBound();
		if (bound) FrameMovers.push_back(D3DXVECTOR4(bound->Center.x, bound->Center.y, bound->Center.z, bound->Radius));
	};
	add(Player->GetNode());
	if (!Player->GetWorldSpace() || !Tes || !Tes->gridCellArray) return;
	GridCellArray* CellArray = Tes->gridCellArray;
	const UInt32 CellArraySize = CellArray->size * CellArray->size;
	for (UInt32 c = 0; c < CellArraySize; c++) {
		TESObjectCELL* Cell = CellArray->GetCell(c);
		if (!Cell || Cell->IsInterior()) continue;
		for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
			TESObjectREFR* Ref = Entry->item;
			if (!Ref || !Ref->baseForm) continue;
			const UInt8 type = Ref->baseForm->formType;
			if (type == TESForm::FormType::kFormType_NPC || type == TESForm::FormType::kFormType_Creature ||
				type == TESForm::FormType::kFormType_LeveledCreature)
				add(Ref->GetNode());
		}
	}
}

// Whether a character's shadow could come from this cascade. The planes are the ones from the cascade's last
// redraw (world space); the cached picture has moved with the camera since, so the test is widened by that distance.
// On screen, everything nearer than the previous cascade's end (InnerDepth, view depth) is shaded by the sharper
// cascades: their selection spheres enclose their slices of the view, and only their outer 10% cross-fades into the
// next one. So a character whose whole shadow stays nearer than 90% of InnerDepth is never sampled from this cascade
// and does not count; that keeps the player and anyone close by from forcing a redraw every frame.
bool ShadowManager::MoversInCascade(int cascade, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, float InnerDepth) {
	if (!ShadowMap->Forms.Actors || FrameMovers.empty()) return false;
	const D3DXVECTOR3 camera = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
	const D3DXVECTOR3 forward(TheRenderManager->CameraForward.x, TheRenderManager->CameraForward.y, TheRenderManager->CameraForward.z);
	const D3DXVECTOR3 moved = camera - CascadeRefreshCamera[cascade];
	const float pad = D3DXVec3Length(&moved) + 64.0f;
	NiFrustumPlanes* planes = &ShadowMap->ShadowMapFrustumPlanes;
	for (const D3DXVECTOR4& mover : FrameMovers) {
		const D3DXVECTOR3 toMover = D3DXVECTOR3(mover.x, mover.y, mover.z) - camera;
		const float shadowReach = mover.w * (1.0f + 2.0f * MoverShadowStretch) + 64.0f;
		if (D3DXVec3Dot(&toMover, &forward) + shadowReach < 0.9f * InnerDepth) continue;
		bool inside = true;
		for (UInt32 p = 0; p < NiFrustumPlanes::MaxPlanes && inside; p++) {
			if (!planes->IsPlaneActive(p)) continue;
			const NiPlane& plane = planes->CullingPlanes[p];
			const float distance = plane.Normal.x * mover.x + plane.Normal.y * mover.y + plane.Normal.z * mover.z - plane.Constant;
			if (distance < -(mover.w + pad)) inside = false;
		}
		if (inside) return true;
	}
	return false;
}

// With the F10 profile and CachedDistantShadows on: every 240 frames, how often each sun cascade was redrawn on its
// schedule and how often because of characters.
void ShadowManager::LogSunShadowStats(bool cachedDistant) {
	if (!GpuTimer::Enabled || !cachedDistant) { SunStats = {}; return; }
	SunStats.Characters += (unsigned)FrameMovers.size();
	if (++SunStats.Frames < 240) return;
	const float perFrame = 100.0f / SunStats.Frames;
	Logger::Log("CACHED DISTANT SHADOWS %u frames: redrawn %% of frames (scheduled + for characters): near %.0f, "
		"middle %.0f + %.0f, far %.0f + %.0f, LOD %.0f + %.0f; characters tracked %.1f",
		SunStats.Frames, SunStats.Scheduled[MapNear] * perFrame,
		SunStats.Scheduled[MapMiddle] * perFrame, SunStats.ForCharacters[MapMiddle] * perFrame,
		SunStats.Scheduled[MapFar] * perFrame, SunStats.ForCharacters[MapFar] * perFrame,
		SunStats.Scheduled[MapLod] * perFrame, SunStats.ForCharacters[MapLod] * perFrame,
		SunStats.Characters / (float)SunStats.Frames);
	SunStats = {};
}

void ShadowManager::RenderShadowMaps() {
	PointShadowForward::Begin();	// UNOFFICIAL: idle unless the cube maps below are published
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // cancel out if rendering effects is disabled
	// UNOFFICIAL SkinnedShadowFaceTest: skeleton bounds are rebuilt once per frame; forget meshes not seen for a while.
	if ((++SkinnedBoundFrame & 1023) == 0) {
		SkinnedBounds.clear();
		// UNOFFICIAL RedrawActorsOnly: forget geometry not seen for a while (keeping the rest's motion history).
		const size_t motionsBefore = CasterMotions.size();
		for (auto it = CasterMotions.begin(); it != CasterMotions.end();)
			it = SkinnedBoundFrame - it->second.seen > 1024 ? CasterMotions.erase(it) : std::next(it);
		if (CasterMotions.size() != motionsBefore) CasterMotionsGeneration++;	// CasterListCache: the lamps' kept entries may be gone
		for (auto it = LampCasterLists.begin(); it != LampCasterLists.end();)	// and lamps not seen for a while
			it = SkinnedBoundFrame - it->second.seen > 1024 ? LampCasterLists.erase(it) : std::next(it);
	}

	// track point lights for interiors and exteriors
	ShadowSceneLight* ShadowLights[ShadowCubeMapsMax] = { NULL };
	NiPointLight* Lights[TrackedLightsMax] = { NULL };
	NiSpotLight* SpotLights[SpotLightsMax] = { NULL };

	TheShaderManager->GetNearbyLights(ShadowLights, Lights, SpotLights);

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::ExteriorsStruct* ShadowsExteriors = &Shadows->Settings.Exteriors;
	ShadowsExteriorEffect::InteriorsStruct* ShadowsInteriors = &Shadows->Settings.Interiors;

	bool isExterior = TheShaderManager->GameState.isExterior;// || currentCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior; // exterior flag currently broken
	bool ExteriorEnabled = isExterior && TheShaderManager->Effects.ShadowsExteriors->Enabled && ShadowsExteriors->Enabled;
	bool InteriorEnabled = !isExterior && TheShaderManager->Effects.ShadowsInteriors->Enabled;

	// early out in case shadow rendering is not required
	if (!ExteriorEnabled && !InteriorEnabled && !TheShaderManager->orthoRequired && !TheShaderManager->orthoSkyRequired || !ShadowShadersLoaded) {
		return;
	}
	if (!Player->parentCell) return;

	auto timer = TimeLogger();
	static CpuTimer shadowMapsCpuTimer("Shadow maps (CPU)");
	CpuProfileScope shadowMapsCpu(shadowMapsCpuTimer);
	Shadows->Constants.CharacterData.w = 0.0f;	// UNOFFICIAL 2026-10-23 CharacterShadowMap: read only once this frame's draw is in it (RenderCharacterShadowMap)

	// prepare some pointers to the device and surfaces
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	static GpuTimer sunCascadesTimer("Sun cascade geometry");
	static GpuTimer atlasResolveTimer("Shadow atlas resolve");
	static GpuTimer atlasFilterTimer("Shadow atlas prefilter");
	static GpuTimer orthoMapTimer("Ortho shadow map");
	static GpuTimer pointMapsTimer("Point shadow cubemaps");
	static GpuTimer flashlightMapsTimer("Flashlight shadow maps");
	IDirect3DSurface9* DepthSurface = NULL;
	IDirect3DSurface9* RenderSurface = NULL;
	D3DVIEWPORT9 viewport;

	D3DXVECTOR4* ShadowData = &TheShaderManager->Effects.ShadowsExteriors->Constants.Data;
	D3DXVECTOR4* OrthoData = &TheShaderManager->Effects.ShadowsExteriors->Constants.OrthoData;
	Device->GetDepthStencilSurface(&DepthSurface);
	Device->GetRenderTarget(0, &RenderSurface);
	Device->GetViewport(&viewport);	

	DWORD zfunc;
	Device->GetRenderState(D3DRS_ZFUNC, &zfunc); // backup in case of inverted depth

	DWORD oldStencilEnable, oldStencilRef, oldStencilFunc;
	DWORD oldAlphaRef, oldNormalizeNormals, oldPointSize;

	Device->GetRenderState(D3DRS_STENCILENABLE, &oldStencilEnable);
	Device->GetRenderState(D3DRS_STENCILREF, &oldStencilRef);
	Device->GetRenderState(D3DRS_STENCILFUNC, &oldStencilFunc);
	Device->GetRenderState(D3DRS_ALPHAREF, &oldAlphaRef);
	Device->GetRenderState(D3DRS_NORMALIZENORMALS, &oldNormalizeNormals);
	Device->GetRenderState(D3DRS_POINTSIZE, &oldPointSize);

	if (1.0 - NiDX9Renderer::GetSingleton()->m_fZClear) // inverted depth
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL, RenderStateArgs);
	else
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_STENCILENABLE, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_NORMALIZENORMALS, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_POINTSIZE, 810365505, RenderStateArgs);

	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();
	
	D3DXVECTOR4 PlayerPosition = Player->pos.toD3DXVEC4();
	TESObjectCELL* currentCell = Player->parentCell;

	// Flag player geometry so we can control if it should be rendered in shadow cubemaps
	FlagPlayerGeometry();

	// Render directional shadows for Sun/Moon
	NiNode* PlayerNode = Player->GetNode();
	D3DXVECTOR3 At;
	At.x = PlayerNode->m_worldTransform.pos.x - TheRenderManager->CameraPosition.x;
	At.y = PlayerNode->m_worldTransform.pos.y - TheRenderManager->CameraPosition.y;
	At.z = PlayerNode->m_worldTransform.pos.z - TheRenderManager->CameraPosition.z;

	// Render all shadow maps
	Device->BeginScene();

	// Quantize sun direction angle to reduce shimmer by a large factor.
	D3DXVECTOR3 SunDir = Shadows->CalculateSmoothedSunDir();

	if (isExterior && (ExteriorEnabled || TheShaderManager->orthoRequired || TheShaderManager->orthoSkyRequired)) {

		// Update cascade depths based on current camera.
		Shadows->GetCascadeDepths();

		// Opaque passes (TESR_ShadowData.y always 0) use the no-discard copy, so the GPU keeps early-Z.
		geometryPass->VertexShader = ShadowMapVertex;
		geometryPass->PixelShader = ShadowMapOpaquePixel;
		alphaPass->VertexShader = ShadowMapVertex;
		alphaPass->PixelShader = ShadowMapPixel;
		skinnedGeoPass->VertexShader = ShadowMapVertex;
		skinnedGeoPass->PixelShader = ShadowMapOpaquePixel;
		speedTreePass->VertexShader = ShadowMapVertex;
		speedTreePass->PixelShader = ShadowMapPixel;
		terrainLODPass->VertexShader = ShadowMapVertex;
		terrainLODPass->PixelShader = ShadowMapOpaquePixel;

		// UNOFFICIAL contact hardening. The sun's disc is about 0.53 degrees wide, so a shadow edge blurs by about
		// 0.0093 world units per unit between the thing casting it and the ground; SunSize scales that.
		if (ContactHardeningCompiled) {
			// (The settings are read by ShadowsExteriorEffect::UpdateSettings when they change, not by name every frame.)
			// UNOFFICIAL 2026-10-16 (night cost study, work/bench/foundations/nightcost): at full night (dayLight 0) the
			// shadows are the moon's, faded and dim, and their soft edges did not show: off there, the night test spot
			// changed fewer pixels than two identical frames differ by, and the world scene took 0.8-1.0 ms less (GTX 1070,
			// 1440p). AtNight keeps it.
			const bool night = TheShaderManager->GameState.dayLight <= 0.0f && !Shadows->Settings.ContactHardening.AtNight;
			const bool on = Shadows->Settings.ContactHardening.Enabled && !night;
			ContactHardeningData.x = on ? 0.00925f * Shadows->Settings.ContactHardening.SunSize : 0.0f;
			ContactHardeningData.y = Shadows->Settings.ContactHardening.MaxSoftness;
			ContactHardeningData.z = Shadows->Settings.ContactHardening.Smooth ? 1.0f : 0.0f;	// UNOFFICIAL 2026-10-15 Smooth
			static float logged = -1.0f;
			if (ContactHardeningData.x != logged) {
				Logger::Log("UNOFFICIAL contact hardening %s%s (penumbra %.4f per unit of distance, max %.0f texels)",
					on ? "ON" : "OFF", night && Shadows->Settings.ContactHardening.Enabled ? " for the night (AtNight off)" : "",
					ContactHardeningData.x, ContactHardeningData.y);
				logged = ContactHardeningData.x;
			}
		}
		else {
			static bool hinted = false;
			if (!hinted && Shadows->Settings.ContactHardening.Enabled) {
				Logger::Log("UNOFFICIAL contact hardening was turned on after startup: restart the game once to apply it.");
				hinted = true;
			}
		}
		// UNOFFICIAL 2026-10-16 SkipWhenFaded (dawn cost study, work/bench/foundations/dawncost): while the sun's shadows are
		// fully faded and nothing reads the cascade maps (ShadowsExteriorEffect::FadedCascadesUnread), they are not drawn.
		// Only once they hold a full draw since the atlas was made (SunAtlasDrawn), and the first frame the cascades are drawn
		// after a skip redraws every one, so no reader ever sees a map that missed frames. While the sun is below the horizon
		// (the whole fade-1 window in the Mojave's climate) nothing redraws them anyway.
		const bool skipCascades = ExteriorEnabled && Shadows->SunFaded && SunAtlasDrawn && Shadows->FadedCascadesUnread();
		if (skipCascades) {
			SunCascadesSkipped = true;
			if (SunDir.z > 0.0f) ShadowData->z = 0;	// as the cascade pass leaves it
		}
		else if (SunCascadesSkipped) {
			ForceAllCascades = true;
			SunCascadesSkipped = false;
		}
#if NVR_TEST_TOOLS
		// DIAGNOSTIC SkipWhenFadedCheck: while the cascades are skipped the atlas is set to 0 (depth 0 / zero moments: everything
		// in shadow), so a reader the checks missed changes the picture. The real atlas is kept in a copy and put back when the
		// skip ends (the sun is below the horizon in the fade-1 window: no pass would redraw it). The render target and depth are
		// put back at the end of this function.
		{
			static IDirect3DTexture9* kept = nullptr;
			static IDirect3DSurface9* keptSurface = nullptr;
			static bool poisoned = false;
			const bool poison = skipCascades && Shadows->Settings.Exteriors.SkipWhenFadedCheck && Shadows->ShadowAtlasSurface;
			if (poison && !poisoned) {
				D3DSURFACE_DESC desc = {}, keptDesc = {};
				Shadows->ShadowAtlasSurface->GetDesc(&desc);
				if (keptSurface && (FAILED(keptSurface->GetDesc(&keptDesc)) || keptDesc.Width != desc.Width || keptDesc.Height != desc.Height ||
					keptDesc.Format != desc.Format)) {
					keptSurface->Release(); kept->Release(); keptSurface = nullptr; kept = nullptr;
				}
				if (!kept && SUCCEEDED(Device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &kept, NULL)))
					kept->GetSurfaceLevel(0, &keptSurface);
				poisoned = keptSurface && SUCCEEDED(Device->StretchRect(Shadows->ShadowAtlasSurface, NULL, keptSurface, NULL, D3DTEXF_NONE));
				Logger::Log("SkipWhenFadedCheck: shadow atlas %s", poisoned ? "kept and poisoned while skipped" : "could not be kept: not poisoned");
			}
			if (poison && poisoned) {
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurface);
				Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
				Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
			}
			else if (!poison && poisoned) {
				const bool back = SUCCEEDED(Device->StretchRect(keptSurface, NULL, Shadows->ShadowAtlasSurface, NULL, D3DTEXF_NONE));
				Logger::Log("SkipWhenFadedCheck: shadow atlas %s", back ? "put back" : "could NOT be put back");
				poisoned = false;
			}
		}
#endif
		static int loggedSkip = -1;
		const int skipState = skipCascades ? 2 : Shadows->SunFaded && ExteriorEnabled ? 1 : 0;
		if (skipState != loggedSkip) {
			Logger::Log("SUN FADE STATE: sun cascade maps %s", skipState == 2 ? "SKIPPED" : skipState == 1 ?
				"drawn (read this frame: the volumetric fog's sun glow, the volumetric light or the deferred lookup; or no full draw yet)" : "drawn");
			loggedSkip = skipState;
		}
		SunCascadesDrawnMask = 0;	// set again below on the frames the sun cascades are drawn
		if (ExteriorEnabled && SunDir.z > 0.0f && !skipCascades) {
			// Recalculate billboard vectors for speedtree leaves shader.
			RecalculateBillboardVectors(&SunDir);

			ShadowData->z = 0; // set shader constant to identify other shadow maps
			auto shadowMapTimer = TimeLogger();

			// UNOFFICIAL 2026-10-21 (sunfix): one log line whenever the sun reaches or leaves straight overhead (GetCascadeViewProj).
			{
				static int loggedOverhead = -1;
				const int overheadState = !ShadowsExteriorEffect::SunOverhead(SunDir) ? 0 : Shadows->Settings.Exteriors.OverheadSunFix ? 2 : 1;
				if (overheadState != loggedOverhead) {
					Logger::Log("SUN OVERHEAD STATE: sun %.4f %.4f %.4f %s", SunDir.x, SunDir.y, SunDir.z, overheadState == 2 ?
						"straight overhead: cascades built with +Y as up (OverheadSunFix)" : overheadState == 1 ?
						"straight overhead: cascades flat and empty, no sun shadows from them (OverheadSunFix off)" : "not overhead: cascades as always");
					loggedOverhead = overheadState;
				}
			}

			if (Shadows->ShadowAtlasSurfaceMSAA)
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurfaceMSAA);
			else
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurface);

			Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);

			unsigned updatedCascades = 0;
			{
			static CpuTimer sunCascadesCpuTimer("Sun cascades (CPU)");
			CpuProfileScope cpu(sunCascadesCpuTimer);
			GpuProfileScope gpu(sunCascadesTimer, Device);
#if NVR_TEST_TOOLS
			SunShadowStats::Mode = Shadows->Settings.Exteriors.CascadeStats;	// UNOFFICIAL 2026-10-21 test tools
			if (SunShadowStats::Mode) SunShadowStats::BeginFrame();
			const unsigned long long statsLoopStart = SunShadowStats::Mode ? SunShadowStats::Now() : 0;
#endif
			// CachedDistantShadows (ReducedQuality): the staggered schedule for the middle, far and LOD cascades, but a
			// cascade with a character in it is redrawn every frame, and once more after the last one left, so moving
			// characters never keep a stale shadow (the flicker the plain stagger caused). Other moving objects (doors,
			// physics clutter) are treated as still and can lag up to 3 or 7 frames in those cascades.
			const bool staggered = TheSettingManager->SettingsMain.Main.StaggeredSunShadows;
			const bool cachedDistant = TheSettingManager->SettingsMain.Main.CachedDistantShadows && !staggered;
			if (cachedDistant) CollectMovers(SunDir);
			// UNOFFICIAL 2026-10-21 (sunshadow): SunCascadeOneWalk schedules every cascade first (the schedule of one does not depend
			// on another's draw), walks once for all that redraw, then draws them in the same order. Without it each cascade is
			// scheduled, walked and drawn in turn, as before. SunCascadeSkipFlat: a flat cascade is cleared, not walked or drawn.
			const bool oneWalk = Shadows->Settings.Exteriors.SunOneWalk;
			const bool skipFlat = Shadows->Settings.Exteriors.SunSkipFlat;
			const bool leanDraws = Shadows->Settings.Exteriors.SunLeanDraws;	// RenderPass::Lean around the new path's draws
#if NVR_TEST_TOOLS
			const int verify = Shadows->Settings.Exteriors.SunVerify;
#endif
			// UNOFFICIAL 2026-10-27 displacement shadows (DisplaceShadows.h): from here to EndCascades the cell land near the camera is
			// sorted into the displaced land's pass.
			DisplaceShadows::BeginCascades();
			D3DXMATRIX cascadeViewProj[4];
			bool cascadeCached[4] = {};
			float cascadeInner[4] = {};
			unsigned flatCascades = 0;
			auto drawCascade = [&](int i) {
				static const char* const cascadeNames[] = { "Sun cascade 0 (near)", "Sun cascade 1 (middle)", "Sun cascade 2 (far)", "Sun cascade 3 (lod)" };
				GpuDebug::LabelScope label(i >= 0 && i < 4 ? cascadeNames[i] : "Sun cascade");	// UNOFFICIAL 2026-10-26 GpuDebugLabels
				ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[i];
				const bool flat = (flatCascades >> i) & 1;
				Shadows->Constants.ShadowViewProj = cascadeViewProj[i];
				SunLandDropScale = min(1.0f, 2.0f * max(SunDir.z, 0.0f));	// SunDir is a unit vector: z = sin(elevation)
#if NVR_TEST_TOOLS
				if (SunShadowStats::Mode) SunShadowStats::Sum[i].frames++;
				SunShadowStats::Current = SunShadowStats::Mode >= 2 ? &SunShadowStats::Sum[i] : nullptr;	// 2: the full split
				if (SunShadowStats::Current) {	// 3: the column check (ShadowMapCascadeCenterRadius is camera-relative)
					const D3DXVECTOR4& cr = ShadowMap->ShadowMapCascadeCenterRadius;
					SunShadowStats::CurrentColumn = { D3DXVECTOR3(cr.x + TheRenderManager->CameraPosition.x, cr.y + TheRenderManager->CameraPosition.y,
						cr.z + TheRenderManager->CameraPosition.z), SunDir, cr.w, &ShadowMap->ShadowMapFrustumPlanes };
					SunShadowStats::Current->radius = cr.w;
					static unsigned columnLogs = 0;
					if (SunShadowStats::Mode >= 3 && columnLogs < 16) {
						columnLogs++;
						NiCamera* camera = WorldSceneGraph->camera;
						Logger::Log("SUN CASCADE COLUMN %d: centre (camera-relative) %.0f %.0f %.0f radius %.1f | slice %.6f..%.6f of the clip range | camera near %.2f far %.0f fov %.1f"
							" | sun %.3f %.3f %.3f | camera at %.0f %.0f %.0f", i, cr.x, cr.y, cr.z, cr.w, ShadowMap->ShadowMapNear, ShadowMap->ShadowMapRadius,
							camera->Frustum.Near, camera->Frustum.Far, WorldSceneGraph->cameraFOV, SunDir.x, SunDir.y, SunDir.z,
							TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
					}
				}
				// SunCascadeVerify 2: first the old way (its own walk, every caster drawn), its map kept for the comparison below.
				std::vector<UInt8> oldMap;
				bool oldRead = false;
				if (verify >= 2 && (oneWalk || flat || leanDraws)) {
					std::unordered_set<NiGeometry*> sharedLand;
					if (oneWalk) sharedLand.swap(RenderPass::LandSet);
					RenderShadowMap(ShadowMap, &cascadeViewProj[i]);
					if (oneWalk) RenderPass::LandSet.swap(sharedLand);
					oldRead = ReadCascadeRegion(Shadows, ShadowMap->ShadowMapViewPort, oldMap);
				}
#endif
				BeginShadowMap(ShadowMap, &cascadeViewProj[i], oneWalk);
#if NVR_TEST_TOOLS
				// SunCascadeVerify 1: the old walk of this cascade into the passes' lists, taken out again in draw order and compared
				// with this cascade's share of the one walk (and the land the drop applies to). Flat cascades: the map check proves them.
				if (verify >= 1 && oneWalk && !flat) {
					std::unordered_set<NiGeometry*> sharedLand;
					sharedLand.swap(RenderPass::LandSet);
					WalkShadowMap(ShadowMap);
					std::vector<std::pair<NiGeometry*, UInt8>> oldOrder, newOrder;
					for (UInt8 pass = kSunOpaque; pass <= kSunDisplaceLand; pass++) {
						std::stack<NiGeometry*>& list = SunPassOf(pass)->GeometryList;
						while (!list.empty()) { oldOrder.emplace_back(list.top(), pass); list.pop(); }
						for (auto c = SunCasters[i].rbegin(); c != SunCasters[i].rend(); ++c) if (c->pass == pass) newOrder.emplace_back(c->geometry, pass);
					}
					bool same = oldOrder == newOrder;
					unsigned landDiff = 0;
					for (const auto& caster : newOrder)
						if (caster.second == kSunOpaque && (RenderPass::LandSet.count(caster.first) != 0) != (sharedLand.count(caster.first) != 0)) landDiff++;
					RenderPass::LandSet.swap(sharedLand);
					if (same && !landDiff) SunVerify.listSame++;
					else {
						SunVerify.listDiff++;
						if (SunVerify.listDiff <= 20) {
							size_t first = 0;
							while (first < oldOrder.size() && first < newOrder.size() && oldOrder[first] == newOrder[first]) first++;
							Logger::Log("SUN CASCADE VERIFY: cascade %d lists DIFFER: old walk %u casters, one walk %u, first difference at %u; land drop differs on %u",
								i, (unsigned)oldOrder.size(), (unsigned)newOrder.size(), (unsigned)first, landDiff);
						}
					}
					SunVerify.casters += newOrder.size();
				}
#endif
				if (oneWalk) PushSunCasters(i);
				else if (!flat) WalkShadowMap(ShadowMap);
				RenderPass::Lean = leanDraws;
				DrawShadowMap(ShadowMap, oneWalk);
				RenderPass::Lean = false;
#if NVR_TEST_TOOLS
				if (verify >= 2 && (oneWalk || flat || leanDraws)) {
					std::vector<UInt8> newMap;
					if (!oldRead || !ReadCascadeRegion(Shadows, ShadowMap->ShadowMapViewPort, newMap)) SunVerify.mapFailed++;
					else if (oldMap == newMap) SunVerify.mapSame++;
					else {
						SunVerify.mapDiff++;
						if (SunVerify.mapDiff <= 20) {
							size_t differ = 0;
							for (size_t b = 0; b < oldMap.size() && b < newMap.size(); b++) differ += oldMap[b] != newMap[b];
							Logger::Log("SUN CASCADE VERIFY: cascade %d MAPS DIFFER%s: %u of %u bytes", i, flat ? " (flat)" : "", (unsigned)differ, (unsigned)oldMap.size());
						}
					}
				}
				if (verify) { SunVerify.cascades++; if (flat) SunVerify.flat++; }
				SunShadowStats::Current = nullptr;
#endif
				SunLandDropScale = 0.0f;
				CascadeRefreshCamera[i] = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
				CascadeHadMover[i] = cascadeCached[i] && MoversInCascade(i, ShadowMap, cascadeInner[i]);
			};
			for (int i = MapNear; i < MapOrtho; i++) {
				ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[i];
				const bool cachedCascade = cachedDistant && i != MapNear;
				const unsigned updatePeriod = SunCascadeUpdatePeriod(i, Shadows->Settings.ShadowMaps.LimitFrequency,
					TheSettingManager->SettingsMain.Main.NearCascadeInterval, staggered || cachedCascade);

				bool update = ForceAllCascades || SunCascadeUpdatesOnFrame(i, FrameCounter, updatePeriod);
				const float innerDepth = i > MapNear ? ((const float*)Shadows->Constants.ShadowMapRadius)[i - 1] : 0.0f;
				if (update) SunStats.Scheduled[i]++;
				else if (cachedCascade && (CascadeHadMover[i] || MoversInCascade(i, ShadowMap, innerDepth))) {
					update = true;
					SunStats.ForCharacters[i]++;
				}
				if (update) {
					updatedCascades |= 1u << i;
					cascadeViewProj[i] = Shadows->GetCascadeViewProj(ShadowMap, &SunDir);
					cascadeCached[i] = cachedCascade;
					cascadeInner[i] = innerDepth;
					if (skipFlat && FlatCascade(cascadeViewProj[i])) flatCascades |= 1u << i;
					if (!oneWalk) drawCascade(i);
				}
				else {
					// Keep cached cascades locked to camera translation between geometry refreshes.
					D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
					D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
					D3DXMATRIX translationMatrix;
					D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
					ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
					ShadowMap->CameraTranslation = newCameraTranslation;
					// A cached NEAR cascade (NearCascadeInterval 2) also moves its selection sphere with the
					// camera: the centre is camera-relative, and the near sphere is small enough for a stale
					// centre to matter. The rarely refreshed cascades keep their original behaviour.
					if (i == MapNear) {
						ShadowMap->ShadowMapCascadeCenterRadius.x -= difference.x;
						ShadowMap->ShadowMapCascadeCenterRadius.y -= difference.y;
						ShadowMap->ShadowMapCascadeCenterRadius.z -= difference.z;
					}

				}

				std::string message = "ShadowManager::RenderShadowMap ";
				message += std::to_string(i);
				shadowMapTimer.LogTime(message.c_str());
			}
			SunCascadesDrawnMask = updatedCascades;	// UNOFFICIAL 2026-10-28 grass shadows past the near cascade (GrassShadows.h)
			if (oneWalk && updatedCascades) {
				// The one walk, for the cascades that redraw and are not flat; then each draws its share, in the usual order.
				for (int c = 0; c < 4; c++) { SunForms[c] = &Shadows->ShadowMaps[c].Forms; SunPlanes[c] = &Shadows->ShadowMaps[c].ShadowMapFrustumPlanes; }
				SunLandDropScale = min(1.0f, 2.0f * max(SunDir.z, 0.0f));
				RenderPass::LandDrop = ShadowMapLandDrop();	// (AccumObject notes land only while a drop is wanted)
				RenderPass::LandSet.clear();
#if NVR_TEST_TOOLS
				const unsigned long long walkStart = SunShadowStats::Mode >= 2 ? SunShadowStats::Now() : 0;
#endif
				WalkSunCascades(updatedCascades & ~flatCascades);
#if NVR_TEST_TOOLS
				if (SunShadowStats::Mode >= 2) SunShadowStats::tSharedWalk += SunShadowStats::Now() - walkStart;
#endif
				RenderPass::LandDrop = 0.0f;
				SunLandDropScale = 0.0f;
				for (int i = MapNear; i < MapOrtho; i++) if (updatedCascades & (1u << i)) drawCascade(i);
				RenderPass::LandSet.clear();
			}
#if NVR_TEST_TOOLS
			if (verify && ++SunVerify.frames >= 240) {
				Logger::Log("SUN CASCADE VERIFY %u frames, %u cascades drawn (%u flat): lists %u identical, %u differ (%.0f casters each on average); "
					"maps %u identical, %u differ, %u not read", SunVerify.frames, SunVerify.cascades, SunVerify.flat, SunVerify.listSame, SunVerify.listDiff,
					SunVerify.listSame + SunVerify.listDiff ? (double)SunVerify.casters / (SunVerify.listSame + SunVerify.listDiff) : 0.0,
					SunVerify.mapSame, SunVerify.mapDiff, SunVerify.mapFailed);
				SunVerify = {};
			}
			else if (!verify) SunVerify = {};
#endif
			if (ForceAllCascades) SunAtlasDrawn = true;	// UNOFFICIAL SkipWhenFaded: every cascade was drawn this frame
			ForceAllCascades = false;
			DisplaceShadows::EndCascades();	// UNOFFICIAL 2026-10-27
			LogSunShadowStats(cachedDistant);
#if NVR_TEST_TOOLS
			if (SunShadowStats::Mode) SunShadowStats::EndFrame(SunShadowStats::Now() - statsLoopStart);
#endif
			}

			// Resolve MSAA.
			if (Shadows->ShadowAtlasSurfaceMSAA) {
				GpuProfileScope gpu(atlasResolveTimer, Device);
				bool partialResolveFailed = false;
				for (int i = MapNear; i < MapOrtho; ++i) {
					if (!(updatedCascades & (1u << i))) continue;
					const D3DVIEWPORT9& viewport = Shadows->ShadowMaps[i].ShadowMapViewPort;
					RECT region = {(LONG)viewport.X, (LONG)viewport.Y,
						(LONG)(viewport.X + viewport.Width), (LONG)(viewport.Y + viewport.Height)};
					if (FAILED(Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, &region,
						Shadows->ShadowAtlasSurface, &region, D3DTEXF_NONE))) {
						partialResolveFailed = true;
						break;
					}
				}
				if (partialResolveFailed) {
					Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, NULL,
						Shadows->ShadowAtlasSurface, NULL, D3DTEXF_NONE);
					updatedCascades = (1u << MapOrtho) - 1;
				}
			}

			if (Shadows->Settings.ShadowMaps.Prefilter) {
				GpuProfileScope gpu(atlasFilterTimer, Device);
				BlurShadowAtlas(updatedCascades);
			}

			if (Shadows->Settings.ShadowMaps.Mipmaps)
				Shadows->ShadowAtlasTexture->GenerateMipSubLevels();

			// UNOFFICIAL 2026-10-23 CharacterShadowMap: the player's own map, from the same (quantised) sun, after the cascades.
			RenderCharacterShadowMap(SunDir);
		}
#if NVR_TEST_TOOLS
		// DIAGNOSTIC SkipWhenFadedCheck 4: the atlas set to 0 at any hour, nothing skipped (shows that the poison of 1 and 2
		// reaches the shaders that read the maps: by day everything in the cascades' reach turns to shadow).
		if (Shadows->Settings.Exteriors.SkipWhenFadedCheck == 4 && Shadows->ShadowAtlasSurface) {
			Device->SetRenderTarget(0, Shadows->ShadowAtlasSurface);
			Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
			Device->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
			ForceAllCascades = true;	// redrawn in full once the check is off
		}
#endif

		// render ortho map if one of the effects using ortho is active
		if (TheShaderManager->orthoRequired || TheShaderManager->orthoSkyRequired) {
			GpuProfileScope gpu(orthoMapTimer, Device);
			auto shadowMapTimer = TimeLogger();

			ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[MapOrtho];

			// UNOFFICIAL 2026-10-17 Skylighting v2 (evidence skylight2-LOG.md): when ONLY the sky visibility needs the map (no rain or
			// snow effect asked for it this frame), it is redrawn only when the camera moved MoveRefresh units since the last draw,
			// every MapRefresh frames (cells that streamed in), on a settings change or after a pause in its use; in between it
			// moves with the camera as below. The statics, terrain and trees it holds do not change from frame to frame.
			bool draw = !Shadows->Settings.OrthoMap.LimitFrequency || !((FrameCounter + 2) % 4);
			static D3DXVECTOR3 skyLastDrawPos(0.0f, 0.0f, 0.0f);
			static int skyFramesSinceDraw = 1 << 20;
			static int skySettingsSeen = -1;
			static bool skyLastDrawSkyOnly = false;	// the map in the texture was drawn camera-centred (below)
			const bool skyOnly = TheShaderManager->orthoSkyRequired && !TheShaderManager->orthoRequired;
			const D3DXVECTOR3 cameraNow = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
			if (skyOnly) {
				const D3DXVECTOR3 moved = cameraNow - skyLastDrawPos;
				const float moveRefresh = Shadows->Settings.OrthoMap.SkyMoveRefresh;
				draw = skyFramesSinceDraw >= Shadows->Settings.OrthoMap.SkyMapRefresh || TheShaderManager->orthoSkyFresh || !skyLastDrawSkyOnly ||
					D3DXVec3LengthSq(&moved) > moveRefresh * moveRefresh || skySettingsSeen != Shadows->Settings.OrthoMap.SkySettingsGeneration;
			}
			if (draw) {
				Device->SetRenderTarget(0, Shadows->ShadowMapOrthoSurface);
				Device->SetDepthStencilSurface(Shadows->ShadowMapOrthoDepthSurface);

				ShadowData->z = 1; // identify ortho map in shader constant
				D3DXVECTOR3 OrthoDir = D3DXVECTOR3(0.05f, 0.05f, 1.0f);
				// UNOFFICIAL Skylighting v2 (sky only): the map is centred on the CAMERA (a view slice from -2/3 to +2/3 of the ortho
				// distance: its bounding sphere is centred on the camera, ~3000 units round it at the default 3000), not on the view
				// ahead, so turning the camera never needs a redraw; only moving does (and then the cached map is translated).
				const float orthoNearWas = ShadowMap->ShadowMapNear, orthoRadiusWas = ShadowMap->ShadowMapRadius;
				if (skyOnly && Shadows->Settings.OrthoMap.SkyOwnProjection) {
					// UNOFFICIAL 2026-10-17 Skylighting v2 (OwnProjection, launch 2 diagnosis): an explicit top-down projection in camera-
					// relative world space (the space of the casters' world matrices, RenderManager::CreateD3DMatrix): straight down from
					// Distance above the camera, +y up in the map, +-Distance round the camera, depth 0..2 Distance, the world origin
					// snapped to a texel (as GetCascadeViewProj does) so a redraw does not shift the texels.
					NiCamera* sceneCamera = WorldSceneGraph->camera;
					const float R = Shadows->Settings.OrthoMap.Distance, H = Shadows->Settings.OrthoMap.Distance;
					D3DXVECTOR3 eye(0.0f, 0.0f, H), at(0.0f, 0.0f, 0.0f), up(0.0f, 1.0f, 0.0f);
					D3DXMATRIX view, proj;
					D3DXMatrixLookAtRH(&view, &eye, &at, &up);
					D3DXMatrixOrthoOffCenterRH(&proj, -R, R, -R, R, 0.0f, 2.0f * H);
					D3DXMATRIX viewProj = view * proj;
					const float mapSize = ShadowMap->ShadowMapResolution;
					D3DXVECTOR4 origin(-sceneCamera->m_worldTransform.pos.x, -sceneCamera->m_worldTransform.pos.y, -sceneCamera->m_worldTransform.pos.z, 1.0f);
					D3DXVec4Transform(&origin, &origin, &viewProj);
					const float ox = origin.x * mapSize * 0.5f, oy = origin.y * mapSize * 0.5f;
					proj._41 += (floorf(ox + 0.5f) - ox) * 2.0f / mapSize;
					proj._42 += (floorf(oy + 0.5f) - oy) * 2.0f / mapSize;
					viewProj = view * proj;
					NiFrustum frustum(-R, R, R, -R, 0.0f, 2.0f * H, true);
					TheCameraManager->SetFrustumPlanes(&ShadowMap->ShadowMapFrustumPlanes, &viewProj, eye, frustum);
					ShadowMap->ShadowMapFrustumPlanes.SetActivePlaneState(62);
					ShadowMap->ShadowMapCascadeCenterRadius = D3DXVECTOR4(0.0f, 0.0f, 0.0f, R);
					ShadowMap->Forms.MinRadius = ShadowMap->Forms.OrigMinRadius * R * ShadowMap->ShadowMapInverseResolution;
					ShadowMap->CameraTranslation = sceneCamera->m_worldTransform.pos.toD3DXVEC3();
					Shadows->Constants.ShadowViewProj = viewProj;
				}
				else {
					if (skyOnly) { ShadowMap->ShadowMapRadius = orthoRadiusWas * (2.0f / 3.0f); ShadowMap->ShadowMapNear = -ShadowMap->ShadowMapRadius; }
					Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &OrthoDir);
				}
				ShadowMap->ShadowMapNear = orthoNearWas;
				ShadowMap->ShadowMapRadius = orthoRadiusWas;

#if NVR_TEST_TOOLS
				// UNOFFICIAL 2026-10-17 OrthoProbe (test tools): a new ProbeDump value = this draw also writes each texel's caster (OrthoProbe.h).
				const int probeValue = Shadows->Settings.OrthoMap.ProbeDump;
				const bool probe = probeValue != 0 && probeValue != OrthoProbe::LastValue && OrthoProbe::Prepare(Device, (UINT)Shadows->Settings.OrthoMap.Resolution);
				DWORD probeWrite1 = 0;
				if (probe) {
					OrthoProbe::Owners.clear(); OrthoProbe::Drawn.clear(); OrthoProbe::Recording = true;
					for (int& n : OrthoProbe::LandCount) n = 0;
					Device->GetRenderState(D3DRS_COLORWRITEENABLE1, &probeWrite1);
					Device->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
					Device->SetRenderTarget(1, OrthoProbe::IdSurface);
				}
#endif
				// UNOFFICIAL Skylighting v2 CleanMap: alpha-tested casters with their test (leaves, fences), no blended-only geometry.
				// fixes-b #4: only the sky's own map (skyOnly): a map the rain or snow effect also reads stays as it was before v2 (solid casters), or the rain would fall through leaves and fences
				const bool clean = skyOnly && Shadows->Settings.OrthoMap.SkyCleanMap;
				const bool alphaWas = ShadowMap->Forms.AlphaEnabled;
				if (clean) { ShadowMap->Forms.AlphaEnabled = true; OrthoSkipBlendOnly = true; }
				RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj);
				ShadowMap->Forms.AlphaEnabled = alphaWas;
				OrthoSkipBlendOnly = false;
				skyLastDrawPos = cameraNow;
				skyFramesSinceDraw = 0;
				skyLastDrawSkyOnly = skyOnly;
				skySettingsSeen = Shadows->Settings.OrthoMap.SkySettingsGeneration;
#if NVR_TEST_TOOLS
				if (probe) {
					OrthoProbe::Recording = false;
					Device->SetRenderTarget(1, nullptr);
					Device->SetRenderState(D3DRS_COLORWRITEENABLE1, probeWrite1);
					OrthoProbe::Dump(Device, Shadows->ShadowMapOrthoSurface, ShadowMap->ShadowCameraToLight, probeValue);
					OrthoProbe::LastValue = probeValue;
					OrthoProbe::Owners.clear(); OrthoProbe::Drawn.clear();
				}
#endif
			}
			else {
				D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
				D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
				D3DXMATRIX translationMatrix;
				D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
				ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
				ShadowMap->CameraTranslation = newCameraTranslation;
				++skyFramesSinceDraw;
			}
			// UNOFFICIAL 2026-10-27 Persistent wetness and snow (CoverMap.h): the map the rain and snow effects read is this frame's (drawn
			// with solid casters, or moved with the camera), so the cover map may take the top surfaces' heights from it.
			if (!skyOnly) CoverMapOrthoFrame = TheFrameRateManager->FrameNumber;

			OrthoData->x = Shadows->Settings.OrthoMap.Distance * 2;
			OrthoData->y = ShadowMap->ShadowMapInverseResolution;
	
			shadowMapTimer.LogTime("ShadowManager::RenderShadowMap Ortho");
		}

		// UNOFFICIAL 2026-10-14: the light grid's map of the world from the sun (AmbientLight GridBounce), plain depth as the
		// ortho map, every few frames.
		AmbientLightEffect* ambientLight = TheShaderManager->Effects.AmbientLight;
		if (ambientLight && ambientLight->WantsGridSunMap(SunDir)) {
			static GpuTimer gridSunTimer("Light grid sun map");
			GpuProfileScope gpu(gridSunTimer, Device);
			RecalculateBillboardVectors(&SunDir);
			Device->SetRenderTarget(0, ambientLight->gridSunSurface);
			Device->SetDepthStencilSurface(ambientLight->gridSunDepth);
			ShadowData->z = 1; // plain depth, as the ortho map
			Shadows->Constants.ShadowViewProj = ambientLight->GridSunViewProj(SunDir);
			// With the colour shader: every pass draws depth + the caster's colour (its diffuse texture) into two targets.
			const bool colour = ShadowGridColorPixel && ambientLight->gridSunColorSurface;
			RenderPass* passes[] = { geometryPass, alphaPass, skinnedGeoPass, speedTreePass, terrainLODPass };
			ShaderRecordPixel* saved[5] = {};
			if (colour) {
				for (int i = 0; i < 5; i++) { saved[i] = passes[i]->PixelShader; passes[i]->PixelShader = ShadowGridColorPixel; }
				Device->SetRenderTarget(1, ambientLight->gridSunColorSurface);
				RenderPass::BindDiffuse = true;
			}
			RenderShadowMap(&ambientLight->gridSunMap, &Shadows->Constants.ShadowViewProj);
			if (colour) {
				for (int i = 0; i < 5; i++) passes[i]->PixelShader = saved[i];
				Device->SetRenderTarget(1, nullptr);
				RenderPass::BindDiffuse = false;
				ShadowData->w = 0.0f;
			}
			ambientLight->gridSunHasColor = colour;
		}

		// UNOFFICIAL 2026-10-19 [ShadowsExteriors.TerrainShadows] (after Conor's port of Community Shaders' Terrain Shadows): the land's
		// heights loaded and swept toward the sun on worker threads (effects/TerrainShadows.cpp); the SunFar pass applies them.
		Shadows->TerrainShadows.Update(Device, D3DXVECTOR3(TheShaderManager->ShaderConst.SunDir));   // the real sun, not the quantized one
	}

	// Render shadow maps for point lights
	bool usePointLights = (TheShaderManager->GameState.isDayTime > 0.5) ? ShadowsExteriors->UsePointShadowsDay : ShadowsExteriors->UsePointShadowsNight;

	AlphaEnabled = ShadowsInteriors->Forms.AlphaEnabled;
	geometryPass->VertexShader = ShadowCubeMapVertex;
	geometryPass->PixelShader = ShadowCubeMapOpaquePixel;
	alphaPass->VertexShader = ShadowCubeMapVertex;
	alphaPass->PixelShader = ShadowCubeMapPixel;
	skinnedGeoPass->VertexShader = ShadowCubeMapVertex;
	skinnedGeoPass->PixelShader = ShadowCubeMapOpaquePixel;
	speedTreePass->VertexShader = ShadowCubeMapVertex;
	speedTreePass->PixelShader = ShadowCubeMapPixel;

	auto shadowMapTimer = TimeLogger();
	if ((isExterior && usePointLights) || (!isExterior && InteriorEnabled)) {
		GpuProfileScope gpu(pointMapsTimer, Device);
		// Each slot's cubemap stores length(light - point) / radius, which does not depend on the camera,
		// so with PointShadowInterval > 1 a slot can keep its contents between redraws (see
		// PointShadowSchedule.h for when it must be redrawn at once).
		static PointShadowSlotState slots[ShadowCubeMapsMax];
		static unsigned scheduleFrame = 0, statFrames = 0, statPresent = 0, statRedrawn = 0, statStaticReused = 0;
		static unsigned statReasons[(int)PointShadowRedraw::Count] = {};
		const unsigned interval = (unsigned)TheSettingManager->SettingsMain.Main.PointShadowInterval;
		// UNOFFICIAL RedrawActorsOnly (PointShadowSchedule.h PointShadowDrawPlan): what each slot's static cube map holds.
		static PointShadowSlotState staticSlots[ShadowCubeMapsMax];
		static unsigned statStaticDrawn = 0, statMovingOnly = 0, statWhole = 0;
		static unsigned statMovedLamps = 0, statMovedWithPeople = 0, statAnchored = 0;	// UNOFFICIAL 2026-10-18 diagnostic (intshadow)
		static float statMovedStep = 0.0f, statMovedStepMax = 0.0f, statMovedWander = 0.0f, statMovedRadius = 0.0f;
		static unsigned statMovedResized = 0;
		static float statMovedRadiusStep = 0.0f;
		static const void* movedLight[ShadowCubeMapsMax] = {};
		static float movedHome[ShadowCubeMapsMax][3] = {};
		const bool split = ShadowsInteriors->RedrawActorsOnly && ActorsOnlySupported(Device);

		// render the cubemaps for each light; the last slot's cubemap is never sampled (ShadowCubeMapsSampled), so it is not drawn
		// (UNOFFICIAL: with forward shadows indoors every slot up to LightPoints is drawn, PointShadowForward::ShadowedSlots)
		const int shadowedSlots = PointShadowForward::ShadowedSlots(isExterior);
		for (int i = 0; i < shadowedSlots; i++) {
			ShadowSceneLight* shadowLight = ShadowLights[i];
			if (!shadowLight || !Shadows->EnsureCubeMap(i)) { slots[i].valid = false; continue; } // no light (or no cube map) at this index

			PointShadowSlotState now;
			NiPointLight* pointLight = shadowLight->sourceLight;
			now.light = shadowLight;
			now.texture = Shadows->Textures.ShadowCubeMapTexture[i];
			now.cell = currentCell;
			now.x = pointLight->m_worldTransform.pos.x;
			now.y = pointLight->m_worldTransform.pos.y;
			now.z = pointLight->m_worldTransform.pos.z;
#if defined(OBLIVION)
			now.radius = pointLight->CanCarry ? 256.0f : pointLight->Spec.r * ShadowsInteriors->LightRadiusMult; // as in RenderShadowCubeMap
#else
			now.radius = pointLight->Spec.r * ShadowsInteriors->LightRadiusMult; // as in RenderShadowCubeMap (no 256 for CanCarry lamps, see there)
#endif
			PointShadowCasterState(shadowLight, now.casterHash, now.staticCasters);
			now.valid = true;
			// UNOFFICIAL 2026-10-18 LampShadowMoveTolerance ([Main.Main.ReducedQuality], CHANGES THE IMAGE, off at 0): a lamp that has
			// moved less than this from where its cube map was drawn (a flickering light's jitter) keeps that point: its cube map is
			// kept or refreshed from there, and the forward lookups use it (PointShadowForward::Publish takes the drawn position).
			// Indoors with the forward shadows only (the screen-space pass looks up from the light's own position).
			NiPoint3 anchor = { now.x, now.y, now.z };
			bool anchored = false;
			{
				const float tolerance = TheSettingManager->SettingsMain.Main.LampShadowMoveTolerance;
				if (tolerance > 0.0f && !isExterior && PointShadowForward::UsesAllSlots(isExterior) && ForwardWasActive &&
					slots[i].valid && slots[i].light == shadowLight && slots[i].cell == now.cell && slots[i].radius == now.radius) {
					const float dx = now.x - slots[i].x, dy = now.y - slots[i].y, dz = now.z - slots[i].z;
					if ((dx != 0.0f || dy != 0.0f || dz != 0.0f) && dx * dx + dy * dy + dz * dz <= tolerance * tolerance) {
						now.x = anchor.x = slots[i].x; now.y = anchor.y = slots[i].y; now.z = anchor.z = slots[i].z;
						anchored = true;
						if (GpuTimer::Enabled) statAnchored++;
					}
				}
			}
			// UNOFFICIAL 2026-10-20 InvisibleShadowFromLamp ([Shaders.ShadowsInteriors.Forward], a LOOK OPTION, 0 off): an invisible light
			// (no lamp part within FixtureDistance) with a lamp part within that distance has its cube map drawn from that lamp part, over
			// its reach plus the move, without that lamp: it still lights the room from where the designers put it, but its shadows fall
			// from a lamp the player can see. The forward lookups read the cube from where it was drawn (Publish takes now's position).
			float radiusOverride = 0.0f;
			int skipModel = -1;
			bool relocated = false;
			{
				const ShadowsExteriorEffect::InteriorsStruct::ForwardStruct& forward = ShadowsInteriors->Forward;
				if (forward.InvisibleShadowFromLamp > 0.0f && forward.Enabled && !isExterior && PointShadowForward::UsesAllSlots(isExterior) &&
					LampFixtures::Invisible(pointLight, forward.FixtureDistance)) {
					if (const LampFixtures::Glow* lamp = LampFixtures::NearestLampPart(pointLight, forward.InvisibleShadowFromLamp)) {
						const float dx = lamp->x - pointLight->m_worldTransform.pos.x, dy = lamp->y - pointLight->m_worldTransform.pos.y,
							dz = lamp->z - pointLight->m_worldTransform.pos.z;
						now.x = anchor.x = lamp->x; now.y = anchor.y = lamp->y; now.z = anchor.z = lamp->z;
						now.radius += sqrtf(dx * dx + dy * dy + dz * dz);
						radiusOverride = now.radius;
						skipModel = lamp->model;
						relocated = true;
						static std::unordered_map<const void*, int> loggedMove;
						if (ShadowsInteriors->Forward.LogLamps && loggedMove.size() < 256 && !loggedMove.count(shadowLight)) {
							loggedMove[shadowLight] = 1;
							Logger::Log("LAMP %p (invisible, at %.0f %.0f %.0f) casts its shadows from the lamp part '%s' of '%s' at %.0f %.0f %.0f (%.0f units away), cube radius %.0f",
								(const void*)shadowLight, pointLight->m_worldTransform.pos.x, pointLight->m_worldTransform.pos.y, pointLight->m_worldTransform.pos.z,
								lamp->name ? lamp->name : "", lamp->root ? lamp->root : "", lamp->x, lamp->y, lamp->z, sqrtf(dx * dx + dy * dy + dz * dz), now.radius);
						}
					}
				}
			}
			if (GpuTimer::Enabled) statPresent++;
			const PointShadowRedraw why = PointShadowRedrawReason(slots[i], now, scheduleFrame, i, interval);
			if (why == PointShadowRedraw::None) {
				if (GpuTimer::Enabled && now.staticCasters) statStaticReused++;
				continue;
			}
			// UNOFFICIAL 2026-10-18 diagnostic (intshadow): lamps redrawn because they moved -- how far, and whether they wander or
			// jitter round one place (a flickering light's movement) -- with the F10 profile on.
			if (GpuTimer::Enabled && why == PointShadowRedraw::Moved) {
				const float dx = now.x - slots[i].x, dy = now.y - slots[i].y, dz = now.z - slots[i].z;
				const float step = sqrtf(dx * dx + dy * dy + dz * dz);
				statMovedLamps++; statMovedStep += step; statMovedStepMax = (std::max)(statMovedStepMax, step); statMovedRadius += now.radius;
				if (now.radius != slots[i].radius) {
					statMovedResized++;
					statMovedRadiusStep = (std::max)(statMovedRadiusStep, fabsf(now.radius - slots[i].radius) / (std::max)(now.radius, 1.0f));
				}
				if (!now.staticCasters && shadowLight->kGeometryList.start) statMovedWithPeople++;
				if (movedLight[i] != shadowLight) { movedLight[i] = shadowLight; movedHome[i][0] = now.x; movedHome[i][1] = now.y; movedHome[i][2] = now.z; }
				const float hx = now.x - movedHome[i][0], hy = now.y - movedHome[i][1], hz = now.z - movedHome[i][2];
				statMovedWander = (std::max)(statMovedWander, sqrtf(hx * hx + hy * hy + hz * hz));
				static std::unordered_map<const void*, bool> named;
				static unsigned namedLines = 0;
				if (namedLines < 16 && !named[shadowLight]) {
					named[shadowLight] = true; namedLines++;
					NiNode* parent = pointLight->m_parent;
					Logger::Log("POINT SHADOWS MOVING LAMP %p '%s' (parent '%s') at %.1f %.1f %.1f, cube radius %.0f, step %.3f units, %s casters",
						(const void*)shadowLight, pointLight->m_pcName ? pointLight->m_pcName : "", parent && parent->m_pcName ? parent->m_pcName : "",
						now.x, now.y, now.z, now.radius, step, now.staticCasters ? "only still" : shadowLight->kGeometryList.start ? "moving" : "no listed");
				}
			}

			// UNOFFICIAL RedrawActorsOnly: with people in reach, the walls and furniture come from the lamp's static cube map
			// (drawn when missing or stale) and only the people are drawn again.
			const bool hasList = shadowLight->kGeometryList.start != nullptr;
			const bool moving = hasList && !now.staticCasters;
			PointShadowSlotState staticNow = now;
			staticNow.staticCasters = true;
			staticNow.texture = nullptr;
			if (split && hasList) {
				AdoptStaticCubeMap(Shadows, staticSlots, i, shadowLight);
				if (moving) Shadows->EnsureStaticCubeMap(i);
				staticNow.texture = Shadows->Textures.ShadowCubeMapStaticTexture[i];
			}
			const PointShadowDraw draw = PointShadowDrawPlan(split && hasList, staticSlots[i], staticNow, moving);
			CubeEyeOverride = anchored || relocated ? &anchor : nullptr;	// UNOFFICIAL LampShadowMoveTolerance: draw from the kept point
			CubeRadiusOverride = radiusOverride;	// UNOFFICIAL InvisibleShadowFromLamp (0: the light's own)
			CubeRelocated = relocated;
			CubeSkipModel = skipModel;
			// UNOFFICIAL CubeFaceViewCull: only for a lamp redrawn every frame anyway (above RenderShadowCubeMap)
			// (Any reader of the cube maps indoors -- the forward lookups or the screen-space lamp mask -- reads them for pixels in view.)
			CubeCullAllowed = TheSettingManager->SettingsMain.Main.CubeFaceViewCull && interval <= 1 && !isExterior &&
				(moving || why == PointShadowRedraw::Moved);
			CubeCulledAny = false;
			struct EyeReset { ~EyeReset() { CubeEyeOverride = nullptr; CubeCullAllowed = false; CubeRadiusOverride = 0.0f; CubeRelocated = false; CubeSkipModel = -1; } } eyeReset;
			// UNOFFICIAL 2026-10-18 diagnostic (intshadow): GPU time per lamp of each kind of draw (avg per lamp; the counts per frame
			// are in the POINT SHADOWS lines), F10 profile only.
			static GpuTimer staticLayerTimer("  Cube static layer (1 lamp)", 480), movingOntoTimer("  Cube people onto copy (1 lamp)", 960),
				wholeTimer("  Cube whole lamp (1 lamp)", 480);
			if (draw == PointShadowDraw::StaticThenMoving) {
				GpuProfileScope gpuLamp(staticLayerTimer, Device);
				RenderShadowCubeMap(ShadowLights, i, CubeLayer::StaticOnly);
				staticSlots[i] = staticNow;
				if (GpuTimer::Enabled) statStaticDrawn++;
			}
			bool drawn = false;
			if (draw != PointShadowDraw::All) {
				GpuProfileScope gpuLamp(movingOntoTimer, Device);
				drawn = RenderShadowCubeMap(ShadowLights, i, CubeLayer::MovingOnto);
				if (drawn && GpuTimer::Enabled) statMovingOnly++;
				if (!drawn) {
					ActorsOnlyBroken = true;
					Logger::Log("UNOFFICIAL RedrawActorsOnly: copying a static cube map failed; lamps are redrawn whole from now on, as before");
				}
			}
			// Render targets set in function due to rendering multiple faces.
			if (!drawn) {
				GpuProfileScope gpuLamp(wholeTimer, Device);
				RenderShadowCubeMap(ShadowLights, i);
				if (GpuTimer::Enabled) statWhole++;
			}
			now.partial = CubeCulledAny;	// UNOFFICIAL CubeFaceViewCull: redrawn next frame whatever happens
			slots[i] = now;
			if (GpuTimer::Enabled) { statRedrawn++; statReasons[(int)why]++; }

			std::string message = "ShadowManager::RenderShadowCubeMap ";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
		scheduleFrame++;
		// UNOFFICIAL 2026-10-29 ShadowPool (core/ShadowPool.h): static cube maps for the next lamps of the ranking, drawn a few a frame.
		static PointShadowForward::PoolEntry poolEntries[ShadowPoolMax];
		int poolCount = 0;
		{
			const int want = ShadowPool::Wanted();
			const int spare = ShadowCubeMapsMax - 1;	// its static entries hold each pool cube while it is drawn
			static bool spareWarned = false;
			if (!want || isExterior || !PointShadowForward::UsesAllSlots(isExterior)) {
				if (!want && !ShadowPool::Cubes.empty()) ShadowPool::Release();
			}
			else if (shadowedSlots >= spare) {
				if (!spareWarned) { spareWarned = true; Logger::Log("[WARNING] UNOFFICIAL ShadowPool: needs [Shaders.ShadowsInteriors.Main] LightPoints below %d (it draws through the last slot): off", spare); }
			}
			else if (Shadows->CubeMapSizeCreated && Shadows->EnsureStaticCubeMap(spare)) {
				ShadowPool::Frame++;
				if (ShadowPool::MadeSize != Shadows->CubeMapSizeCreated) { ShadowPool::Release(); ShadowPool::MadeSize = Shadows->CubeMapSizeCreated; }
				LARGE_INTEGER frequency, counter;
				QueryPerformanceFrequency(&frequency);
				QueryPerformanceCounter(&counter);
				const double nowSeconds = (double)counter.QuadPart / (double)frequency.QuadPart;
				int budget = ShadowPool::Budget();
				static GpuTimer poolTimer("  Cube pool static layer (per frame)", 480);
				GpuProfileScope gpuPool(poolTimer, Device);
				for (ShadowSceneLight* light : ShadowPool::Candidates) {
					if (poolCount >= want) break;
					NiPointLight* source = light ? light->sourceLight : nullptr;
					if (!source) continue;
					const float radius = source->Spec.r * ShadowsInteriors->LightRadiusMult;	// as the slot loop
					const NiPoint3& at = source->m_worldTransform.pos;
					const int index = ShadowPool::EntryFor(light, Shadows->CubeMapSizeCreated, want);
					if (index < 0) continue;
					ShadowPool::Cube& cube = ShadowPool::Cubes[index];
					const bool current = cube.valid && cube.x == at.x && cube.y == at.y && cube.z == at.z && cube.radius == radius && cube.cell == currentCell;
					if (!current) {
						if (budget <= 0) { if (GpuTimer::Enabled) ShadowPool::StatWaiting++; continue; }	// drawn on a later frame; no shadow until then
						budget--;
						IDirect3DCubeTexture9*& tex = Shadows->Textures.ShadowCubeMapStaticTexture[spare];
						IDirect3DSurface9** faces = Shadows->Textures.ShadowCubeMapStaticSurface[spare];
						std::swap(tex, cube.texture);
						for (int f = 0; f < 6; f++) std::swap(faces[f], cube.faces[f]);
						ShadowSceneLight* one[ShadowCubeMapsMax] = {};
						one[spare] = light;
						const bool drawn = RenderShadowCubeMap(one, spare, CubeLayer::StaticOnly);
						std::swap(tex, cube.texture);
						for (int f = 0; f < 6; f++) std::swap(faces[f], cube.faces[f]);
						if (!drawn) continue;
						cube.x = at.x; cube.y = at.y; cube.z = at.z; cube.radius = radius; cube.cell = currentCell; cube.valid = true;
						if (GpuTimer::Enabled) ShadowPool::StatDrawn++;
					}
					if (cube.since <= 0.0) cube.since = nowSeconds;
					cube.used = ShadowPool::Frame;
					poolEntries[poolCount++] = { light, D3DXVECTOR3(cube.x, cube.y, cube.z), cube.radius, cube.texture, ShadowCubeMapsMax + index, cube.since };
				}
				if (GpuTimer::Enabled) {
					ShadowPool::StatPublished += poolCount;
					if (++ShadowPool::StatFrames >= 240) {
						const float f = 1.0f / ShadowPool::StatFrames;
						Logger::Log("SHADOW POOL per frame: %.1f lamps past the live slots, %.1f with their static cube map, %.2f drawn, %.2f waiting for a draw; %u cube maps (%u x %u)",
							(float)ShadowPool::Candidates.size(), ShadowPool::StatPublished * f, ShadowPool::StatDrawn * f, ShadowPool::StatWaiting * f,
							(unsigned)ShadowPool::Cubes.size(), ShadowPool::MadeSize, ShadowPool::MadeSize);
						ShadowPool::StatFrames = ShadowPool::StatPublished = ShadowPool::StatDrawn = ShadowPool::StatWaiting = 0;
					}
				}
			}
		}
		// UNOFFICIAL interior forward point-light shadows: the lights whose cube map is theirs this frame.
		if (!isExterior) {
			PointShadowForward::Publish(slots, ShadowLights, shadowedSlots, Shadows->Textures.ShadowCubeMapTexture);
			PointShadowForward::PublishPool(poolEntries, poolCount);	// UNOFFICIAL 2026-10-29 ShadowPool (nothing when the pool is off)
			ForwardWasActive = PointShadowForward::Active;
			PointShadowForward::FrameStats();
		}

		if (GpuTimer::Enabled && ++statFrames >= 240) { // with the F10 profile: how much work the cubemaps really are
			const float perFrame = 1.0f / statFrames;
			Logger::Log("POINT SHADOWS interval %u: %.1f lights present, %.1f cubemaps redrawn, %.1f static cubemaps reused per frame (%u frames); "
				"redrawn because: scheduled %.2f, casters %.2f, new %.2f, other light %.2f, moved %.2f, resized %.2f, cell %.2f, texture %.2f",
				interval, statPresent * perFrame, statRedrawn * perFrame, statStaticReused * perFrame, statFrames,
				statReasons[(int)PointShadowRedraw::Scheduled] * perFrame, statReasons[(int)PointShadowRedraw::CastersChanged] * perFrame,
				statReasons[(int)PointShadowRedraw::NewSlot] * perFrame,
				statReasons[(int)PointShadowRedraw::OtherLight] * perFrame, statReasons[(int)PointShadowRedraw::Moved] * perFrame,
				statReasons[(int)PointShadowRedraw::Resized] * perFrame, statReasons[(int)PointShadowRedraw::OtherCell] * perFrame,
				statReasons[(int)PointShadowRedraw::OtherTexture] * perFrame);
			Logger::Log("POINT SHADOWS skinned casters (SkinnedShadowFaceTest %s): %.1f per frame drawn into %.1f faces (all faces would be %.1f), %.1f skipped beyond the lamp's reach, %.1f without a usable skeleton bound",
				TheSettingManager->SettingsMain.Main.SkinnedShadowFaceTest ? "on" : "off", statSkinnedCasters * perFrame, statSkinnedFaces * perFrame,
				statSkinnedCasters * perFrame * 6.0f, statSkinnedOutOfRange * perFrame, statSkinnedNoBound * perFrame);
			unsigned rigidMoving = 0;
			for (const auto& entry : CasterMotions)
				if (entry.second.seen == SkinnedBoundFrame && entry.second.changed && SkinnedBoundFrame - entry.second.changed < MovingSettleFrames) rigidMoving++;
			Logger::Log("POINT SHADOWS RedrawActorsOnly %s: per frame %.1f lamps redrawn whole, %.1f only their people (a copy of the walls and furniture, plus %.1f static layers drawn); %u rigid pieces moving now (held weapons, doors...)",
				split ? "on" : (ShadowsInteriors->RedrawActorsOnly ? "unavailable" : "off"), statWhole * perFrame, statMovingOnly * perFrame, statStaticDrawn * perFrame, rigidMoving);
			// UNOFFICIAL 2026-10-18 (intshadow): CubeFaceCopySkip's faces, and the lamps that moved.
			Logger::Log("POINT SHADOWS CubeFaceCopySkip %s: per frame %.1f faces left as they were (still the static copy), %.1f copied only, %.1f copied and drawn on, %.1f by the old path (copy all six, draw all six)",
				TheSettingManager->SettingsMain.Main.CubeFaceCopySkip ? "on" : "off", statFacesSkipped * perFrame, statFacesCopied * perFrame, statFacesDrawn * perFrame, statFacesOldPath * perFrame);
			Logger::Log("POINT SHADOWS moved lamps: %.2f per frame (%.2f with moving casters), step per frame mean %.3f max %.3f units, furthest from where first seen %.2f units, cube radius mean %.0f",
				statMovedLamps * perFrame, statMovedWithPeople * perFrame, statMovedLamps ? statMovedStep / statMovedLamps : 0.0f, statMovedStepMax, statMovedWander,
				statMovedLamps ? statMovedRadius / statMovedLamps : 0.0f);
			Logger::Log("POINT SHADOWS LampShadowMoveTolerance %.1f: %.2f lamps per frame kept their drawn point; view test (diagnostic): per frame faces drawn whole %.1f / static layer %.1f / people %.1f, of them wholly outside the view %.1f / %.1f / %.1f",
				TheSettingManager->SettingsMain.Main.LampShadowMoveTolerance, statAnchored * perFrame, statViewFaces[0] * perFrame, statViewFaces[1] * perFrame, statViewFaces[2] * perFrame,
				statViewFacesOut[0] * perFrame, statViewFacesOut[1] * perFrame, statViewFacesOut[2] * perFrame);
			Logger::Log("POINT SHADOWS CubeFaceViewCull %s: per frame faces left out (out of view) %.1f of whole lamps, %.1f of people refreshes; partial redraws %.2f; moved lamps whose radius changed too %.2f per frame (largest change %.1f%%)",
				TheSettingManager->SettingsMain.Main.CubeFaceViewCull ? "on" : "off", statFacesCulled[0] * perFrame, statFacesCulled[2] * perFrame,
				statReasons[(int)PointShadowRedraw::Partial] * perFrame, statMovedResized * perFrame, 100.0f * statMovedRadiusStep);
			statAnchored = 0; statMovedResized = 0; statMovedRadiusStep = 0.0f;
			for (int k = 0; k < 3; k++) statViewFaces[k] = statViewFacesOut[k] = statFacesCulled[k] = 0;
			statFacesSkipped = statFacesCopied = statFacesDrawn = statFacesOldPath = 0;
			statMovedLamps = statMovedWithPeople = 0; statMovedStep = statMovedStepMax = statMovedWander = statMovedRadius = 0.0f;
			statSkinnedCasters = statSkinnedFaces = statSkinnedOutOfRange = statSkinnedNoBound = 0;
			statFrames = statPresent = statRedrawn = statStaticReused = 0;
			statStaticDrawn = statMovingOnly = statWhole = 0;
			for (unsigned& r : statReasons) r = 0;
		}
	}

	if (TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive && TheShaderManager->Effects.Flashlight->Settings.renderShadows) {
		GpuProfileScope gpu(flashlightMapsTimer, Device);
		// render shadow maps for spotlights
		
		for (int i = 0; i < SpotLightsMax; i++) {
			if (!SpotLights[i] || SpotLights[i]->Spec.r == 0) continue; //bypass lights with no radius

			// Render targets set in function.
			RenderShadowSpotlight(SpotLights, i);

			std::string message = "ShadowManager::RenderShadowSpotLight";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
	}

	// reset renderer to previous state
	Device->SetDepthStencilSurface(DepthSurface);
	Device->SetRenderTarget(0, RenderSurface);
	Device->SetViewport(&viewport);
	Device->SetRenderState(D3DRS_ZFUNC, zfunc);
	Device->SetRenderState(D3DRS_STENCILENABLE, oldStencilEnable);
	Device->SetRenderState(D3DRS_STENCILREF, oldStencilRef);
	Device->SetRenderState(D3DRS_STENCILFUNC, oldStencilFunc);
	Device->SetRenderState(D3DRS_ALPHAREF, oldAlphaRef);
	Device->SetRenderState(D3DRS_NORMALIZENORMALS, oldNormalizeNormals);
	Device->SetRenderState(D3DRS_POINTSIZE, oldPointSize);

	//release smart pointers to prevent memory leak
	if (DepthSurface) DepthSurface->Release();
	if (RenderSurface) RenderSurface->Release();

	if (TheSettingManager->SettingsMain.Develop.DebugMode && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Console)) {
		if (Global->OnKeyDown(0x17)) { // TODO: setting for debug key ?
			char Filename[MAX_PATH];

			time_t CurrentTime = time(NULL);
			GetCurrentDirectoryA(MAX_PATH, Filename);
			strcat(Filename, "\\Test");
			if (GetFileAttributesA(Filename) == INVALID_FILE_ATTRIBUTES) CreateDirectoryA(Filename, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmapatlas.jpg", D3DXIFF_JPG, Shadows->ShadowAtlasSurface, NULL, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmaportho.jpg", D3DXIFF_JPG, Shadows->ShadowMapOrthoSurface, NULL, NULL);
			if (Shadows->CharacterMapSurface) D3DXSaveSurfaceToFileA(".\\Test\\charshadowmap.jpg", D3DXIFF_JPG, Shadows->CharacterMapSurface, NULL, NULL);	// UNOFFICIAL 2026-10-24 CharacterShadowMap

			InterfaceManager->ShowMessage("Textures taken!");
		}
	}

	Device->EndScene();

	FrameCounter = (FrameCounter + 1) % 8;
	shadowMapsRenderTime = timer.LogTime("ShadowManager::RenderShadowMaps");
}


// UNOFFICIAL 2026-10-23 CharacterShadowMap (docs/next/CHARACTER-SHADOW-MAP-DESIGN.md). A small sun shadow map (R32F, plain depth
// whatever the cascades' Mode: TESR_ShadowData.z = 1 makes ShadowMap.pso write depth, as for the ortho map) fitted round the player
// (ShadowsExteriorEffect::GetCharacterViewProj), drawn every frame the cascades are, with the cascade passes' shaders and states
// (BeginShadowMap / DrawShadowMap) but its own walk: the player's node alone (and, with CharacterShadowMapOthers, the other actors of
// the loaded cells whose bound touches the box), not the loaded cells. The lookups (Includes/Shadow.hlsl CharacterMapShadow,
// SunShadows.fx.hlsl) take the darker of the cascades' shadow and this map's where a pixel is inside the box, fading to the cascades
// alone over the box's outer band; so the cascades keep drawing the player as before (the player's shadow on the ground, and the
// shadows of walls and trees on the player, stay theirs), and this map only adds the fine self-shadow the cascades' texels lose.
// Off, or not compiled in, or in first person: nothing here, CharacterData.w stays 0, every shader skips the lookup.
void ShadowManager::RenderCharacterShadowMap(const D3DXVECTOR3& SunDir) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	if (!Shadows->Settings.ShadowMaps.CharacterMap || !Shadows->CharacterMapCompiled) return;
	if (!Shadows->EnsureCharacterMap()) return;
	D3DXMATRIX viewProj;
	if (!Shadows->GetCharacterViewProj(SunDir, viewProj)) return;

	IDirect3DDevice9* Device = TheRenderManager->device;
	static GpuTimer characterMapTimer("Character shadow map");
	static CpuTimer characterMapCpuTimer("Character shadow map (CPU)");
	CpuProfileScope cpu(characterMapCpuTimer);
	GpuProfileScope gpu(characterMapTimer, Device);

	ShadowsExteriorEffect::ShadowMapSettings* map = &Shadows->CharacterMap;
	D3DXVECTOR4* ShadowData = &Shadows->Constants.Data;
	const float zWas = ShadowData->z;
	ShadowData->z = 1.0f;	// plain depth (as the ortho map), whatever the cascades' Mode
	Shadows->Constants.ShadowViewProj = viewProj;
	Device->SetRenderTarget(0, Shadows->CharacterMapSurface);
	Device->SetDepthStencilSurface(Shadows->CharacterMapDepthSurface);

	BeginShadowMap(map, &viewProj);
	// The walk: the player's node (the body, hair, headgear, the held weapon: all under it), culled against the box's planes.
	NiNode* playerNode = Player->GetNode();
	AccumChildren(playerNode, &map->Forms, false, false, &map->ShadowMapFrustumPlanes);
	unsigned others = 0;
	if (Shadows->Settings.ShadowMaps.CharacterMapOthers && Player->GetWorldSpace() && Tes && Tes->gridCellArray) {
		GridCellArray* CellArray = Tes->gridCellArray;
		const UInt32 CellArraySize = CellArray->size * CellArray->size;
		for (UInt32 c = 0; c < CellArraySize; c++) {
			TESObjectCELL* Cell = CellArray->GetCell(c);
			if (!Cell || Cell->IsInterior()) continue;
			for (TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First; Entry; Entry = Entry->next) {
				TESObjectREFR* Ref = Entry->item;
				if (!Ref || !Ref->baseForm || Ref == (TESObjectREFR*)Player) continue;
				const UInt8 type = Ref->baseForm->formType;
				if (type != TESForm::FormType::kFormType_NPC && type != TESForm::FormType::kFormType_Creature && type != TESForm::FormType::kFormType_LeveledCreature) continue;
				NiNode* node = Ref->GetNode();
				if (!node || node == playerNode || (node->m_flags & NiAVObject::APP_CULLED) || !node->WithinFrustum(&map->ShadowMapFrustumPlanes)) continue;
				AccumChildren(node, &map->Forms, false, false, &map->ShadowMapFrustumPlanes);
				others++;
			}
		}
	}
	const unsigned casters = (unsigned)(geometryPass->GeometryList.size() + alphaPass->GeometryList.size() + skinnedGeoPass->GeometryList.size());
	DrawShadowMap(map);
	ShadowData->z = zWas;
	Shadows->Constants.CharacterData.w = 1.0f + (float)Shadows->Settings.ShadowMaps.CharacterMapDebug;	// the map holds this frame's draw: the lookups read it (2-4: a debug view, Shadow.hlsl)

	if (Shadows->Settings.ShadowMaps.CharacterMapLog) {
		static DWORD lastLog = 0;
		if (GetTickCount() - lastLog > 1000) {
			lastLog = GetTickCount();
			const float radius = Shadows->Constants.CharacterCenter.w;
			Logger::Log("CharacterShadowMap: fit %d, half-extent %.0f units (texel %.3f units = %.2f cm), %u casters (%u other actors), sun %.3f %.3f %.3f, bias %.5f, band %.3f",
				Shadows->Settings.ShadowMaps.CharacterMapFit, radius, 2.0f * radius / map->ShadowMapResolution, 2.0f * radius / map->ShadowMapResolution * 1.428f,
				casters, others, SunDir.x, SunDir.y, SunDir.z, Shadows->Constants.CharacterData.z, Shadows->Constants.CharacterData.y);
		}
	}
}

/*
 * Clear a part of the shadow map atlas based on the shadow mapping mode.
 * 
 * Note: Render target, view port should be set beforehand. Scene has to be already being rendered.
 */
void ShadowManager::ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* TargetShadowMap = Shadows->ShadowAtlasSurface;

	Device->SetDepthStencilSurface(NULL);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetPixelShader(ShadowMapClearPixel->ShaderHandle, false);

	Device->SetPixelShaderConstantF(0, (const float*) ClearColor, 1);

	// Draw a full-screen quad (inside the viewport)
	struct VERTEX { float x, y, z, rhw; };
	VERTEX vertices[] = {
		{ (float)ViewPort->X - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)ViewPort->X - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f }
	};

	RenderState->SetFVF(D3DFVF_XYZRHW, false);

	Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VERTEX));

	Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}


/*
* Filters the Shadow Map of given index using a 2 pass gaussian blur
*/
void ShadowManager::BlurShadowAtlas(unsigned cascadeMask) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	
	IDirect3DDevice9* Device = TheRenderManager->device;
    NiDX9RenderState* RenderState = TheRenderManager->renderState;

	// Ping-pong: horizontal pass atlas -> scratch, vertical pass scratch -> atlas.
	//
	// Neither pass may sample the atlas while ShadowAtlasSurface -- level 0 of that same
	// texture -- is the bound render target. Reading a bound render target is undefined in
	// D3D9 and a read/write feedback loop on Vulkan under DXVK. This Gaussian is the only
	// filtering the shadow maps get (the cascade lookup is a single tap), so anything that
	// compromises it shows up directly as hard, unfiltered texels along every shadow edge.
	if (!cascadeMask || !Shadows->ShadowAtlasBlurTexture || !Shadows->ShadowAtlasBlurSurface) return;

	DWORD oldScissorEnabled = FALSE;
	RECT oldScissor = {};
	if (FAILED(Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissorEnabled)) ||
		FAILED(Device->GetScissorRect(&oldScissor))) return;

    Device->SetDepthStencilSurface(NULL);
    RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetVertexShader(ShadowMapBlurVertex->ShaderHandle, false);
    RenderState->SetPixelShader(ShadowMapBlurPixel->ShaderHandle, false);
	RenderState->SetFVF(FrameFVF, false);
	Device->SetStreamSource(0, Shadows->ShadowAtlasVertexBuffer, 0, sizeof(FrameVS));

	// Pass map resolution to shader as a constant
	ShadowMapBlurPixel->SetShaderConstantF(0, &Shadows->Constants.ShadowBlur, 1);

	// blur in two passes, horizontally then vertically
	D3DXVECTOR4 Blur[2] = {
		D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f),
		D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f),
	};
	IDirect3DTexture9* Source[2] = { Shadows->ShadowAtlasTexture, Shadows->ShadowAtlasBlurTexture };
	IDirect3DSurface9* Target[2] = { Shadows->ShadowAtlasBlurSurface, Shadows->ShadowAtlasSurface };

	for (int i = 0; i < 2; i++) {
		// Unbind the previous pass's target before it becomes this pass's source, so the
		// two are never bound as texture and render target at the same time.
		RenderState->SetTexture(0, nullptr);
		Device->SetRenderTarget(0, Target[i]);
		RenderState->SetTexture(0, Source[i]);

		// set blur direction shader constants
		ShadowMapBlurPixel->SetShaderConstantF(1, &Blur[i], 1);

		RenderState->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE, RenderStateArgs);
		for (int cascade = MapNear; cascade < MapOrtho; ++cascade) {
			if (!(cascadeMask & (1u << cascade))) continue;
			const D3DVIEWPORT9& viewport = Shadows->ShadowMaps[cascade].ShadowMapViewPort;
			RECT region = {(LONG)viewport.X, (LONG)viewport.Y,
				(LONG)(viewport.X + viewport.Width), (LONG)(viewport.Y + viewport.Height)};
			Device->SetScissorRect(&region);
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
		}
	}

	RenderState->SetTexture(0, nullptr);
	Device->SetScissorRect(&oldScissor);
	RenderState->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissorEnabled, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}

