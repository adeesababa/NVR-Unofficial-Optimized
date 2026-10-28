#pragma once

class ColoringEffect : public EffectRecord
{
public:
	ColoringEffect() : EffectRecord("Coloring") {};

	struct ColoringStruct {
		D3DXVECTOR4		ColorCurve;
		D3DXVECTOR4		EffectGamma;
		D3DXVECTOR4		Data;
		D3DXVECTOR4		Values;
	};

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();

	ColoringStruct	Constants;

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extPass;
};