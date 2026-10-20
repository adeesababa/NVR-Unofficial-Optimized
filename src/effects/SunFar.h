#pragma once

class SunFarEffect : public EffectRecord
{
public:
	SunFarEffect() : EffectRecord("SunFar") {};

	struct SettingsStruct {
		bool	Clouds;
		float	CloudStrength;
		float	CloudDensity;
		float	CloudHeight;
		float	CloudTile;
		float	CloudSpeed;
		int		DebugView;
	};
	SettingsStruct	Settings = {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Clouds;
		D3DXVECTOR4	CloudOffset[2];
		D3DXVECTOR4	CloudWeight;
		D3DXVECTOR4	CloudBlend;
		D3DXVECTOR4	Apply;
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	Texture = nullptr;
	IDirect3DSurface9*	Surface = nullptr;

	void	UpdateSettings();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateConstants();
	bool	ShouldRender();
	void	RenderFactor(IDirect3DDevice9* Device);
	IDirect3DTexture9*	FactorTexture() const { return Constants.Apply.x > 0.5f ? Texture : nullptr; }

private:
	IDirect3DBaseTexture9*	layerTextures[4] = {};
	IDirect3DBaseTexture9*	blendTextures[4] = {};
	double	drift[4][2] = {};
	double	lastDays = -1.0;
	bool	cloudsReady = false;
};
