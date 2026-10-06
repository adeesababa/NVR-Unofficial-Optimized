#pragma once

class BounceLightEffect : public EffectRecord
{
public:
	BounceLightEffect() : EffectRecord("BounceLight") {};

	struct BounceLightStruct {
		D3DXVECTOR4		Data;
	};
	BounceLightStruct	Constants;
	bool				quarterResolution = false;
	IDirect3DTexture9*	halfTexture[2] = {};
	IDirect3DSurface9*	halfSurface[2] = {};
	IDirect3DTexture9*	quarterTexture[2] = {};
	IDirect3DSurface9*	quarterSurface[2] = {};
	IDirect3DTexture9*	halfPrepTexture = nullptr;
	IDirect3DSurface9*	halfPrepSurface = nullptr;
	IDirect3DTexture9*	quarterPrepTexture = nullptr;
	IDirect3DSurface9*	quarterPrepSurface = nullptr;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	EnsureBuffers(bool quarter);
	bool	halfTried = false;
	bool	quarterTried = false;
	void	UpdateSettings();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
