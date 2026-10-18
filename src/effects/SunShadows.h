#pragma once

class SunShadowsEffect : public EffectRecord
{
public:

	SunShadowsEffect() : EffectRecord("SunShadows") {};

	struct SunShadowStruct {
	};
	SunShadowStruct	Constants;

	// Scratch targets so no pass samples TESR_PointShadowBuffer while also rendering into it.
	IDirect3DTexture9* scratchTexture[2] = {};
	IDirect3DSurface9* scratchSurface[2] = {};
	bool pingPongFailed = false;

	void	SetCT();
	void	RegisterTextures();

	void	UpdateConstants() {};
	void	RegisterConstants() {};
	void	UpdateSettings();

	// UNOFFICIAL 2026-10-17 FastPaths ([Shaders.ShadowsExteriors.ScreenSpace] FastPaths): techniques 3 and 4 of SunShadows.fx are
	// techniques 0 and 1 (ForwardContactShadows) with the march's early returns (contact shadows off, farther than the render
	// distance, turned away from the sun) made real: the effect compiler had flattened them, so every pixel ran the march. Same picture.
	bool	fastPaths = false;

	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
};
