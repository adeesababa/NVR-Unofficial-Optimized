#pragma once

class SkyViewEffect : public EffectRecord
{
public:
	SkyViewEffect() : EffectRecord("SkyView") {};

	struct TexturesStruct {
		IDirect3DTexture9* TableTexture = nullptr;
		IDirect3DSurface9* TableSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants() {};
	void	RegisterTextures();
	bool	ShouldRender();
	void	RenderTable(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
};
