// UNOFFICIAL, optional (off by default): blood that runs and dries, and pools under bodies. See BloodFlow.h.

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "BloodFlow.h"

// Game addresses (FalloutNV.exe 1.4.0.525), checked against the game's code:
//  ProcessLists, the static object at 0x11E0E80 (444 'mov ecx, 0x11E0E80' in the exe).
//    +0x08  the process array's data (Actor*[]), element i at data[i] (ProcessArray at +0x04, its data at +4: 0x877A30)
//    +0x14  first index of each process level (high = 0, middle high = 1), +0x24 one past the last (0x5DE0F0, 0x5BE5C0)
//    +0x58  GlobalTempEffectList, a BSSimpleList<NiPointer<BSTempEffect>> (ProcessLists::AddTempEffect, 0x973FD0, adds
//           every temp effect but the magic ones there)
//  BSTempEffect::kDecals, a BSSimpleList at 0x11D6A30: where most finished splats go. BSTempEffectSimpleDecal::
//    FinalizeGeometry (0x68C496): permanent ones go nowhere, ones that must fade (+0x2E) to the temp effect list above,
//    the rest to kDecals. The game does not age the ones in kDecals, so a splat's age is counted here from when it
//    is first seen (SplatsSeen).
//  BSTempEffect: +0x08 lifetime, +0x10 age (seconds).
//  BSTempEffectSimpleDecal (vtable 0x1069DDC): +0x2D valid (GetIsValid, 0x4A0350), +0x30 texture set, +0x34 origin
//    (GetOrigin, 0x4A0D30; WORLD space: BGSDecalManager::AddDecal measures the player's distance to it), +0x40 the way
//    it was thrown (world), +0x114 its turn, +0x118 width, +0x11C height (GetDecalHeight, 0x4A0D90), +0x120 depth,
//    +0x140 origin given in the hit object's own space (then ignored here). NOT world space: +0x4C and +0x58 are the
//    centre and direction moved into the hit mesh's own space (ProcessGeometry, 0x68B4C0: inverse world rotation, over
//    its world scale, as are width/height/depth). Round 1-2 used +0x4C as the centre, so every splat failed the
//    distance test and no drips were ever drawn (log 2026-10-12: splats found, no 'splat at' lines).
//  BGSTextureSet: the diffuse texture's path (a BSString) at +0x3C.
namespace {
	const UInt32 ProcessListsAddress = 0x11E0E80;
	const UInt32 SimpleDecalVtbl = 0x1069DDC;

	struct RawSplat {
		float Origin[3], Direction[3], Center[3], Normal[3];
		float Width, Height, Depth, Age, Lifetime;
		bool Local;			// the origin is in the hit object's own space
		float GameAge;		// the game's own age (Age becomes ours, counted from when it was first seen)
		void* TextureSet;
		void* Decal;
	};

	struct ListNode { void* Item; ListNode* Next; };

	const UInt32 DecalListAddress = 0x11D6A30;

