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
	ID3D9NvrExtPipeline*	extPipeline = nullptr;
	bool					extTried = false;

};