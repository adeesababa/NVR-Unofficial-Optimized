#pragma once

class SkyDitherEffect : public EffectRecord
{
public:
	SkyDitherEffect() : EffectRecord("SkyDither") {};

	struct SettingsStruct {
		float	Strength;
		bool	SkyOnly;
		int		Pattern;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
	};
	ConstantsStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	bool	ShouldRender() { return Settings.Strength > 0.0f; };

private:
	UINT	frame = 0;
};
