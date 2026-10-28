#pragma once

namespace FoldFog {

	constexpr UInt32 kEnabledPasses = 0x011F91D8;
	constexpr UInt32 kRebuildBit = 0x20000000;
	inline bool On = false, Applied = false;

	constexpr unsigned kSlots = 8192;
	inline const void* Key[kSlots] = {};
	inline unsigned Frame[kSlots] = {};
	inline unsigned FrameId = 1;
	inline unsigned long long Folded = 0, Skipped = 0, Frames = 0;

	struct PassArrayView {
		void* vtbl;
		PointShadowForward::RenderPassView** base;
		UInt16 maxSize, size, eSize, growBy;
		UInt32 count;
	};
	static_assert(offsetof(PassArrayView, count) == 0x10, "RenderPassArray layout");

	inline bool IsEnv(UInt16 e) { return e >= 0x248 && e <= 0x24B; }
	inline bool IsFog(UInt16 e) { return e == 0x252 || e == 0x254; }

	inline bool Find(const void* geometry, bool insert) {
		unsigned h = (unsigned)(((uintptr_t)geometry >> 4) * 2654435761u) & (kSlots - 1);
		for (unsigned probe = 0; probe < 64; probe++, h = (h + 1) & (kSlots - 1)) {
			if (Frame[h] == FrameId && Key[h] == geometry) return true;
			if (Frame[h] != FrameId) {
				if (insert) { Key[h] = geometry; Frame[h] = FrameId; }
				return insert;
			}
		}
		return false;
	}

	inline void BeginFrame() {
		const bool want = TheSettingManager->GetSettingI("Main.Main.Misc", "FoldFog") != 0;
		if (want != Applied) {
			if (Applied) *(UInt32*)kEnabledPasses ^= kRebuildBit;
			Applied = want;
			Logger::Log("UNOFFICIAL FoldFog: %s", want ? "on (opaque env-mapped objects fog in their lit pass; their fog pass is not drawn)" :
				"off (env-mapped objects drawn with their fog pass, as the game does; every object's passes rebuilt)");
		}
		On = want;
		FrameId++;
		if (!GpuTimer::Enabled) { Folded = Skipped = Frames = 0; return; }
		if (On && ++Frames >= 600) {
			Logger::Log("UNOFFICIAL FoldFog (F10): per frame %.1f lit passes fogged in their shader, %.1f fog passes not drawn", Folded / (double)Frames, Skipped / (double)Frames);
			Folded = Skipped = Frames = 0;
		}
	}

	inline bool Skip(void* passPtr, bool world) {
		if (!On || !world || !passPtr) return false;
		PointShadowForward::RenderPassView* pass = (PointShadowForward::RenderPassView*)passPtr;
		if (IsFog(pass->passEnum)) {
			if (!Find(pass->geometry, false)) return false;
			Skipped++;
			return true;
		}
		const char* name = Pointers::Functions::GetPassDescription(pass->passEnum);
		if (!name || strncmp(name, "BSSM_ADT", 8) || !pass->geometry) return false;
		const BSShaderProperty* property = (const BSShaderProperty*)pass->geometry->propertyState.m_spShadeProperty;
		if (!property || property->fAlpha < 1.0f || property->fFadeAlpha < 1.0f) return false;
		const NiAlphaProperty* alpha = pass->geometry->propertyState.m_spAlphaProperty;
		if (alpha && (alpha->flags & 1)) return false;
		const PassArrayView* passes = (const PassArrayView*)property->pRenderPassArray;
		if (!passes || !passes->base) return false;
		for (UInt32 i = 0; i + 2 < passes->count; i++) {
			if (passes->base[i] != pass) continue;
			const PointShadowForward::RenderPassView* env = passes->base[i + 1];
			const PointShadowForward::RenderPassView* fog = passes->base[i + 2];
			if (!env || !fog || !IsEnv(env->passEnum) || !IsFog(fog->passEnum)) return false;
			pass->unk08 = 1;
			Find(pass->geometry, true);
			Folded++;
			return false;
		}
		return false;
	}
}
