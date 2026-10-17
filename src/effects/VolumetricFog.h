#pragma once

class VolumetricFogEffect : public EffectRecord
{
public:
	VolumetricFogEffect() : EffectRecord("VolumetricFog") {};

	struct VolumetricFogStruct {
		D3DXVECTOR4		LowFog;
		D3DXVECTOR4		HighFog;
		D3DXVECTOR4		SimpleFog;
		D3DXVECTOR4		Blend;
		D3DXVECTOR4		Height;
		D3DXVECTOR4		Data;
	};
	VolumetricFogStruct	Constants;

	float	Amount;
	float	AmountInteriors;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();

	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	bool	CanComposite();
	bool	compositeShadow = false;
	bool	compositeAO = false;
	bool	compositeApplied = false;
	bool	compositeFailed = false;
	IDirect3DTexture9* compositeAOTexture = nullptr;
};
