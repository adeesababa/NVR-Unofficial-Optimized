#pragma once

class SandstormEffect : public EffectRecord
{
public:
	SandstormEffect() : EffectRecord("Sandstorm") {
		memset(&Constants, 0, sizeof(Constants));
		Constants.LeafStorm = D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f);
	};

	struct SandstormSettingsStruct {
		char	Keyword[128];
		float	Visibility;
		float	HeightFalloff;
		float	Billowing;
		float	NoiseScale;
		float	WindSpeed;
		float	MinWindSpeed;
		float	StreakStrength;
		float	DustColor[3];
		float	SunScattering;
		float	Brightness;
		float	FadeSpeed;
		float	GrassWind;
		float	TreeWind;
		float	LightScattering;
		float	EmissiveThreshold;
		float	EmissiveDistance;
		float	Steps;
		float	Octaves;
		bool	SoftEdges;
		bool	FineShelter;
		bool	ThroughBlended;
		bool	ForceStorm;
		bool	FastPath;
		float	FixedTime;
	};
	SandstormSettingsStruct	Settings;

	struct SandstormStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Wind;
		D3DXVECTOR4	Shape;
		D3DXVECTOR4	Color;
		D3DXVECTOR4	Cover;
		D3DXVECTOR4	Lights;
		D3DXVECTOR4	Emissive;
		D3DXVECTOR4	Water;
		D3DXVECTOR4	Quality;
		D3DXVECTOR4	LeafStorm;
		D3DXVECTOR4	GridLayout;
		D3DXVECTOR4	LowLayout;
	};
	SandstormStruct	Constants;

	float	GrassStorm[2] = { 0.0f, 0.0f };

	float	Intensity = 0.0f;
	double	Time = 0.0;
	unsigned int LastFrame = 0;
	void*	LastWeather = nullptr;
	void*	LastSecond = nullptr;
	bool	FirstIsStorm = false;
	bool	SecondIsStorm = false;
	void*	LoggedWeather = nullptr;
	void*	LoggedSecond = nullptr;

	bool	HidesBlendedDepth() { return Enabled && Effect && Settings.ThroughBlended && ShouldRender(); }
	bool	Underwater = false;

	struct TexturesStruct {
		IDirect3DTexture9*	GridTexture = nullptr;
		IDirect3DSurface9*	GridSurface = nullptr;
		IDirect3DTexture9*	GridTempTexture = nullptr;
		IDirect3DSurface9*	GridTempSurface = nullptr;
		IDirect3DTexture9*	StreaksTexture = nullptr;
		IDirect3DSurface9*	StreaksSurface = nullptr;
		IDirect3DTexture9*	GlowTexture = nullptr;
		IDirect3DSurface9*	GlowSurface = nullptr;
	};
	TexturesStruct	Textures;
	bool	TexturesTried = false;
	int		TexturesFailed = 0;
	bool	UseFastPath();
	void	RenderBuffers(IDirect3DDevice9* Device);
	void	RegisterTextures();
	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	bool	IsSandstorm(TESWeather* Weather);
	void	ClearWind();
};
