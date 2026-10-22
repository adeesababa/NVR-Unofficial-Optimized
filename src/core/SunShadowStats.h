#pragma once

#include <intrin.h>
#include "TestTools.h"
#include "GpuProfiler.h"
#if NVR_TEST_TOOLS
#include "SettingShots.h"
#endif

namespace SunShadowStats {
#if NVR_TEST_TOOLS
	enum Pass { kOpaque, kAlpha, kSkinned, kTree, kLodLand, kPasses };

	struct Cascade {
		unsigned long long frames;
		unsigned long long tWalkLod, tWalkCells, tClear, tDraws;
		unsigned long long tUpdate, tSetCT, tWind, tRender;
		unsigned long long refs, refNodes, refsIn, nodes, childTests, geometry;
		unsigned long long draws[kPasses], partitions;
		unsigned long long vbChanges, ibChanges, declChanges, cullChanges, texChanges;
		unsigned long long refsOutside;
		double boundRadiusIn;
		float radius;
	};

	inline int Mode = 0;
	inline Cascade Sum[4] = {};
	inline Cascade* Current = nullptr;
	inline int CurrentPass = -1;
	inline unsigned long long tLoop = 0;
	inline unsigned long long tSharedWalk = 0;
	inline unsigned Frames = 0;
	inline unsigned long long WindowTsc = 0;
	inline double WindowMs = 0.0;
	inline size_t ShotSeen = (size_t)-1;
	inline bool ShotWindow = false;

	inline const void* LastVB = nullptr; inline const void* LastIB = nullptr; inline const void* LastDecl = nullptr;
	inline UInt32 LastCull = ~0u; inline const void* LastTex = nullptr;

	struct Column { D3DXVECTOR3 center, sun; float radius; const NiFrustumPlanes* planes; };
	inline Column CurrentColumn = {};
	inline unsigned ExamplesLogged = 0;
	inline void CheckRef(const NiNode* node, const TESObjectREFR* ref) {
		if (Mode < 3 || !Current || !node || !node->m_kWorldBound) return;
		const NiBound* b = node->m_kWorldBound;
		const D3DXVECTOR3 rel(b->Center.x - CurrentColumn.center.x, b->Center.y - CurrentColumn.center.y, b->Center.z - CurrentColumn.center.z);
		const float along = D3DXVec3Dot(&rel, &CurrentColumn.sun);
		const D3DXVECTOR3 perp = rel - along * CurrentColumn.sun;
		const float reach = CurrentColumn.radius * 1.4143f + b->Radius;
		Current->boundRadiusIn += b->Radius;
		if (D3DXVec3LengthSq(&perp) <= reach * reach) return;
		Current->refsOutside++;
		if (ExamplesLogged >= 12) return;
		ExamplesLogged++;
		const NiFrustumPlanes* p = CurrentColumn.planes;
		char planes[384] = ""; size_t used = 0;
		for (UInt32 i = 0; p && i < NiFrustumPlanes::MaxPlanes; i++) {
			const NiPlane& pl = p->CullingPlanes[i];
			const float d = pl.Normal.x * b->Center.x + pl.Normal.y * b->Center.y + pl.Normal.z * b->Center.z - pl.Constant;
			const int w = _snprintf_s(planes + used, sizeof(planes) - used, _TRUNCATE, " p%u%s n(%.3f %.3f %.3f) c %.0f d %.0f;", i, p->IsPlaneActive(i) ? "" : "(off)",
				pl.Normal.x, pl.Normal.y, pl.Normal.z, pl.Constant, d);
			if (w < 0) break; used += w;
		}
		Logger::Log("SUN CASCADE CHECK: cascade %d passed ref %08X (type %u) bound (%.0f %.0f %.0f) r %.0f: off the column axis by %.0f, reach %.0f "
			"(column centre %.0f %.0f %.0f, radius %.0f, sun %.3f %.3f %.3f); planes:%s", (int)(Current - Sum), ref ? ref->refID : 0, ref && ref->baseForm ? (unsigned)ref->baseForm->formType : 0u,
			b->Center.x, b->Center.y, b->Center.z, b->Radius, sqrtf(D3DXVec3LengthSq(&perp)), reach, CurrentColumn.center.x, CurrentColumn.center.y, CurrentColumn.center.z,
			CurrentColumn.radius, CurrentColumn.sun.x, CurrentColumn.sun.y, CurrentColumn.sun.z, planes);
	}

	inline unsigned long long Now() { return __rdtsc(); }

	inline void BeginPass(int pass) {
		CurrentPass = pass;
		LastVB = LastIB = LastDecl = LastTex = nullptr; LastCull = ~0u;
	}

	inline void Reset() {
		memset(Sum, 0, sizeof(Sum));
		tLoop = 0; tSharedWalk = 0; Frames = 0;
		WindowTsc = Now(); WindowMs = CpuTimer::NowMs();
	}

