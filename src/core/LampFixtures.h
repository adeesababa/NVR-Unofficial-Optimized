#pragma once

#include <vector>
#include <unordered_map>

namespace LampFixtures {

	struct Glow { float x, y, z, radius, emit; const char* name; const char* root; };

	inline std::vector<Glow> Glows;
	inline const void* GlowCell = nullptr;
	inline double GlowListed = -1.0e9, CellEntered = 0.0;
	inline unsigned Generation = 0;
	inline bool Ready = false;
	inline UInt64 GlowHash = 0;

	struct Answer { float x, y, z; unsigned generation; float distance; };
	inline std::unordered_map<const void*, Answer> Answers;

	inline double Now() {
		LARGE_INTEGER frequency, counter;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		return (double)counter.QuadPart / (double)frequency.QuadPart;
	}

	inline float Emission(NiGeometry* geo) {
		NiMaterialProperty* material = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));
		if (!material) return 0.0f;
		const float mult = material->fEmitMult > 0.0f ? material->fEmitMult : 1.0f;
		return (0.2126f * material->emit.r + 0.7152f * material->emit.g + 0.0722f * material->emit.b) * mult;
	}

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
					if (!bound || !(bound->Radius > 0.0f) || bound->Radius > MaxLampPart) continue;
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

	inline void Update(TESObjectCELL* cell, bool log) {
		if (!cell) return;
		const double now = Now();
		if (cell != GlowCell) { GlowCell = cell; CellEntered = now; GlowListed = -1.0e9; Answers.clear(); Ready = false; }
		const double period = now - CellEntered < 10.0 ? 1.0 : 10.0;
		if (now - GlowListed < period) return;
		GlowListed = now;
		ListGlows(cell, log);
	}

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

	inline bool Invisible(const NiPointLight* light, float fixtureDistance) {
		if (!light || light->CanCarry || !Ready) return false;
		return NearestGlow(light) > fixtureDistance;
	}
}
