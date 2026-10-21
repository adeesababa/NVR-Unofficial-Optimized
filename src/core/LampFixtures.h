#pragma once

#include <vector>
#include <unordered_map>
#include <cstring>
#include <cctype>

namespace LampFixtures {

	struct Glow { float x, y, z, radius, emit; const char* name; const char* root; int model; };

	inline std::vector<Glow> Glows;
	inline const void* GlowCell = nullptr;
	inline double GlowListed = -1.0e9, CellEntered = 0.0;
	inline unsigned Generation = 0;
	inline bool Ready = false;
	inline UInt64 GlowHash = 0;

	struct Part { float x, y, z, radius; };
	struct Model { float x, y, z, radius; const NiNode* root; std::vector<Part> parts; const char* name; };
	inline bool LogModels = false;
	inline unsigned LogModelLines = 0;
	inline float OwnLightDistance = 24.0f;
	inline std::vector<Model> Models;
	inline std::unordered_map<const void*, int> ModelOf;
	inline std::unordered_map<const void*, int> ObjectRoots;
	inline constexpr float MaxLampModel = 200.0f;

	inline bool LampName(const char* path) {
		if (!path || !*path) return false;
		const char* name = path;
		for (const char* c = path; *c; c++) if (*c == '\\' || *c == '/') name = c + 1;
		char low[128]; size_t n = 0;
		for (; name[n] && n < sizeof(low) - 1; n++) low[n] = (char)tolower((unsigned char)name[n]);
		low[n] = 0;
		static const char* const keys[] = { "light", "lamp", "fan", "chandel", "sconce", "bulb", "lantern", "candle", "torch" };
		for (const char* k : keys) if (strstr(low, k)) return true;
		return false;
	}

