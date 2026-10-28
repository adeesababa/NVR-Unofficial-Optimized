#pragma once


#include <cmath>

struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct _D3DVIEWPORT9;

namespace RenderRect {
	inline bool		On = false;
	inline float	ScaleX = 1.0f;
	inline float	ScaleY = 1.0f;
	inline float	LastScaleX = 1.0f;
	inline float	LastScaleY = 1.0f;

	inline bool Active() { return On; }
	inline unsigned Width(unsigned w) { return On ? (unsigned)floorf((float)w * ScaleX + 0.5f) : w; }
	inline unsigned Height(unsigned h) { return On ? (unsigned)floorf((float)h * ScaleY + 0.5f) : h; }
	inline bool ScaleChanged() { return On && (ScaleX != LastScaleX || ScaleY != LastScaleY); }

	inline void Begin(float sx, float sy) { On = true; ScaleX = sx; ScaleY = sy; }
	inline void End() { if (On) { LastScaleX = ScaleX; LastScaleY = ScaleY; } On = false; ScaleX = ScaleY = 1.0f; }

	struct Suspend {
		bool was;
		Suspend() : was(On) { On = false; }
		~Suspend() { On = was; }
	};

	bool ScreenShaped(unsigned w, unsigned h);
	void ApplyViewport(IDirect3DDevice9* Device);
	void WholeViewport(IDirect3DDevice9* Device, _D3DVIEWPORT9& Viewport);
	void RescaleAfterUpscale(IDirect3DSurface9* Surface);
	void RescaleRegistered(IDirect3DDevice9* Device);
}
