#pragma once

class SunShadowsEffect : public EffectRecord
{
public:

	SunShadowsEffect() : EffectRecord("SunShadows") {};

	struct SunShadowStruct {
	};
	SunShadowStruct	Constants;

	IDirect3DTexture9* scratchTexture[2] = {};
	IDirect3DSurface9* scratchSurface[2] = {};
	bool pingPongFailed = false;

	void	SetCT();
	void	RegisterTextures();

	void	UpdateConstants() {};
	void	RegisterConstants() {};
	void	UpdateSettings() {};

	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
};
