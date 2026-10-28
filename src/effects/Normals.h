#pragma once

class NormalsEffect : public EffectRecord
{
public:
	NormalsEffect() : EffectRecord("Normals") {};

	struct NormalsStruct {
		D3DXVECTOR4		Data;
	};
	NormalsStruct	Constants;

	struct NormalTextures {
		IDirect3DTexture9* NormalsTexture;
		IDirect3DSurface9* NormalsSurface;
	};
	NormalTextures	Textures;

	void	UpdateConstants();
	void	UpdateSettings() {};
	void	RegisterConstants();
	void	RegisterTextures();

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;

	bool	RenderSmooth(IDirect3DDevice9* Device);
	bool	Smoothed() const { return smoothed; }
	D3DXVECTOR4			SmoothData = D3DXVECTOR4(0, 0, 0, 0);
	IDirect3DTexture9*	SmoothTempTexture = nullptr;
	IDirect3DSurface9*	SmoothTempSurface = nullptr;
private:
	CachedHandle		techSmooth;
	bool				smoothed = false;
	bool				smoothTried = false;
	bool				ExtSmooth(IDirect3DDevice9* Device);
	ExtPass				extPass;
	ExtPass				extPassSmooth;
};