	inline const char* ModelPath(TESForm* base) {
		if (!base) return nullptr;
		if (base->formType == TESForm::FormType::kFormType_Stat) return static_cast<TESObjectSTAT*>(base)->model.nifPath.m_data;
		if (base->formType == TESForm::FormType::kFormType_Light) return static_cast<TESObjectLIGH*>(base)->modelSwap.nifPath.m_data;
		return nullptr;
	}

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
		const float mult = material->fEmitMult > 0.0f ? material->fEmitMult : 0.0f;
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
		std::vector<Model> models;
		std::unordered_map<const void*, int> modelOf, objectRoots;
		std::vector<NiAVObject*> stack;
		UInt64 hash = 1469598103934665603ull;
		unsigned refs = 0, geometries = 0;
		for (TList<TESObjectREFR>::Entry* entry = &cell->objectList.First; entry; entry = entry->next) {
			TESObjectREFR* ref = entry->item;
			if (!ref || !ref->baseForm) continue;
			NiNode* root = ref->GetNode();
			if (!root) continue;
			refs++;
			int model = -1;
			const char* path = ModelPath(ref->baseForm);
			const bool modelLessLight = ref->baseForm->formType == TESForm::FormType::kFormType_Light && !(path && *path);
			if (modelLessLight) continue;
			objectRoots[root] = 1;
			NiBound* rootBound = root->GetWorldBound();
			if ((path && *path ? LampName(path) : LampName(root->m_pcName)) && rootBound && rootBound->Radius > 0.0f && rootBound->Radius <= MaxLampModel) {
				model = (int)models.size();
				models.push_back({ rootBound->Center.x, rootBound->Center.y, rootBound->Center.z, rootBound->Radius, root, {}, path && *path ? path : root->m_pcName });
				hash = (hash ^ (UInt64)(SInt64)(rootBound->Center.x * 8.0f + rootBound->Center.y * 32.0f + rootBound->Center.z * 128.0f)) * 1099511628211ull;
			}
			stack.clear();
			stack.push_back(root);
			while (!stack.empty()) {
				NiAVObject* object = stack.back();
				stack.pop_back();
				if (!object || (object->m_flags & NiAVObject::NiFlags::APP_CULLED)) continue;
				if (object->IsGeometry()) {
					NiGeometry* geo = static_cast<NiGeometry*>(object);
					geometries++;
					if (model >= 0) {
						modelOf[geo] = model;
						if (NiBound* b = geo->GetWorldBound()) models[model].parts.push_back({ b->Center.x, b->Center.y, b->Center.z, b->Radius });
					}
					const float emit = Emission(geo);
					if (emit < 0.25f) continue;
					NiBound* bound = geo->GetWorldBound();
					if (!bound || !(bound->Radius > 0.0f) || bound->Radius > MaxLampPart) continue;
					found.push_back({ bound->Center.x, bound->Center.y, bound->Center.z, bound->Radius, emit, geo->m_pcName, root->m_pcName, model });
					hash = (hash ^ (UInt64)(SInt64)(bound->Center.x * 4.0f + bound->Center.y * 16.0f + bound->Center.z * 64.0f)) * 1099511628211ull;
					continue;
				}
				if (NiNode* node = object->IsNiNode())
					for (int i = 0; i < node->m_children.end; i++) stack.push_back(node->m_children.data[i]);
			}
		}
		hash ^= found.size() ^ ((UInt64)models.size() << 20) ^ ((UInt64)(refs > 0) << 40);
		Ready = refs > 0;
		const bool changed = hash != GlowHash;
		if (changed) { GlowHash = hash; Generation++; }
		if (log && changed) {
			Logger::Log("UNOFFICIAL LampFixtures: %u glowing meshes and %u lamp models (%u meshes) among %u meshes of %u placed objects with 3D (listed in %.2f ms)",
				(unsigned)found.size(), (unsigned)models.size(), (unsigned)modelOf.size(), geometries, refs, (Now() - started) * 1000.0);
			for (size_t i = 0; i < found.size() && i < 160; i++)
				Logger::Log("  GLOW %3u at %.0f %.0f %.0f radius %.0f emissive %.2f | '%s' of '%s'", (unsigned)i, found[i].x, found[i].y, found[i].z,
					found[i].radius, found[i].emit, found[i].name ? found[i].name : "", found[i].root ? found[i].root : "");
		}
		Glows.swap(found);
		Models.swap(models);
		ModelOf.swap(modelOf);
		ObjectRoots.swap(objectRoots);
	}

	inline bool OwnLight(const Model& m, const NiPointLight* light, const NiPoint3* at) {
		if (OwnLightDistance < 0.0f) return false;
		bool builtIn = false;
		for (const NiAVObject* node = light; node; node = node->m_parent) {
			if (node == m.root) return true;
			if (ObjectRoots.count(node)) builtIn = true;
		}
		if (!builtIn || !(OwnLightDistance > 0.0f)) return false;
		for (const Part& p : m.parts) {
			if (p.radius > MaxLampPart) continue;
			const float dx = p.x - at->x, dy = p.y - at->y, dz = p.z - at->z;
			if (sqrtf(dx * dx + dy * dy + dz * dz) - p.radius <= OwnLightDistance) return true;
		}
		return false;
	}

	inline int ModelIndexOf(const NiGeometry* geo) { auto it = ModelOf.find(geo); return it == ModelOf.end() ? -1 : it->second; }

	inline bool NearLampModel(const NiGeometry* geo, const NiPointLight* light, const NiPoint3* at, float distance, std::unordered_map<int, int>& cache) {
		auto it = ModelOf.find(geo);
		if (it == ModelOf.end()) return false;
		int& answer = cache[it->second];
		if (!answer) {
			const Model& m = Models[it->second];
			const float dx = m.x - at->x, dy = m.y - at->y, dz = m.z - at->z;
			const float edge = sqrtf(dx * dx + dy * dy + dz * dz) - m.radius;
			const bool close = edge <= distance, own = close && OwnLight(m, light, at);
			answer = close && !own ? 1 : 2;
			if (LogModels && close && LogModelLines < 400) {
				LogModelLines++;
				float smallest = 1.0e9f;
				for (const Part& p : m.parts) smallest = (std::min)(smallest, p.radius);
				Logger::Log("LAMPMODEL light %p at %.0f %.0f %.0f: lamp model '%s' (radius %.0f, %u meshes, smallest %.0f) edge %.0f units: %s",
					(const void*)light, at->x, at->y, at->z, m.name ? m.name : "", m.radius, (unsigned)m.parts.size(), smallest, edge,
					own ? "its OWN lamp, keeps shadowing it" : "LEFT OUT of its shadows");
			}
		}
		return answer == 1;
	}

	inline void Update(TESObjectCELL* cell, bool log) {
		if (!cell) return;
		const double now = Now();
		if (cell != GlowCell) { GlowCell = cell; CellEntered = now; GlowListed = -1.0e9; Answers.clear(); Ready = false; Models.clear(); ModelOf.clear(); ObjectRoots.clear(); }
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

	inline const Glow* NearestLampPart(const NiPointLight* light, float maxDistance) {
		const NiPoint3& p = light->m_worldTransform.pos;
		const Glow* best = nullptr; float bestDistance = maxDistance * maxDistance;
		for (const Glow& g : Glows) {
			const float dx = g.x - p.x, dy = g.y - p.y, dz = g.z - p.z, d = dx * dx + dy * dy + dz * dz;
			if (d <= bestDistance) { bestDistance = d; best = &g; }
		}
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
