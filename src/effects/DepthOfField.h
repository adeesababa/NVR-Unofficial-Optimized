#pragma once

class DepthOfFieldEffect : public EffectRecord
{
public:
	DepthOfFieldEffect() : EffectRecord("DepthOfField") {};

	struct ValuesStruct {
		float DistantBlur;
		float DistantBlurStartRange;
		float DistantBlurEndRange;
		float BaseBlurRadius;
		float focusDistance;
		float Radius;
		float DiameterRange;
		float NearBlurCutOff;
		int Mode;
		bool Enabled;
	};

	struct DepthOfFieldSettingsStruct {
		ValuesStruct	FirstPerson;
		ValuesStruct	ThirdPerson;
		ValuesStruct	VanityView;
	};
	DepthOfFieldSettingsStruct Settings;

	struct DepthOfFieldStruct {
		bool			Enabled;
		D3DXVECTOR4		Blur;
		D3DXVECTOR4		Data;
	};
	DepthOfFieldStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	// UNOFFICIAL 2026-10-25 (NVR DXVK): technique DistantBlurOnly as src\hlsl\NewVegas\Compute\DepthOfField.frag (EffectRecord::ExtRender)
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
};