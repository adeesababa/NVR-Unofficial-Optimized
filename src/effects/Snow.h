#pragma once

class SnowEffect : public EffectRecord
{
public:
	SnowEffect() : EffectRecord("Snow") {
		Constants.SnowAnimator.Initialize(0);
	};

	struct SnowStruct {
		Animator		SnowAnimator;
		D3DXVECTOR4		Data;
	};
	SnowStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;

};