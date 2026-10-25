#pragma once

class BloomLegacyEffect : public EffectRecord
{
public:
	BloomLegacyEffect() : EffectRecord("BloomLegacy") {};

	struct ValuesStruct {
		float Luminance;
		float MiddleGray;
		float WhiteCutOff;
		float BloomIntensity;
		float OriginalIntensity;
		float BloomSaturation;
		float OriginalSaturation;
	};
	struct BloomLegacySettingsStruct {
		ValuesStruct Main;
		ValuesStruct Interiors;
	};
	BloomLegacySettingsStruct Settings;

	struct BloomLegacyStruct {
		D3DXVECTOR4		Data;
		D3DXVECTOR4		Values;
	};

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();


	// UNOFFICIAL 2026-10-25 (NVR DXVK): the four passes as src\hlsl\NewVegas\Compute\BloomLegacy*.frag (EffectRecord::ExtRender)
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extBloom, extBlur, extCombine;

	BloomLegacyStruct	Constants;
};