#pragma once

class AmbientLightEffect : public EffectRecord
{
public:
	AmbientLightEffect() : EffectRecord("AmbientLight") {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Data2;
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	prepTexture = nullptr;
	IDirect3DSurface9*	prepSurface = nullptr;
	IDirect3DTexture9*	bufferTexture[2] = {};
	IDirect3DSurface9*	bufferSurface[2] = {};
	bool				buffersTried = false;
	CachedHandle		paramLayout, paramFlags, paramTemporal, paramReproject;
	CachedHandle		paramSunFarFlags;
	CachedHandle		paramScene;
	CachedHandle		techSky;
	D3DXVECTOR4			sceneLight = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);
	IDirect3DTexture9*	historyTexture[2] = {};
	IDirect3DSurface9*	historySurface[2] = {};
	IDirect3DTexture9*	occlusionHistory[2] = {};
	IDirect3DSurface9*	occlusionHistorySurface[2] = {};
	int					historyIndex = 0;
	bool				historyValid = false;
	bool				temporal = true;
	D3DXMATRIX			lastView, lastInvView, lastProj;
	double				lastTime = 0.0;
	unsigned			frameNumber = 0;

	static const int	GridSunMapSize = 256;
	static const int	GridAtlasSize = 128;
	float				gridBounce = 0.0f;
	float				gridCell = 200.0f;
	int					gridSteps = 8;
	float				gridSpread = 0.3f;
	float				gridBlocking = 1.0f;
	IDirect3DTexture9*	gridGeometry = nullptr;
	IDirect3DSurface9*	gridGeometrySurface = nullptr;
	CachedHandle		paramGridStep, paramGridShift, paramGridScreen;
	float				gridScreenStrength = 6.0f;
	float				gridColour = 0.25f;
	static const int	StatsWidth = 16, StatsHeight = 9;
	bool				interiorBalance = true;
	float				interiorBounceShare = 1.0f;
	IDirect3DTexture9*	statsTexture = nullptr;
	IDirect3DSurface9*	statsSurface = nullptr;
	IDirect3DTexture9*	balanceTexture[2] = {};
	IDirect3DSurface9*	balanceSurface[2] = {};
	int					balanceIndex = 0;
	bool				balanceValid = false;
	CachedHandle		paramInterior;
	IDirect3DTexture9*	gridScreen[2] = {};
	IDirect3DSurface9*	gridScreenSurface[2] = {};
	IDirect3DTexture9*	gridScreenColor[2] = {};
	IDirect3DSurface9*	gridScreenColorSurface[2] = {};
	int					gridScreenIndex = 0;
	bool				gridScreenValid = false;
	double				gridScreenOrigin[3] = {};
	bool				gridSunActive = false;
	bool	GridMoveOrDue();
	IDirect3DTexture9*	gridSunTexture = nullptr;
	IDirect3DSurface9*	gridSunSurface = nullptr;
	IDirect3DSurface9*	gridSunDepth = nullptr;
	IDirect3DTexture9*	gridSunColorTexture = nullptr;
	IDirect3DSurface9*	gridSunColorSurface = nullptr;
	bool				gridSunHasColor = false;
	bool				gridColorValid = false;
	IDirect3DTexture9*	gridWaveColor[2] = {};
	IDirect3DSurface9*	gridWaveColorSurface[2] = {};
	IDirect3DTexture9*	gridAccumColor[2] = {};
	IDirect3DSurface9*	gridAccumColorSurface[2] = {};
	IDirect3DTexture9*	gridWave[2] = {};
	IDirect3DSurface9*	gridWaveSurface[2] = {};
	IDirect3DTexture9*	gridAccum[2] = {};
	IDirect3DSurface9*	gridAccumSurface[2] = {};
	int					gridAccumIndex = 0;
	bool				gridBuffersTried = false;
	bool				gridSunFresh = false;
	bool				gridValid = false;
	double				gridOrigin[3] = {};
	double				gridPendingOrigin[3] = {};
	unsigned			gridFrame = 0;
	UINT				gridRenderedFrame = 0;
	float				gridSunTexel = 1.0f;
	D3DXMATRIX			gridSunToGrid, gridToSun;
	ShadowsExteriorEffect::ShadowMapSettings gridSunMap = {};
	CachedHandle		paramGridOrigin, paramGridData, paramGridLight, paramGridSunToGrid, paramGridToSun;

	void	EnsureGridBuffers();
	bool	WantsGridSunMap(const D3DXVECTOR3& sunDir);
	D3DXMATRIX GridSunViewProj(const D3DXVECTOR3& sunDir);
	void	RenderGrid(IDirect3DDevice9* Device, bool sun, bool screen);
	bool				foldShadow = false;
	bool				shadowApplied = false;

	void	EnsureBuffers();
	void	UpdateConstants() {};
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
