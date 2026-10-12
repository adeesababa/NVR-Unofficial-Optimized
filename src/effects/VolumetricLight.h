#pragma once

class VolumetricLightEffect : public EffectRecord
{
public:
	VolumetricLightEffect() : EffectRecord("VolumetricLight") {};

	struct SettingsStruct {
		float	Strength;
		float	Darken;
		float	Density;
		float	MaxDistance;
		int		Steps;
		float	Anisotropy;
		float	NightStrength;
		float	RainStrength;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Data2;
		D3DXVECTOR4	Layout;
	};
	ConstantsStruct	Constants;

	struct TexturesStruct {
		IDirect3DTexture9* MarchTexture = nullptr;
		IDirect3DSurface9* MarchSurface = nullptr;
		IDirect3DTexture9* BlurTexture = nullptr;
		IDirect3DSurface9* BlurSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
};
