#pragma once

#include <vector>
#include <unordered_map>

// UNOFFICIAL 2026-10-20 (interior look): which lamps sit at a visible lamp, for [Shaders.ShadowsInteriors.Forward]
// InvisibleLightShadowStrength.
//
// New Vegas lights its interiors with invisible lights: LIGH forms without a model, placed by the level designers for mood
// ("OfficeLightKeyNeutral", "BasementLightFillCool", "...Ambient", "...Kicker") wherever they wanted light, not at the lamps
// the player sees. FalloutNV.esm (bench\deep-roadmap\evidence\intlook\esm_lamps.py): Doc Mitchell's house has 25 lights, all
// without a model, and only 1 within 32 units of a lamp mesh (its wall sconces' nearest lights stand 100-160 units out in the
// room); the Prospector Saloon 24, 6 at a lamp. The game never gave lamps shadows, so where they stand did not matter; NVR's
// lamp shadows cast from those points: a sconce lit from the middle of the room throws its whole outline on the wall behind
// it, fill lights under a ceiling fan throw the blades onto the ceiling, and shadows fall from places where nothing glows.
//
// A light counts as AT A LAMP when a lamp part (LampPart: a small glowing mesh -- lamp glass, bulbs, glow sprites, flames) of
// a placed object of the cell lies within FixtureDistance of it; every other light is an invisible light. Many lamp models carry
// their own lights (each chandelier globe, each sconce bulb); those sit at their glass and always count.
// [Shaders.ShadowsInteriors.Forward] LampGlassShadows false also leaves lamp parts out of every lamp's shadow map: glowing glass
// is the light source, yet drawn as a solid blocker it threw a lamp's whole shade outline onto the wall (ShadowManager).
// Carried lights (torches, the Pip-Boy light) always count as at a lamp. The cell's glowing meshes are listed again whenever
// the cell changes and every few seconds after (objects load their 3D after the cell itself); a light keeps its answer until
// that list changes.
namespace LampFixtures {

	struct Glow { float x, y, z, radius, emit; const char* name; const char* root; };	// name/root: LogLamps only (engine strings)

	inline std::vector<Glow> Glows;							// the cell's glowing meshes (world bound centre and radius)
	inline const void* GlowCell = nullptr;					// the cell they were listed for
	inline double GlowListed = -1.0e9, CellEntered = 0.0;	// when (seconds, QueryPerformanceCounter)
	inline unsigned Generation = 0;							// bumped whenever the list changes
	inline bool Ready = false;								// the last listing saw placed objects with 3D (else: no answer yet)
	inline UInt64 GlowHash = 0;

	struct Answer { float x, y, z; unsigned generation; float distance; };
	inline std::unordered_map<const void*, Answer> Answers;	// per NiPointLight: the nearest glowing mesh's distance

	inline double Now() {
		LARGE_INTEGER frequency, counter;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		return (double)counter.QuadPart / (double)frequency.QuadPart;
	}

	// How strongly a mesh glows: its material's emissive colour (times its multiplier) as luma; 0 without a material.
	inline float Emission(NiGeometry* geo) {
		NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
		if (!material) return 0.0f;
		const float mult = material->fEmitMult > 0.0f ? material->fEmitMult : 1.0f;
		return (0.2126f * material->emit.r + 0.7152f * material->emit.g + 0.0722f * material->emit.b) * mult;
	}

	// A lamp's glowing part: glows (emissive luma >= 0.25) and is small. Measured in game (LogLamps GLOW lines, 2026-10-20, saloon +
	// Doc Mitchell's house): every lamp part was 3-33 units in bound radius (bulbs 3-9, chandelier globes 9, sconce glass 12-16,
	// glow sprites 18-25, a hanging lamp's glow 33); everything else that glows was 66 or more (the fake window light shafts
	// IndFXLightRays 125-232, window panes 111, wall pieces 109-138, a jukebox 66-80). The shafts reach over the designers'
	// fill lights by the windows, so without the size limit those counted as lamps.
	inline constexpr float MaxLampPart = 48.0f;
	inline bool LampPart(NiGeometry* geo) {
		if (Emission(geo) < 0.25f) return false;
		NiBound* bound = geo->GetWorldBound();
		return bound && bound->Radius > 0.0f && bound->Radius <= MaxLampPart;
	}

