#pragma once

class WaterLensEffect : public EffectRecord
{
public:
	WaterLensEffect() : EffectRecord("WaterLens") {};

	struct WaterLensStruct {
		Animator		WaterLensAnimator;
		D3DXVECTOR4		Data;
	};
	WaterLensStruct	Constants;

	float	amount;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	bool	ShouldRender();
};
