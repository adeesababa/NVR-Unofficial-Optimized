#pragma once

class ImageAdjustEffect : public EffectRecord
{
public:
	ImageAdjustEffect() : EffectRecord("ImageAdjust") {};

	struct ValuesStruct {
		float Brightness;
		float Contrast;
		float Saturation;
		float Strength;
		float DarkColorR;
		float DarkColorG;
		float DarkColorB;
		float LightColorR;
		float LightColorG;
		float LightColorB;
	};

	struct ImageAdjustSettingsStruct {
		ValuesStruct Main;
		ValuesStruct Night;
		ValuesStruct Interiors;
	};
	ImageAdjustSettingsStruct Settings;

	struct ImageAdjustStruct {
		D3DXVECTOR4		Data;
		D3DXVECTOR4		DarkColor;
		D3DXVECTOR4		LightColor;
	};
	ImageAdjustStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender() override;

	// UNOFFICIAL 2026-10-23 (NVR DXVK): the pass as src\hlsl\NewVegas\Compute\ImageAdjust.frag (EffectRecord::ExtRender)
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;

};
