#pragma once

class CinemaEffect : public EffectRecord
{
public:
	CinemaEffect() : EffectRecord("Cinema") {};

	struct CinemaStruct {
		D3DXVECTOR4		Data;
		D3DXVECTOR4		Settings;
	};

	float	Mode;
	float	aspectRatio;
	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();


	CinemaStruct	Constants;

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;
};