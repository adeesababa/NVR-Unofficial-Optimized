#pragma once

class ExposureEffect : public EffectRecord
{
public:
	ExposureEffect() : EffectRecord("Exposure") {};

	struct ValuesStruct {
		float MinBrightness;
		float MaxBrightness;
		float DarkAdaptSpeed;
		float LightAdaptSpeed;
	};

	struct ExposureSettingsStruct {
		ValuesStruct Main;
		ValuesStruct Night;
		ValuesStruct Interiors;
		bool	Histogram;
		float	HistogramLowPercent;
		float	HistogramHighPercent;
	};
	ExposureSettingsStruct	Settings;

	struct ExposureStruct {
		D3DXVECTOR4		Data;
	};
	ExposureStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;

};