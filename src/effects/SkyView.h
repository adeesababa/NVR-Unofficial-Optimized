#pragma once

class SkyViewEffect : public EffectRecord
{
public:
	SkyViewEffect() : EffectRecord("SkyView") {};

	struct TexturesStruct {
		IDirect3DTexture9* TableTexture = nullptr;
		IDirect3DSurface9* TableSurface = nullptr;
		IDirect3DTexture9* TransTexture = nullptr;
		IDirect3DSurface9* TransSurface = nullptr;
		IDirect3DTexture9* MSTexture = nullptr;
		IDirect3DSurface9* MSSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants() {};
	void	RegisterTextures();
	bool	ShouldRender();
	void	RenderTable(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);

private:
	bool	transBuilt = false, msBuilt = false;
	float	builtHaze = -1.0f, builtAlbedo = -1.0f;
};
