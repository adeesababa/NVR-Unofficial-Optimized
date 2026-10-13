#pragma once

// UNOFFICIAL: volumetric clouds (Effects\VolumetricClouds.fx.hlsl) -- a layer of 3D clouds marched and lit by the sun, in
// place of the game's flat cloud layers. Each frame: a march of one pixel in four of a quarter-resolution grid, a
// temporal pass that keeps the other three from earlier frames, then a full-resolution composite over the sky at the
// start of the HDR chain. Coverage follows the game's weather type; the light follows its sun, moon and sky colours.
// The game's cloud layers are hidden (SKYTEX.pso, TESR_CloudsHide) except in the water's reflection.
class VolumetricCloudsEffect : public EffectRecord
{
public:
	VolumetricCloudsEffect() : EffectRecord("VolumetricClouds") {};

	struct SettingsStruct {
		float	Base, Thickness;				// metres
		float	Coverage[4];				// pleasant, cloudy, rainy, snow
		float	TypeBias[4];				// flat layers (-1) .. tall heaps (+1), the same four
		float	Density;
		float	ShapeSize, DetailSize, WeatherSize;	// metres per tile of each noise texture
		float	Erosion;
		float	SunStrength, AmbientStrength, Forward;
		int		Steps;
		float	MaxDistance, HazeDistance;	// metres
		float	WindSpeed, Evolution;		// wind multiplier, detail drift in metres per second
		float	History;					// how much of the earlier frames each refresh keeps
		float	Brightness;
		float	EdgeSharpness, ShapeSmoothing, Turbulence, DetailSmoothness;
		bool	HideGameClouds;
		bool	GameTime;					// UNOFFICIAL: the wind follows the game's clock (waiting moves the clouds; the same time, the same sky)
		float	Shadows;					// UNOFFICIAL: how much the clouds shade the ground (0 off)
		float	ShadowRange;				// UNOFFICIAL: metres across the cloud shadow map
		float	WaterReflections;			// UNOFFICIAL: the clouds drawn into the game's water reflection (0 off)
		bool	ShadowDebug;				// UNOFFICIAL: show the cloud shadow alone (white lit, black shaded)
		int		ReflectionDebug;			// UNOFFICIAL: in the water reflection, red where it looks up (sky), blue where down
	};
	SettingsStruct	Settings = {};

	struct ConstantsStruct {
		D3DXVECTOR4	Shape;		// x: base altitude (m), y: thickness (m), z: coverage, w: density
		D3DXVECTOR4	Noise;		// x: shape tile (m), y: detail tile (m), z: erosion, w: weather map tile (m)
		D3DXVECTOR4	Light;		// x: sun strength, y: ambient strength, z: forward scattering, w: rain darkening
		D3DXVECTOR4	Wind;		// xy: wind offset (m), z: detail drift (m), w: cloud type bias
		D3DXVECTOR4	March;		// x: steps, y: max distance (m), z: frame number, w: haze distance (m)
		D3DXVECTOR4	Layout;		// xy: one quarter-resolution texel in UV, z: history weight, w: brightness
		D3DXVECTOR4	Blocks;		// xy: one march texel in UV, zw: the block's pixel marched this frame
		D3DXVECTOR4	Hide;		// x: 1 = the game's cloud layers are hidden (set per draw by SetShadersHook)
		D3DXVECTOR4	Look;		// x: edge sharpness, y: shape noise mip bias, z: turbulence, w: smooth share of the detail
		D3DXMATRIX	PrevViewProj;	// last frame's view-projection
		D3DXVECTOR4	Extra;		// UNOFFICIAL x: shadow strength, y: shadow map width (m), w: shadow debug view
		D3DXMATRIX	ReflectInvVP;		// UNOFFICIAL: the reflection camera's clip space -> world direction
		D3DXMATRIX	HistoryViewProj;	// UNOFFICIAL: the view-projection of the frame whose clouds are in the history
		D3DXVECTOR4	ReflectData;		// UNOFFICIAL x: strength, yz: the reflection's size, w: the far plane's depth
	};
	ConstantsStruct	Constants = {};

	struct TexturesStruct {
		IDirect3DTexture9* CurrentTexture = nullptr;
		IDirect3DSurface9* CurrentSurface = nullptr;
		IDirect3DTexture9* HistoryTexture[2] = {};
		IDirect3DSurface9* HistorySurface[2] = {};
		IDirect3DTexture9* HistoryRead = nullptr;	// the slot TESR_CloudHistory follows
		IDirect3DTexture9* ShadowTexture = nullptr;	// UNOFFICIAL: the cloud shadow map (256 x 256)
		IDirect3DSurface9* ShadowSurface = nullptr;
	};
	TexturesStruct	Textures;

	float	HideGameClouds = 0.0f;	// 1 while these clouds replace the game's, for SetShadersHook
	float	HideGameCloudsInReflection = 0.0f;	// UNOFFICIAL: 1 while these clouds are drawn into the water's reflection too
	// UNOFFICIAL: the clouds in the game's water reflection. CaptureReflection (SetShadersHook, in the reflection pass) notes the
	// pass's target, depth buffer and camera; DrawIntoReflection (RenderReflectionsHook, after the pass) draws them there.
	void	CaptureReflection(IDirect3DDevice9* Device);
	void	DrawIntoReflection(IDirect3DDevice9* Device);
	void	BeforeResolve(IDirect3DDevice9* Device, IDirect3DSurface9* source);	// the device's StretchRect hook
	void	EndReflectionPass(IDirect3DDevice9* Device);	// RenderReflectionsHook, after the game's pass

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	// March, temporal, then the composite into the frame chain.
	void	RenderClouds(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		IDirect3DSurface9* SourceSurface);

private:
	float		coverage = -1.0f, rain = 0.0f, typeBias = 0.0f;	// eased toward the weather's
	D3DXVECTOR3	wind = D3DXVECTOR3(0, 0, 0);
	D3DXMATRIX	lastViewProj;
	UINT		frame = 0, read = 0;
	double		lastRenderTime = -1.0;	// TheFrameRateManager->Time of the last frame drawn
	UINT		lowWidth = 0, lowHeight = 0, blockWidth = 0, blockHeight = 0;
	double		lastCloudTime = -1.0;	// UNOFFICIAL GameTime: the game's clock, in seconds at the normal time scale, last frame
	IDirect3DSurface9*	reflectTarget = nullptr;	// UNOFFICIAL: this reflection pass's target and depth buffer (held until drawn)
	IDirect3DSurface9*	reflectDepth = nullptr;
	D3DXMATRIX	reflectView, reflectProj;
	IDirect3DStateBlock9*	reflectState = nullptr;
	bool		reflectLogged = false;
	bool		reflectNoted = false;	// UNOFFICIAL: this pass's target is noted (only its first)
	bool		ReflectionWanted() const;
};
