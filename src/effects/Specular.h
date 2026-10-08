#pragma once

class SpecularEffect : public EffectRecord
{
public:
	SpecularEffect() : EffectRecord("Specular") {};

	struct SpecularStruct {
		D3DXVECTOR4		EffectStrength;
		D3DXVECTOR4		Data;
	};
	SpecularStruct	Constants;

	struct LookStruct {
		float SunStrength;
		float Roughness;
		float SkyReflections;
		float DistanceFade;
		float SpecularAA;
		float Reflectance;
	};
	struct SettingsSpecularStruct {
		LookStruct Exterior;
		LookStruct Rain;
	};
	SettingsSpecularStruct Settings;

	struct TexturesStruct {
		IDirect3DTexture9* NormalsTexture = nullptr;
		IDirect3DSurface9* NormalsSurface = nullptr;
		IDirect3DTexture9* LightTexture = nullptr;
		IDirect3DSurface9* LightSurface = nullptr;
	};
	TexturesStruct	Textures;
	CachedHandle techBounce, paramBounceData, paramBounceLayout;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();

	bool	CanTakeBounce();
	bool	RenderWithBounce(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		IDirect3DTexture9* bounce, const D3DXVECTOR4& data, const D3DXVECTOR4& layout);
};
