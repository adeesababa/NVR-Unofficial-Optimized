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
	};
	ExposureSettingsStruct	Settings;

	// UNOFFICIAL 2026-10-26 (NVR DXVK, fork-only): [Shaders.Exposure.Histogram]. Histogram on: the frame's average luma comes from a
	// luminance histogram (compute, atomics; src\hlsl\NewVegas\Compute\AvgLumaHistogram.comp) with the darkest LowPercent and the
	// brightest HighPercent of the pixels left out. Read by AvgLumaEffect::RenderCompute; nothing changes without the NVR DXVK extension.
	struct HistogramStruct {
		bool	Histogram;
		float	LowPercent;
		float	HighPercent;
	};
	HistogramStruct		Histogram = {};

	struct ExposureStruct {
		D3DXVECTOR4		Data;
	};
	ExposureStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();

	// UNOFFICIAL 2026-10-23 (NVR DXVK): the pass as src\hlsl\NewVegas\Compute\Exposure.frag (EffectRecord::ExtRender)
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;

};