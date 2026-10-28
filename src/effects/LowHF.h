#pragma once

class LowHFEffect : public EffectRecord
{
public:
	LowHFEffect() : EffectRecord("LowHF") {};

	struct LowHFSettingsStruct {
		float healthLimit;
		float FatigueLimit;
		float LumaMultiplier;
		float BlurMultiplier;
		float VignetteMultiplier;
		float DarknessMultiplier;
	};
	LowHFSettingsStruct Settings;

	struct LowHFStruct {
		D3DXVECTOR4		Data;
		float			HealthCoeff;
		float			FatigueCoeff;
	};
	LowHFStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
	bool	ShouldRender();
};
