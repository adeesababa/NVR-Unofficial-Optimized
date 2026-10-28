#pragma once

class LensEffect : public EffectRecord
{
public:
	LensEffect() : EffectRecord("Lens") {};

	struct LensSettingsStruct {
		float bloomExponent;
		float strength;
		float smudginess;
	};

	struct LensSettings {
		LensSettingsStruct Main;
		LensSettingsStruct Interiors;
		LensSettingsStruct Night;
	};
	LensSettings	Settings;

	struct LensStruct {
		D3DXVECTOR4		Data;
	};
	LensStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	bool	ShouldRender();
};