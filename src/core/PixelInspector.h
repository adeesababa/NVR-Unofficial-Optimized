#pragma once


class EffectRecord;

namespace PixelInspector {
	enum Kind { Color = 0, Depth = 1, Normal = 2, Buffer = 3 };

	extern bool Active;

	void Capture(const char* Label, IUnknown* Resource, Kind kind = Color);
	inline void CaptureIf(const char* Label, IUnknown* Resource, Kind kind = Color) { if (Active && Resource) Capture(Label, Resource, kind); }

	void AfterFrame(EffectRecord* Effect);
	void AfterOwnTarget(EffectRecord* Effect, IDirect3DSurface9* Target, UINT Technique);
	extern int InOwnTarget;
	struct OwnTargetScope {
		EffectRecord* effect; IDirect3DSurface9* target; UINT technique;
		OwnTargetScope(EffectRecord* e, IDirect3DSurface9* t, UINT tech) : effect(e), target(t), technique(tech) { InOwnTarget++; }
		~OwnTargetScope() { InOwnTarget--; if (Active) AfterOwnTarget(effect, target, technique); }
	};
	struct EffectScope {
		EffectRecord* effect; IDirect3DSurface9* target; IDirect3DSurface9* rendered; UINT technique;
		EffectScope(EffectRecord* e, IDirect3DSurface9* t, IDirect3DSurface9* r, UINT tech) : effect(e), target(t), rendered(r), technique(tech) {}
		~EffectScope() { if (Active && !InOwnTarget && target && target == rendered) AfterOwnTarget(effect, target, technique); }
	};

	void EndFrame();

	bool PixelFor(UINT Width, UINT Height, int& x, int& y);

	void DrawUI();
}
