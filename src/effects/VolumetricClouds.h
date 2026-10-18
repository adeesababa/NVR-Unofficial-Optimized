#pragma once
#include <string>
#include <unordered_map>
#include <vector>

class VolumetricCloudsEffect : public EffectRecord
{
public:
	VolumetricCloudsEffect() : EffectRecord("VolumetricClouds") {};

	struct SettingsStruct {
		float	Base, Thickness;
		float	Coverage[4];
		float	TypeBias[4];
		float	Density;
		float	ShapeSize, DetailSize, WeatherSize;
		float	Erosion;
		float	SunStrength, AmbientStrength, Forward;
		int		Steps;
		float	MaxDistance, HazeDistance;
		float	WindSpeed, Evolution;
		float	History;
		float	Brightness;
		float	EdgeSharpness, ShapeSmoothing, Turbulence, DetailSmoothness;
		bool	HideGameClouds;
		bool	GameTime;
		float	Shadows;
		float	ShadowRange;
		float	WaterReflections;
		bool	ShadowDebug;
		int		ReflectionDebug;
		float	FineDetail;
		float	FineDetailSize;
		float	Sharpness;
		float	FromWeatherTextures;
		bool	FixedMarchExit;
	};
	SettingsStruct	Settings = {};

	struct ConstantsStruct {
		D3DXVECTOR4	Shape;
		D3DXVECTOR4	Noise;
		D3DXVECTOR4	Light;
		D3DXVECTOR4	Wind;
		D3DXVECTOR4	March;
		D3DXVECTOR4	Layout;
		D3DXVECTOR4	Blocks;
		D3DXVECTOR4	Hide;
		D3DXVECTOR4	Look;
		D3DXMATRIX	PrevViewProj;
		D3DXVECTOR4	Extra;
		D3DXMATRIX	ReflectInvVP;
		D3DXMATRIX	HistoryViewProj;
		D3DXVECTOR4	ReflectData;
		D3DXVECTOR4	Detail;
		D3DXVECTOR4	Motion;
	};
	ConstantsStruct	Constants = {};

	struct TexturesStruct {
		IDirect3DTexture9* CurrentTexture = nullptr;
		IDirect3DSurface9* CurrentSurface = nullptr;
		IDirect3DTexture9* HistoryTexture[2] = {};
		IDirect3DSurface9* HistorySurface[2] = {};
		IDirect3DTexture9* HistoryRead = nullptr;
		IDirect3DTexture9* ShadowTexture = nullptr;
		IDirect3DSurface9* ShadowSurface = nullptr;
		IDirect3DTexture9* ProbeTexture = nullptr;
		IDirect3DSurface9* ProbeSurface = nullptr;
	};
	TexturesStruct	Textures;
	UINT	shadowMapSize = 256;

	float	HideGameClouds = 0.0f;
	float	HideGameCloudsInReflection = 0.0f;
	void	CaptureReflection(IDirect3DDevice9* Device);
	void	DrawIntoReflection(IDirect3DDevice9* Device);
	void	BeforeResolve(IDirect3DDevice9* Device, IDirect3DSurface9* source);
	void	EndReflectionPass(IDirect3DDevice9* Device);

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	RenderClouds(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		IDirect3DSurface9* SourceSurface);

private:
	float		coverage = -1.0f, rain = 0.0f, typeBias = 0.0f;
	D3DXVECTOR3	wind = D3DXVECTOR3(0, 0, 0);
	double		windX = 0.0, windY = 0.0, windZ = 0.0;
	D3DXMATRIX	lastViewProj;
	UINT		frame = 0, read = 0;
	double		lastRenderTime = -1.0;
	UINT		lowWidth = 0, lowHeight = 0, blockWidth = 0, blockHeight = 0;
	double		lastCloudTime = -1.0;
	IDirect3DSurface9*	reflectTarget = nullptr;
	IDirect3DSurface9*	reflectDepth = nullptr;
	D3DXMATRIX	reflectView, reflectProj;
	IDirect3DStateBlock9*	reflectState = nullptr;
	bool		reflectLogged = false;
	bool		reflectNoted = false;
	bool		ReflectionWanted() const;
	std::unordered_map<TESWeather*, float>	weatherFill;
	std::unordered_map<std::string, float>	textureFill;
	TESWeather*	lastWeather = nullptr;
	int			learnTries = 0;
	float		snapWait = -1.0f;
	void		LearnWeatherFill(Sky* sky);
	struct ProbeJob { std::string path; IDirect3DBaseTexture9* texture; float lod; UINT width, height; };
	std::vector<ProbeJob>	probeQueue;
	IDirect3DSurface9*	probeReadback = nullptr;
	void		ProbeLayer(IDirect3DDevice9* Device);
	void		WeatherLook(TESWeather* weather, float& coverage, float& rain, float& bias);
};
