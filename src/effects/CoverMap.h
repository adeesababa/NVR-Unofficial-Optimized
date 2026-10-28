#pragma once

class CoverMapEffect : public EffectRecord
{
public:
	CoverMapEffect() : EffectRecord("CoverMap") { memset(&Constants, 0, sizeof(Constants)); }

	void	UpdateConstants() override;
	void	UpdateSettings() override;
	void	RegisterConstants() override;
	void	RegisterTextures() override;
	bool	ShouldRender() override;

	bool	WetActive() const;
	bool	SnowActive() const;
	UINT	WetTechnique(WetWorldEffect* wetWorld, UINT normal) const;
	UINT	SnowTechnique(SnowAccumulationEffect* snow) const;

	void	RenderUpdate(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
	void	RenderDebug(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* SourceSurface);

	struct SettingsStruct {
		bool	wet = false, snow = false;
		float	size = 0.0f;
		int		resolution = 0;
		float	wetTime = 0.0f, dryTime = 0.0f, puddleFill = 0.0f, puddleDry = 0.0f, wetShade = 1.0f;
		float	snowIncrease = 0.0f, snowDecrease = 0.0f, snowShade = 1.0f, drifts = 0.0f, footprints = 0.0f, footSize = 0.0f;
		int		debug = 0;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Update, Origin, Shift, Rates, SnowRates, Sun, Shade, Start, FootA, FootB, Debug;
		D3DXVECTOR4	Data, Window, Global, Trail;
		D3DXMATRIX	OrthoInverse;
	};
	ConstantsStruct	Constants;

private:
	bool	EnsureMap(IDirect3DDevice9* Device);
	void	ReleaseMap();
	double	MinStep() const;
	float	TrailRange(double texelSize) const;
	void	SetReaderConstants();

	IDirect3DTexture9*	mapTexture[2] = {};
	IDirect3DSurface9*	mapSurface[2] = {};
	IDirect3DTexture9*	auxTexture[2] = {};
	IDirect3DSurface9*	auxSurface[2] = {};
	IDirect3DTexture9*	currentTexture = nullptr;
	IDirect3DTexture9*	currentAux = nullptr;
	int					current = 0;
	int					mapResolution = 0;
	bool				mapUnorm = true;
	bool				mapFailed = false;
	bool				mapValid = false;

	long long			originX = 0, originY = 0;
	double				referenceZ = 0.0;
	TESWorldSpace*		worldSpace = nullptr;

	float				wetBound = 0.0f, snowBound = 0.0f;
	D3DXVECTOR4			startGlobal = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	float				pendWetGain = 0.0f, pendWetDry = 0.0f, pendPuddleGain = 0.0f, pendPuddleDry = 0.0f, pendSnowGain = 0.0f, pendSnowMelt = 0.0f;
	double				lastCommit = 0.0;
	bool				clockSet = false;
	bool				lastRaining = false, lastSnowing = false;
	float				rainNow = 0.0f;
	bool				snowNow = false;
	bool				resyncWet = true, resyncSnow = true;
	unsigned int		tickFrame = ~0u;
	unsigned int		framesSinceUpdate = 0;
	double				feetX = 0.0, feetY = 0.0, feetZ = 0.0;
	bool				feetSet = false;
	unsigned int		updates = 0;
	CachedHandle		updateHandle;
};

extern CoverMapEffect* TheCoverMap;
extern unsigned int CoverMapOrthoFrame;
