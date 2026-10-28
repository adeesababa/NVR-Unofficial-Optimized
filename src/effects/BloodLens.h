#pragma once

class BloodLensEffect : public EffectRecord
{
public:
	BloodLensEffect() : EffectRecord("BloodLens") {
		Constants.Percent = 0.0f;
	};

	struct BloodLensStruct {
		D3DXVECTOR4		Params;
		D3DXVECTOR4		BloodColor;
		D3DXVECTOR4		Time;
		float			Percent;
	};
	BloodLensStruct	Constants;

	float effectTime;
	float intensity = 0.0f;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	bool	ShouldRender();
};