	inline void ListGlows(TESObjectCELL* cell, bool log) {
		const double started = Now();
		std::vector<Glow> found;
		std::vector<NiAVObject*> stack;
		UInt64 hash = 1469598103934665603ull;
		unsigned refs = 0, geometries = 0;
		for (TList<TESObjectREFR>::Entry* entry = &cell->objectList.First; entry; entry = entry->next) {
			TESObjectREFR* ref = entry->item;
			if (!ref || !ref->baseForm || ref->baseForm->formType == TESForm::FormType::kFormType_Light) continue;
			NiNode* root = ref->GetNode();
			if (!root) continue;
			refs++;
			stack.clear();
			stack.push_back(root);
			while (!stack.empty()) {
				NiAVObject* object = stack.back();
				stack.pop_back();
				if (!object || (object->m_flags & NiAVObject::NiFlags::APP_CULLED)) continue;
				if (object->IsGeometry()) {
					NiGeometry* geo = static_cast<NiGeometry*>(object);
					geometries++;
					const float emit = Emission(geo);
					if (emit < 0.25f) continue;
					NiBound* bound = geo->GetWorldBound();
					if (!bound || !(bound->Radius > 0.0f) || bound->Radius > MaxLampPart) continue;	// a lamp part (LampPart), not a light shaft
					found.push_back({ bound->Center.x, bound->Center.y, bound->Center.z, bound->Radius, emit, geo->m_pcName, root->m_pcName });
					hash = (hash ^ (UInt64)(SInt64)(bound->Center.x * 4.0f + bound->Center.y * 16.0f + bound->Center.z * 64.0f)) * 1099511628211ull;
					continue;
				}
				if (NiNode* node = object->IsNiNode())
					for (int i = 0; i < node->m_children.end; i++) stack.push_back(node->m_children.data[i]);
			}
		}
		hash ^= found.size() ^ ((UInt64)(refs > 0) << 40);
		Ready = refs > 0;
		const bool changed = hash != GlowHash;
		if (changed) { GlowHash = hash; Generation++; }
		if (log && changed) {
			Logger::Log("UNOFFICIAL LampFixtures: %u glowing meshes among %u meshes of %u placed objects with 3D (listed in %.2f ms)",
				(unsigned)found.size(), geometries, refs, (Now() - started) * 1000.0);
			for (size_t i = 0; i < found.size() && i < 160; i++)
				Logger::Log("  GLOW %3u at %.0f %.0f %.0f radius %.0f emissive %.2f | '%s' of '%s'", (unsigned)i, found[i].x, found[i].y, found[i].z,
					found[i].radius, found[i].emit, found[i].name ? found[i].name : "", found[i].root ? found[i].root : "");
		}
		Glows.swap(found);
	}

	// Once per frame, indoors: list the cell's glowing meshes when the cell changed, every second for its first 10 seconds,
	// then every 10 seconds. log: LogLamps (a line whenever the list changes).
	inline void Update(TESObjectCELL* cell, bool log) {
		if (!cell) return;
		const double now = Now();
		if (cell != GlowCell) { GlowCell = cell; CellEntered = now; GlowListed = -1.0e9; Answers.clear(); Ready = false; }
		const double period = now - CellEntered < 10.0 ? 1.0 : 10.0;
		if (now - GlowListed < period) return;
		GlowListed = now;
		ListGlows(cell, log);
	}

	// Distance (world units) from a light to the nearest glowing mesh's bound (0 inside it); a large number when none.
	inline float NearestGlow(const NiPointLight* light) {
		const NiPoint3& p = light->m_worldTransform.pos;
		auto it = Answers.find(light);
		if (it != Answers.end() && it->second.generation == Generation && it->second.x == p.x && it->second.y == p.y && it->second.z == p.z)
			return it->second.distance;
		float best = 1.0e9f;
		for (const Glow& g : Glows) {
			const float dx = g.x - p.x, dy = g.y - p.y, dz = g.z - p.z;
			best = (std::min)(best, (std::max)(0.0f, sqrtf(dx * dx + dy * dy + dz * dz) - g.radius));
		}
		Answers[light] = { p.x, p.y, p.z, Generation, best };
		return best;
	}

	// LogLamps: which glowing mesh (index in the GLOW lines) is the nearest, -1 none.
	inline int NearestGlowIndex(const NiPointLight* light) {
		const NiPoint3& p = light->m_worldTransform.pos;
		int best = -1; float bestDistance = 1.0e9f;
		for (size_t i = 0; i < Glows.size(); i++) {
			const float dx = Glows[i].x - p.x, dy = Glows[i].y - p.y, dz = Glows[i].z - p.z;
			const float d = (std::max)(0.0f, sqrtf(dx * dx + dy * dy + dz * dz) - Glows[i].radius);
			if (d < bestDistance) { bestDistance = d; best = (int)i; }
		}
		return best;
	}

	// An invisible light: no lamp part within fixtureDistance, and not carried. Right after entering a cell, before its objects have
	// their 3D (in game: the first listing of a cell can see none), there is no answer and every light keeps its shadow as before.
	inline bool Invisible(const NiPointLight* light, float fixtureDistance) {
		if (!light || light->CanCarry || !Ready) return false;
		return NearestGlow(light) > fixtureDistance;
	}
}
