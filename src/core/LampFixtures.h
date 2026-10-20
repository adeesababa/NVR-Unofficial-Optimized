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
// A light counts as AT A LAMP when a glowing mesh (a material with an emissive colour: lamp glass, bulbs, glow sprites,
// candle flames) of a placed object of the cell lies within FixtureDistance of it; every other light is an invisible light.
// Carried lights (torches, the Pip-Boy light) always count as at a lamp. The cell's glowing meshes are listed again whenever
// the cell changes and every few seconds after (objects load their 3D after the cell itself); a light keeps its answer until
// that list changes.
namespace LampFixtures {

	struct Glow { float x, y, z, radius; };

	inline std::vector<Glow> Glows;							// the cell's glowing meshes (world bound centre and radius)
	inline const void* GlowCell = nullptr;					// the cell they were listed for
	inline double GlowListed = -1.0e9, CellEntered = 0.0;	// when (seconds, QueryPerformanceCounter)
	inline unsigned Generation = 0;							// bumped whenever the list changes
	inline UInt64 GlowHash = 0;

	struct Answer { float x, y, z; unsigned generation; float distance; };
	inline std::unordered_map<const void*, Answer> Answers;	// per NiPointLight: the nearest glowing mesh's distance

	inline double Now() {
		LARGE_INTEGER frequency, counter;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		return (double)counter.QuadPart / (double)frequency.QuadPart;
	}

	// A mesh glows when its material's emissive colour (times its multiplier) is clearly above black.
	inline bool Glowing(NiGeometry* geo) {
		NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
		if (!material) return false;
		const float mult = material->fEmitMult > 0.0f ? material->fEmitMult : 1.0f;
		const float luma = (0.2126f * material->emit.r + 0.7152f * material->emit.g + 0.0722f * material->emit.b) * mult;
		return luma >= 0.25f;
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
					if (!Glowing(geo)) continue;
					NiBound* bound = geo->GetWorldBound();
					if (!bound || !(bound->Radius > 0.0f) || bound->Radius > 256.0f) continue;	// a lamp, not a lit-up wall
					found.push_back({ bound->Center.x, bound->Center.y, bound->Center.z, bound->Radius });
					hash = (hash ^ (UInt64)(SInt64)(bound->Center.x * 4.0f + bound->Center.y * 16.0f + bound->Center.z * 64.0f)) * 1099511628211ull;
					continue;
				}
				if (NiNode* node = object->IsNiNode())
					for (int i = 0; i < node->m_children.end; i++) stack.push_back(node->m_children.data[i]);
			}
		}
		hash ^= found.size();
		const bool changed = hash != GlowHash;
		if (changed) { GlowHash = hash; Generation++; }
		if (log && changed)
			Logger::Log("UNOFFICIAL LampFixtures: %u glowing meshes among %u meshes of %u placed objects with 3D (listed in %.2f ms)",
				(unsigned)found.size(), geometries, refs, (Now() - started) * 1000.0);
		Glows.swap(found);
	}

	// Once per frame, indoors: list the cell's glowing meshes when the cell changed, every second for its first 10 seconds,
	// then every 10 seconds. log: LogLamps (a line whenever the list changes).
	inline void Update(TESObjectCELL* cell, bool log) {
		if (!cell) return;
		const double now = Now();
		if (cell != GlowCell) { GlowCell = cell; CellEntered = now; GlowListed = -1.0e9; Answers.clear(); }
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

	// An invisible light: no glowing mesh within fixtureDistance, and not carried.
	inline bool Invisible(const NiPointLight* light, float fixtureDistance) {
		if (!light || light->CanCarry) return false;
		return NearestGlow(light) > fixtureDistance;
	}
}