	inline void Flush(const char* label) {
		if (!Frames) { Reset(); return; }
		const double ms = CpuTimer::NowMs() - WindowMs;
		const double tscPerMs = ms > 0.0 ? (double)(Now() - WindowTsc) / ms : 1.0;
		const double f = 1.0 / Frames;
		auto t = [&](unsigned long long ticks) { return ticks / tscPerMs * f; };
		double walk = 0.0, draw = 0.0;
		for (int c = 0; c < 4; c++) { walk += t(Sum[c].tWalkLod + Sum[c].tWalkCells); draw += t(Sum[c].tClear + Sum[c].tDraws); }
		walk += t(tSharedWalk);
		Logger::Log("SUN CASCADE STATS %s, %u frames: cascade loop %.3f ms/frame = walk %.3f (one walk for all %.3f) + draws %.3f + other %.3f",
			label, Frames, t(tLoop), walk, t(tSharedWalk), draw, t(tLoop) - walk - draw);
		static const char* names[4] = { "near", "middle", "far", "lod" };
		for (int c = 0; c < 4; c++) {
			const Cascade& s = Sum[c];
			if (!s.frames) { Logger::Log("  %-6s not drawn", names[c]); continue; }
			if (!s.tWalkCells && !s.tDraws) { Logger::Log("  %-6s drawn %3.0f%%", names[c], 100.0 * s.frames * f); continue; }
			const double d = 1.0 / s.frames;
			auto per = [&](unsigned long long v) { return v * d; };
			auto tc = [&](unsigned long long ticks) { return ticks / tscPerMs * d; };
			const unsigned long long draws = s.draws[kOpaque] + s.draws[kAlpha] + s.draws[kSkinned] + s.draws[kTree] + s.draws[kLodLand];
			if (s.boundRadiusIn > 0.0) Logger::Log("  %-6s sphere radius %.0f (last draw); references that passed: bound radius %.0f on average, %.1f per draw from outside the column (mode 3)",
				names[c], s.radius, s.refsIn ? s.boundRadiusIn / s.refsIn : 0.0, per(s.refsOutside));
			Logger::Log("  %-6s drawn %3.0f%% | per draw of it: walk %.3f (lod %.3f cells %.3f) clear %.3f draws %.3f (constants %.3f setct %.3f wind %.3f render %.3f) ms"
				" | refs %.0f with node %.0f in %.0f, nodes %.0f, child tests %.0f, geometry %.0f | casters %.0f (opaque %.0f alpha %.0f skinned %.0f [partitions %.0f] tree %.0f lodland %.0f)"
				" | changes vb %.0f ib %.0f decl %.0f cull %.0f tex %.0f",
				names[c], 100.0 * s.frames * f, tc(s.tWalkLod + s.tWalkCells), tc(s.tWalkLod), tc(s.tWalkCells), tc(s.tClear), tc(s.tDraws),
				tc(s.tUpdate), tc(s.tSetCT), tc(s.tWind), tc(s.tRender),
				per(s.refs), per(s.refNodes), per(s.refsIn), per(s.nodes), per(s.childTests), per(s.geometry),
				per(draws), per(s.draws[kOpaque]), per(s.draws[kAlpha]), per(s.draws[kSkinned]), per(s.partitions), per(s.draws[kTree]), per(s.draws[kLodLand]),
				per(s.vbChanges), per(s.ibChanges), per(s.declChanges), per(s.cullChanges), per(s.texChanges));
		}
		Reset();
	}

	inline bool Counting = false;

	inline void BeginFrame() {
		if (SettingShots::Running) {
			if (SettingShots::Index != ShotSeen) {
				if (ShotWindow && ShotSeen < SettingShots::List.size()) {
					char label[96]; sprintf_s(label, "shot '%s'", SettingShots::List[ShotSeen].slug.c_str());
					Flush(label);
				}
				ShotSeen = SettingShots::Index; ShotWindow = false; Reset();
			}
			if (!ShotWindow && SettingShots::Frame >= 60) { ShotWindow = true; Reset(); }
			Counting = ShotWindow;
			return;
		}
		if (ShotWindow) { ShotWindow = false; ShotSeen = (size_t)-1; Reset(); }
		if (!WindowTsc) Reset();
		Counting = true;
	}

	inline void EndFrame(unsigned long long loopTicks) {
		if (!Counting) { Reset(); return; }
		tLoop += loopTicks; Frames++;
		if (!SettingShots::Running && Frames >= 240) Flush("240 frames");
	}

	inline void NoteCaster(NiGeometry* Geo) {
		Cascade* s = Current;
		if (CurrentPass >= 0 && CurrentPass < kPasses) s->draws[CurrentPass]++;
		NiGeometryData* data = Geo->geomData;
		NiGeometryBufferData* buffers = data ? data->m_pkBuffData : nullptr;
		if (buffers && CurrentPass != kSkinned) {
			const void* vb = buffers->StreamCount && buffers->VBChip[0] ? (const void*)buffers->VBChip[0]->VB : nullptr;
			const void* ib = buffers->IB;
			const void* decl = buffers->FVF ? (const void*)(uintptr_t)buffers->FVF : (const void*)buffers->VertexDeclaration;
			if (vb != LastVB) s->vbChanges++;
			if (ib != LastIB) s->ibChanges++;
			if (decl != LastDecl) s->declChanges++;
			LastVB = vb; LastIB = ib; LastDecl = decl;
		}
		NiStencilProperty* stencil = (NiStencilProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Stencil);
		const UInt32 cull = stencil ? (UInt32)stencil->GetDrawMode() : 0xFFFFu;
		if (cull != LastCull) s->cullChanges++;
		LastCull = cull;
		if (CurrentPass == kAlpha) {
			BSShaderProperty* shade = (BSShaderProperty*)Geo->GetProperty(NiProperty::PropertyType::kType_Shade);
			NiSourceTexture** textures = shade ? ((BSShaderPPLightingProperty*)shade)->ppTextures[0] : nullptr;
			const void* tex = textures ? (const void*)*textures : nullptr;
			if (tex != LastTex) s->texChanges++;
			LastTex = tex;
		}
	}
#endif
}