	// Every simple decal in one of the game's lists (up to max, after the n already in out). The game can change the
	// lists between frames only, but they are walked under SEH anyway: a bad read just ends the walk.
	int ReadSplats(UInt32 list, RawSplat* out, int n, int max) {
		__try {
			ListNode* node = (ListNode*)list;
			for (int guard = 0; node && guard < 16384 && n < max; node = node->Next, guard++) {
				UInt8* e = (UInt8*)node->Item;
				if (!e || *(UInt32*)e != SimpleDecalVtbl) continue;
				if (!*(bool*)(e + 0x2D)) continue;
				RawSplat& s = out[n];
				memcpy(s.Origin, e + 0x34, 12);
				memcpy(s.Direction, e + 0x40, 12);
				memcpy(s.Center, e + 0x4C, 12);
				memcpy(s.Normal, e + 0x58, 12);
				s.Width = *(float*)(e + 0x118);
				s.Height = *(float*)(e + 0x11C);
				s.Depth = *(float*)(e + 0x120);
				s.Local = *(bool*)(e + 0x140);
				s.Age = *(float*)(e + 0x10);
				s.Lifetime = *(float*)(e + 0x08);
				s.GameAge = s.Age;
				s.TextureSet = *(void**)(e + 0x30);
				s.Decal = e;
				n++;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}

	// The texture set's diffuse path, lower case, into out (empty if unreadable).
	void ReadTexturePath(void* textureSet, char* out, int size) {
		out[0] = 0;
		__try {
			const char* path = *(const char**)((UInt8*)textureSet + 0x3C);
			if (!path) return;
			int i = 0;
			for (; i < size - 1 && path[i]; i++) out[i] = (char)tolower((unsigned char)path[i]);
			out[i] = 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
	}

	struct RawActor {
		UInt32 RefID;
		UInt32 LifeState;
		bool Bleeds;			// not a robot
		bool HasBody;			// the fields below are set
		float BoundCenter[3], BoundRadius;
		float MinZ;
	};

	// The world position of every node under root (up to 256 nodes): the lowest z, the middle of the skeleton (a spine
	// bone if there is one, else the average of the nodes below the root) and how many were seen. Nodes further than
	// `reach` from `bound` are left out: some sit at the world's origin (not placed yet), which once put a pool at z -1.5.
	int WalkNodes(NiAVObject* root, float& minZ, float* centre, const float* bound, float reach) {
		NiAVObject* stack[64];
		int top = 0, seen = 0, nodes = 0;
		float sum[3] = {}, spine[3] = {};
		bool haveSpine = false;
		stack[top++] = root;
		while (top && seen < 256) {
			NiAVObject* object = stack[--top];
			if (!object) continue;
			seen++;
			const NiPoint3& at = object->m_worldTransform.pos;
			const float dx = at.x - bound[0], dy = at.y - bound[1], dz = at.z - bound[2];
			const bool inReach = dx * dx + dy * dy + dz * dz <= reach * reach;
			if (inReach) minZ = min(minZ, at.z);
			NiNode* node = object->IsNiNode();
			if (!node) continue;
			if (object != root && inReach) {
				sum[0] += at.x; sum[1] += at.y; sum[2] += at.z;
				nodes++;
				const char* name = object->m_pcName;
				if (!haveSpine && name && strstr(name, "Spine")) { spine[0] = at.x; spine[1] = at.y; spine[2] = at.z; haveSpine = true; }
			}
			for (UInt32 i = 0; i < node->m_children.end && top < 64; i++)
				if (node->m_children.data[i]) stack[top++] = node->m_children.data[i];
		}
		if (haveSpine) { centre[0] = spine[0]; centre[1] = spine[1]; centre[2] = spine[2]; }
		else if (nodes) { centre[0] = sum[0] / nodes; centre[1] = sum[1] / nodes; centre[2] = sum[2] / nodes; }
		return nodes ? seen : 0;
	}

	// The actors in the high and middle-high process levels: alive ones (refID and state only, for telling a fresh death
	// from a body that was already there) and dead or dying ones with their body's position.
	int ReadActors(RawActor* out, int max) {
		int n = 0;
		__try {
			UInt8* lists = (UInt8*)ProcessListsAddress;
			Actor** data = *(Actor***)(lists + 0x08);
			if (!data) return 0;
			for (int level = 0; level < 2; level++) {
				UInt32 begin = *(UInt32*)(lists + 0x14 + level * 4);
				UInt32 end = *(UInt32*)(lists + 0x24 + level * 4);
				if (end < begin || end - begin > 4096) continue;
				for (UInt32 i = begin; i < end && n < max; i++) {
					Actor* actor = data[i];
					if (!actor || (TESObjectREFR*)actor == (TESObjectREFR*)Player) continue;
					RawActor& a = out[n];
					a.RefID = actor->refID;
					a.LifeState = actor->lifeState;
					a.HasBody = false;
					a.Bleeds = true;
					TESForm* base = actor->baseForm;
					if (base && base->formType == TESForm::kFormType_Creature && *((UInt8*)base + 0x12C) == 6) a.Bleeds = false;  // robot
					if ((actor->flags & 0x800) != 0) a.Bleeds = false;   // disabled
					if ((a.LifeState == 1 || a.LifeState == 2) && a.Bleeds) {
						NiNode* root = actor->GetNode();
						if (root && root->m_kWorldBound) {
							const NiBound* bound = root->m_kWorldBound;
							a.BoundCenter[0] = bound->Center.x;
							a.BoundCenter[1] = bound->Center.y;
							a.BoundCenter[2] = bound->Center.z;
							a.BoundRadius = bound->Radius;
							a.MinZ = 1e30f;
							const float boundAt[3] = { bound->Center.x, bound->Center.y, bound->Center.z };
							a.HasBody = WalkNodes(root, a.MinZ, a.BoundCenter, boundAt, bound->Radius * 1.5f + 16.0f) > 4 && a.BoundRadius > 1.0f &&
								a.MinZ < 1e29f && a.MinZ > bound->Center.z - bound->Radius * 1.5f - 16.0f;
						}
					}
					n++;
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return n;
	}

	float Hash01(UInt32 x) {
		x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
		return (float)(x & 0xFFFFFF) / 16777216.0f;
	}

	UInt32 HashFloats(const float* v, int count) {
		UInt32 h = 2166136261u;
		for (int i = 0; i < count; i++) {
			UInt32 bits;
			memcpy(&bits, &v[i], 4);
			h = (h ^ bits) * 16777619u;
		}
		return h;
	}

	float SrgbToLinear(float c) {
		c = std::clamp(c, 0.0f, 1.0f);
		return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
	}

	using BloodFlowBoxes::Length;

	// What we know of each body: when it was last seen alive, when it died, and where its pool is.
	struct Body {
		double AliveAt = -1.0;		// Clock when last seen alive (-1: never)
		double DeadAt = -1.0;		// Clock when first seen dead or dying
		double SeenAt = 0.0;		// last seen at all
		bool Locked = false;		// the pool's place is fixed (the body has come to rest)
		double LockAt = 0.0;		// Clock when the pool started
		double StillSince = -1.0;
		D3DXVECTOR3 Last = D3DXVECTOR3(0, 0, 0);	// body centre last frame
		D3DXVECTOR3 Pool = D3DXVECTOR3(0, 0, 0);	// the pool's centre on the ground
		float MaxRadius = 0.0f;
	};
	std::unordered_map<UInt32, Body> Bodies;
	std::unordered_map<void*, bool> BloodTextureSets;	// texture set -> is it blood
	int LoggedTextures = 0, LoggedSplats = 0, LoggedPools = 0, LoggedLists = 0, LoggedNearest = 0;

	// When each splat was first seen (the game does not age most of them). Keyed by the decal, checked by its origin, so a
	// new splat made where an old one was freed starts afresh.
	struct SplatSeen { UInt32 Key; double FirstSeen; double LastSeen; };
	std::unordered_map<void*, SplatSeen> SplatsSeen;
	double LastSplatGather = -1e9;
}

void BloodFlowEffect::RegisterConstants() {
	TheShaderManager->RegisterConstant("TESR_BloodFlowFresh", &Constants.Fresh);
	TheShaderManager->RegisterConstant("TESR_BloodFlowDry", &Constants.Dry);
	TheShaderManager->RegisterConstant("TESR_BloodFlowInfo", &Constants.Info);
	TheShaderManager->RegisterConstant("TESR_BloodFlowPool", &Constants.Pool);
	TheShaderManager->RegisterConstant("TESR_BloodFlowProbe", &Constants.Probe);
}

void BloodFlowEffect::RegisterTextures() {
	// The path texture, twice (16 x 576 texels of four floats each, 147 KB apiece): positions in game units need floats.
	TheTextureManager->InitTexture("NVR_BloodFlowPaths0", &PathTexture[0], &PathSurface[0], BloodFlowBoxes::PathColumns, PathRows, D3DFMT_A32B32G32R32F);
	TheTextureManager->InitTexture("NVR_BloodFlowPaths1", &PathTexture[1], &PathSurface[1], BloodFlowBoxes::PathColumns, PathRows, D3DFMT_A32B32G32R32F);
	PathRead = PathTexture[0];
	TheTextureManager->RegisterTexture("TESR_BloodFlowPaths", (IDirect3DBaseTexture9**)&PathRead);
	TheTextureManager->InitTexture("TESR_BloodFlowBounds", &BoundsTexture, &BoundsSurface, 2, ItemMax, D3DFMT_A32B32G32R32F);
}

int BloodFlowEffect::SlotFor(UInt64 key) {
	for (int i = 0; i < ItemMax; i++)
		if (Slots[i].Used && Slots[i].Key == key) { Slots[i].SeenFrame = Frame; return i; }
	// A slot nothing has used, or whose item was not gathered last frame (it starts afresh if it comes back).
	for (int i = 0; i < ItemMax; i++) {
		if (!Slots[i].Used || Frame - Slots[i].SeenFrame > 1) {
			Slots[i] = SlotState();
			Slots[i].Key = key;
			Slots[i].Used = true;
			Slots[i].SeenFrame = Frame;
			return i;
		}
	}
	return -1;
}

void BloodFlowEffect::UpdateSettings() {
	const char* section = "Shaders.BloodFlow.Main";
	Settings.Splats = TheSettingManager->GetSettingI(section, "Splats") != 0;
	Settings.Pools = TheSettingManager->GetSettingI(section, "Pools") != 0;
	Settings.DripLength = std::clamp(TheSettingManager->GetSettingF(section, "DripLength"), 0.0f, 200.0f);
	Settings.DripSpeed = std::clamp(TheSettingManager->GetSettingF(section, "DripSpeed"), 0.05f, 10.0f);
	Settings.DryTime = std::clamp(TheSettingManager->GetSettingF(section, "DryTime"), 1.0f, 3600.0f);
	Settings.PoolSize = std::clamp(TheSettingManager->GetSettingF(section, "PoolSize"), 0.1f, 4.0f);
	Settings.PoolSpreadTime = std::clamp(TheSettingManager->GetSettingF(section, "PoolSpreadTime"), 1.0f, 600.0f);
	Settings.PoolDryTime = std::clamp(TheSettingManager->GetSettingF(section, "PoolDryTime"), 1.0f, 7200.0f);
	Settings.Gloss = std::clamp(TheSettingManager->GetSettingF(section, "Gloss"), 0.0f, 4.0f);
	Settings.FreshColor[0] = TheSettingManager->GetSettingF(section, "FreshColorR");
	Settings.FreshColor[1] = TheSettingManager->GetSettingF(section, "FreshColorG");
	Settings.FreshColor[2] = TheSettingManager->GetSettingF(section, "FreshColorB");
	Settings.DryColor[0] = TheSettingManager->GetSettingF(section, "DryColorR");
	Settings.DryColor[1] = TheSettingManager->GetSettingF(section, "DryColorG");
	Settings.DryColor[2] = TheSettingManager->GetSettingF(section, "DryColorB");
	Settings.MaxFall = std::clamp(TheSettingManager->GetSettingF(section, "MaxFall"), 0.0f, 1000.0f);
	Settings.MaxDistance = std::clamp(TheSettingManager->GetSettingF(section, "MaxDistance"), 200.0f, 20000.0f);
	Settings.MaxSplats = std::clamp(TheSettingManager->GetSettingI(section, "MaxSplats"), 0, ItemMax);
	Settings.MaxPools = std::clamp(TheSettingManager->GetSettingI(section, "MaxPools"), 0, 32);
	Settings.DebugView = TheSettingManager->GetSettingI(section, "DebugView");
}

void BloodFlowEffect::GatherSplats() {
	static RawSplat raw[1024];
	const int fromTemp = ReadSplats(ProcessListsAddress + 0x58, raw, 0, 1024);
	const int total = ReadSplats(DecalListAddress, raw, fromTemp, 1024);
	if (LoggedLists < 10 && total > 0) {
		LoggedLists++;
		Logger::Log("BloodFlow: %d splats in the game's lists (%d temp effects, %d decals)", total, fromTemp, total - fromTemp);
	}
	// Ages: splats seen for the first time now are new, unless the effect has just started (or come back after a pause or
	// a load) or many appear at once (a cell or save loading): those are old, already dried.
	const bool resumed = Clock - LastSplatGather > 1.0;
	LastSplatGather = Clock;
	int appeared = 0;
	for (int i = 0; i < total; i++) {
		auto seen = SplatsSeen.find(raw[i].Decal);
		if (seen == SplatsSeen.end() || seen->second.Key != HashFloats(raw[i].Origin, 3)) appeared++;
	}
	const bool burst = resumed || appeared > 8;
	for (int i = 0; i < total; i++) {
		const UInt32 key = HashFloats(raw[i].Origin, 3);
		SplatSeen& seen = SplatsSeen[raw[i].Decal];
		if (seen.Key != key || seen.LastSeen <= 0.0) seen = { key, burst ? Clock - 1e5 : Clock, Clock };
		seen.LastSeen = Clock;
		raw[i].Age = max(raw[i].Age, (float)min(Clock - seen.FirstSeen, 1e5));
	}
	if (SplatsSeen.size() > 2048 || (SplatsSeen.size() > (size_t)total + 256)) {
		for (auto it = SplatsSeen.begin(); it != SplatsSeen.end();) {
			if (Clock - it->second.LastSeen > 5.0) it = SplatsSeen.erase(it);
			else ++it;
		}
	}
	const D3DXVECTOR3 eye(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	struct Candidate { int Index; float Distance; };
	static Candidate candidates[1024];
	int kept = 0;
	for (int i = 0; i < total; i++) {
		RawSplat& s = raw[i];
		if (!s.TextureSet) continue;
		auto found = BloodTextureSets.find(s.TextureSet);
		if (found == BloodTextureSets.end()) {
			char path[260];
			ReadTexturePath(s.TextureSet, path, sizeof(path));
			const bool blood = strstr(path, "blood") != nullptr;
			found = BloodTextureSets.emplace(s.TextureSet, blood).first;
			if (LoggedTextures < 24) {
				LoggedTextures++;
				Logger::Log("BloodFlow: decal texture '%s' -> %s", path, blood ? "blood" : "not blood");
			}
		}
		if (!found->second || s.Local) continue;
		const D3DXVECTOR3 center(s.Origin[0], s.Origin[1], s.Origin[2]);
		const float distance = Length(center - eye);
		if (!(distance < Settings.MaxDistance)) continue;
		candidates[kept++] = { i, distance };
	}
	std::sort(candidates, candidates + kept, [](const Candidate& a, const Candidate& b) { return a.Distance < b.Distance; });
	kept = min(kept, Settings.MaxSplats);
	for (int c = 0; c < kept && Count < ItemMax; c++) {
		RawSplat& s = raw[candidates[c].Index];
		Item& item = Items[Count];
		const D3DXVECTOR3 center(s.Origin[0], s.Origin[1], s.Origin[2]);
		// The surface's normal is only guessed here (against the way the blood was thrown): the shader finds the real
		// one from the depth buffer.
		D3DXVECTOR3 normal = -D3DXVECTOR3(s.Direction[0], s.Direction[1], s.Direction[2]);
		const float l = Length(normal);
		normal = l > 1e-3f ? normal / l : D3DXVECTOR3(0.0f, 0.0f, 1.0f);
		float radius = 0.5f * max(s.Width, s.Height);
		if (!(radius > 0.5f && radius < 200.0f)) radius = 8.0f;
		item.Center = center;
		item.Normal = normal;
		item.Radius = std::clamp(radius, 1.0f, 60.0f);
		item.Age = std::clamp(s.Age, 0.0f, 1e6f);
		item.Seed = Hash01(HashFloats(s.Origin, 3));
		float fade = 1.0f - std::clamp((candidates[c].Distance - 0.8f * Settings.MaxDistance) / (0.2f * Settings.MaxDistance), 0.0f, 1.0f);
		// Goes as the game removes it (by the game's own clock: most splats are never aged by the game, and their lifetime
		// then says nothing).
		if (s.Lifetime > 0.0f && s.GameAge > 0.0f) fade *= std::clamp((s.Lifetime - s.GameAge) / 2.0f, 0.0f, 1.0f);
		item.Fade = fade;
		item.Pool = false;
		if (item.Fade <= 0.0f) continue;
		UInt64 key = (UInt64)(UInt32)(uintptr_t)s.Decal | ((UInt64)HashFloats(s.Origin, 3) << 32);
		item.Slot = SlotFor(key & ~(1ull << 63));
		item.Fresh = false;
		if (item.Slot < 0) continue;
		if (LoggedSplats < 12) {
			LoggedSplats++;
			Logger::Log("BloodFlow: splat at (%.1f %.1f %.1f), %.0f units away, thrown (%.2f %.2f %.2f), size %.1f x %.1f, depth %.1f, age %.1f of %.1f s (mesh space: centre %.1f %.1f %.1f)",
				s.Origin[0], s.Origin[1], s.Origin[2], candidates[c].Distance, s.Direction[0], s.Direction[1], s.Direction[2],
				s.Width, s.Height, s.Depth, s.Age, s.Lifetime, s.Center[0], s.Center[1], s.Center[2]);
		}
		Count++;
	}
}

void BloodFlowEffect::GatherPools() {
	static RawActor raw[512];
	const int total = ReadActors(raw, 512);
	const D3DXVECTOR3 eye(TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z);
	struct Candidate { UInt32 RefID; float Distance; };
	Candidate candidates[512];
	int kept = 0;
	for (int i = 0; i < total; i++) {
		RawActor& a = raw[i];
		Body& body = Bodies[a.RefID];
		body.SeenAt = Clock;
		const bool dead = a.LifeState == 1 || a.LifeState == 2;
		if (!dead) {
			body = Body();
			body.AliveAt = Clock;
			body.SeenAt = Clock;
			continue;
		}
		if (!a.Bleeds || !a.HasBody) continue;
		// Seen alive a moment ago: it died now. Otherwise the body was already there (loaded with the cell, or a save):
		// its pool is old, spread and dry.
		const bool old = body.DeadAt < 0.0 && !(body.AliveAt >= 0.0 && Clock - body.AliveAt < 5.0);
		if (body.DeadAt < 0.0) body.DeadAt = old ? Clock - 1e5 : Clock;
		const D3DXVECTOR3 now(a.BoundCenter[0], a.BoundCenter[1], a.BoundCenter[2]);
		const float moved = Length(now - body.Last);
		body.Last = now;
		const float maxRadius = std::clamp(a.BoundRadius * 0.3f, 14.0f, 36.0f) * Settings.PoolSize;
		if (body.Locked) {
			// Pushed or dragged well away from its pool: the pool stays behind (it is not drawn any more) and a new one starts.
			const float dx = now.x - body.Pool.x, dy = now.y - body.Pool.y;
			if (sqrtf(dx * dx + dy * dy) > body.MaxRadius * 1.5f) { body.Locked = false; body.StillSince = -1.0; }
		}
		if (!body.Locked) {
			// The pool starts once the body has come to rest (moved under a unit a frame for half a second), or at once
			// for an old body.
			if (moved < 3.0f) { if (body.StillSince < 0.0) body.StillSince = Clock; }
			else body.StillSince = -1.0;
			if (old || (body.StillSince >= 0.0 && Clock - body.StillSince > 0.5) || Clock - body.DeadAt > 3.0) {
				body.Locked = true;
				body.LockAt = old ? Clock - 1e5 : Clock;
				body.Pool = D3DXVECTOR3(now.x, now.y, a.MinZ - 1.5f);
				body.MaxRadius = maxRadius;
				if (LoggedPools < 12) {
					LoggedPools++;
					Logger::Log("BloodFlow: pool under %08X at (%.1f %.1f %.1f), body radius %.1f, lowest node %.1f, pool radius %.1f, %s",
						a.RefID, now.x, now.y, body.Pool.z, a.BoundRadius, a.MinZ, maxRadius, old ? "old body" : "fresh");
				}
			}
		}
		if (!body.Locked) continue;
		const float distance = Length(body.Pool - eye);
		if (!(distance < Settings.MaxDistance)) continue;
		candidates[kept++] = { a.RefID, distance };
	}
	std::sort(candidates, candidates + kept, [](const Candidate& a, const Candidate& b) { return a.Distance < b.Distance; });
	kept = min(kept, Settings.MaxPools);
	for (int c = 0; c < kept && Count < ItemMax; c++) {
		const Body& body = Bodies[candidates[c].RefID];
		// Pools' keys have the top bit set (splats' never do); a pool started afresh (body moved away) gets a new one.
		const UInt64 key = (1ull << 63) | ((UInt64)HashFloats((const float*)&body.Pool, 3) << 32) | candidates[c].RefID;
		const int slot = SlotFor(key);
		if (slot < 0) continue;
		Item& item = Items[Count++];
		item.Slot = slot;
		item.Fresh = false;
		item.Center = body.Pool;
		item.Normal = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
		item.Radius = body.MaxRadius;
		item.Age = (float)min(Clock - body.LockAt, 1e5);
		item.Seed = Hash01(candidates[c].RefID);
		item.Fade = 1.0f - std::clamp((candidates[c].Distance - 0.8f * Settings.MaxDistance) / (0.2f * Settings.MaxDistance), 0.0f, 1.0f);
		item.Pool = true;
		if (c == 0 && LoggedNearest < 30) {
			static double loggedAt = -1e9;
			if (Clock - loggedAt > 2.0) {
				loggedAt = Clock;
				LoggedNearest++;
				const D3DXVECTOR3 rel = body.Pool - eye;
				Logger::Log("BloodFlow: nearest pool %08X at (%.1f %.1f %.1f), from the eye (%.1f %.1f %.1f) = %.0f units, radius %.1f, age %.1f s, eye (%.1f %.1f %.1f)",
					candidates[c].RefID, body.Pool.x, body.Pool.y, body.Pool.z, rel.x, rel.y, rel.z, candidates[c].Distance, item.Radius, item.Age, eye.x, eye.y, eye.z);
			}
		}
	}
	// Forget bodies not seen for a minute (unloaded): coming back, they count as old bodies.
	if (Bodies.size() > 256) {
		for (auto it = Bodies.begin(); it != Bodies.end();) {
			if (Clock - it->second.SeenAt > 60.0) it = Bodies.erase(it);
			else ++it;
		}
	}
}

void BloodFlowEffect::UpdateConstants() {
	Clock += std::clamp((double)TheFrameRateManager->ElapsedTime, 0.0, 0.1);
	Frame++;
	Count = 0;
	// UNOFFICIAL 2026-10-13: SHELVED for the release (the user: not ready). No splats or pools are gathered, so nothing draws
	// (ShouldRender: Count > 0) and the game's lists are not walked, whatever a settings file says. Its settings are gone from
	// the defaults; the code stays for later (toolslood-sim.ps1 still runs the shader offline).
	return;
	if (TheShaderManager->GameState.isUnderwater) return;
	if (Settings.Splats) GatherSplats();
	if (Settings.Pools) GatherPools();

	Constants.Fresh = D3DXVECTOR4(SrgbToLinear(Settings.FreshColor[0]), SrgbToLinear(Settings.FreshColor[1]), SrgbToLinear(Settings.FreshColor[2]), Settings.Gloss);
	Constants.Dry = D3DXVECTOR4(SrgbToLinear(Settings.DryColor[0]), SrgbToLinear(Settings.DryColor[1]), SrgbToLinear(Settings.DryColor[2]), Settings.DryTime);
	Constants.Info = D3DXVECTOR4(TheShaderManager->GameState.isExterior ? 1.0f : 0.0f, (float)Settings.DebugView, Settings.PoolDryTime, Settings.DripSpeed);
	// Gravity: 9.81 m/s^2 at 1.4287 cm a unit.
	Constants.Pool = D3DXVECTOR4(Settings.PoolSpreadTime, Settings.DripLength, Settings.MaxFall, 687.0f);
	// Paths are kept once seen from close enough that a pixel is under 0.8 units (about 1100 units away at 1440p).
	Constants.Probe = D3DXVECTOR4(1.0f / PathRows, 0.8f, (float)PathRows, 0.0f);
}

bool BloodFlowEffect::ShouldRender() {
	return Count > 0;
}

void BloodFlowEffect::Render(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DSurface9* rendered,
	UINT technique, bool clear, IDirect3DSurface9* source) {
	if (!Enabled || !Effect || !ShouldRender()) { renderTime = 0; return; }
	static CachedHandle techniqueCache, probeCache, boundsCache;
	D3DXHANDLE boxes = TechniqueHandle(techniqueCache, "BloodFlow");
	D3DXHANDLE probe = TechniqueHandle(probeCache, "BloodFlowProbe");
	D3DXHANDLE boundsTechnique = TechniqueHandle(boundsCache, "BloodFlowBounds");
	if (!boxes || !probe || !boundsTechnique || !PathSurface[0] || !PathSurface[1] || !BoundsSurface) return;
	if (!Declaration) {
		const D3DVERTEXELEMENT9 elements[] = {
			{ 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
			{ 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
			{ 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1 },
			{ 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2 },
			{ 0, 64, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 3 },
			D3DDECL_END() };
		if (FAILED(device->CreateVertexDeclaration(elements, &Declaration))) { Declaration = nullptr; return; }
	}

	const D3DXVECTOR4& cameraPosition = TheRenderManager->CameraPosition;
	const D3DXVECTOR3 eye(cameraPosition.x, cameraPosition.y, cameraPosition.z);
	const D3DXMATRIX& inv = TheRenderManager->InvViewMatrix;
	const float w = (float)TheRenderManager->width, h = (float)TheRenderManager->height;

	IDirect3DSurface9* previousTarget = nullptr;
	IDirect3DSurface9* previousDepth = nullptr;
	IDirect3DVertexDeclaration9* previousDeclaration = nullptr;
	IDirect3DVertexBuffer9* previousStream = nullptr;
	UINT previousOffset = 0, previousStride = 0;
	device->GetRenderTarget(0, &previousTarget);
	device->GetDepthStencilSurface(&previousDepth);
	device->GetVertexDeclaration(&previousDeclaration);
	device->GetStreamSource(0, &previousStream, &previousOffset, &previousStride);

	// 1. Where each stream runs (BloodFlow.fx.hlsl, ProbePS), for every item gathered, on screen or not: from last frame's
	// path texture into the other one. An item whose rows were not written last time (new, or skipped a frame) starts afresh.
	for (int i = 0; i < Count; i++) Items[i].Fresh = Slots[Items[i].Slot].WrittenPass != ProbePasses - 1;
	const int probeTriangles = BloodFlowBoxes::BuildProbe(Items, Count, Settings.DripLength, PathRows, BloodFlowBoxes::Streams, eye, ProbeVertices);
	const int write = 1 - PathIndex;
	PathRead = PathTexture[PathIndex];
	device->SetRenderTarget(0, PathSurface[write]);
	// (The path texture is smaller than the game's depth-stencil, which is legal; without ReShadeDepthCompat it is unbound.)
	if (!TheSettingManager->SettingsMain.Main.ReShadeDepthCompat) device->SetDepthStencilSurface(nullptr);
	Effect->SetTechnique(probe);
	SetCT();
	UINT passes = 0;
	if (probeTriangles && SUCCEEDED(Effect->Begin(&passes, 0))) {
		Effect->BeginPass(0);
		device->SetVertexDeclaration(Declaration);
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, probeTriangles, ProbeVertices, sizeof(VolumetricSmokeQuads::Vertex));
		Effect->EndPass();
		Effect->End();
	}
	for (int i = 0; i < Count; i++) Slots[Items[i].Slot].WrittenPass = ProbePasses;
	ProbePasses++;
	PathIndex = write;
	PathRead = PathTexture[PathIndex];

	// 2. The box round each item's real paths (BoundsPS), from the paths just found: the paint pass skips the rest.
	const int boundsTriangles = BloodFlowBoxes::BuildProbe(Items, Count, Settings.DripLength, ItemMax, 1, eye, BoundsVertices);
	device->SetRenderTarget(0, BoundsSurface);
	Effect->SetTechnique(boundsTechnique);
	SetCT();
	// (Not bound to its own sampler while drawn into: TESR_BloodFlowBounds is the 7th sampler declared, register 6.)
	device->SetTexture(6, nullptr);
	TheRenderManager->renderState->NoteDeviceTexture(6, nullptr);
	if (boundsTriangles && SUCCEEDED(Effect->Begin(&passes, 0))) {
		Effect->BeginPass(0);
		device->SetVertexDeclaration(Declaration);
		device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, boundsTriangles, BoundsVertices, sizeof(VolumetricSmokeQuads::Vertex));
		Effect->EndPass();
		Effect->End();
	}
	device->SetDepthStencilSurface(previousDepth);

	// 3. Each item's box (BloodFlowBoxes.h), projected in NVR's camera-centred space as VolumetricSmoke does. Its rows
	// were written just now, so none is fresh for this pass.
	for (int i = 0; i < Count; i++) Items[i].Fresh = false;
	float bounds[4];
	const int triangles = BloodFlowBoxes::Build(Items, Count, Settings.DripLength, Settings.MaxFall, eye, D3DXVECTOR3(inv._41, inv._42, inv._43),
		TheRenderManager->ViewProjMatrix, Vertices, bounds);
	const float minU = bounds[0], minV = bounds[1], maxU = bounds[2], maxV = bounds[3];

	// The shader blends straight into the current image (the chain's current texture, or the game's target without the
	// chain), reading the image as it was from TESR_SourceBuffer: only the area the boxes cover is copied there.
	FrameChain& chain = TheShaderManager->Chain;
	const bool chained = chain.Owns(target, rendered);
	IDirect3DSurface9* image = chained ? TheTextureManager->RenderedSurface : target;
	RECT area;
	area.left = (LONG)max(0.0f, floorf(std::clamp(minU, 0.0f, 1.0f) * w) - 2.0f);
	area.top = (LONG)max(0.0f, floorf(std::clamp(minV, 0.0f, 1.0f) * h) - 2.0f);
	area.right = (LONG)min(w, ceilf(std::clamp(maxU, 0.0f, 1.0f) * w) + 2.0f);
	area.bottom = (LONG)min(h, ceilf(std::clamp(maxV, 0.0f, 1.0f) * h) + 2.0f);
	if (triangles && area.right > area.left && area.bottom > area.top) {
		if (source) device->StretchRect(image, &area, source, &area, D3DTEXF_NONE);
		device->SetRenderTarget(0, image);
		Effect->SetTechnique(boxes);
		SetCT();
		if (SUCCEEDED(Effect->Begin(&passes, 0))) {
			Effect->BeginPass(0);
			device->SetVertexDeclaration(Declaration);
			device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, Vertices, sizeof(VolumetricSmokeQuads::Vertex));
			Effect->EndPass();
			Effect->End();
		}
	}
	if (previousDeclaration) device->SetVertexDeclaration(previousDeclaration);
	device->SetStreamSource(0, previousStream, previousOffset, previousStride);
	device->SetRenderTarget(0, previousTarget);
	if (previousTarget) previousTarget->Release();
	if (previousDepth) previousDepth->Release();
	if (previousDeclaration) previousDeclaration->Release();
	if (previousStream) previousStream->Release();
	// Without the chain the rendered texture is kept equal to the target.
	if (triangles && area.right > area.left && area.bottom > area.top && !chained && rendered && rendered != target)
		device->StretchRect(target, &area, rendered, &area, D3DTEXF_NONE);
}
