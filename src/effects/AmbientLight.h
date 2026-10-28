#pragma once

#include "../core/NvrDxvkExt.h"

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
	static const int	GridAtlasWidth = 256;
	bool				gridFine = true;
	double				gridPendingOrigin1[3] = {};
	double				gridOrigin1[3] = {};
	bool				gridFineValid = false;
	CachedHandle		paramGridOrigin1;
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
	bool				noEdgeOutline = true;
	int					normalSmoothing = 36;
	float				normalSmoothingWorld = 0.0f;
	float				horizonBias = 0.0f;
	int					hizLogQ = 0;
	bool				hizEstimateFailed = false;
	bool				hizEstimateChecked = false;
	UINT				hizEstimateFrames = 0;
	bool	HiZWanted() const { return false; }
	CachedHandle		paramEdges, techSmooth;
	bool				prepared = false;
	bool	RenderPrepare(IDirect3DDevice9* Device);
	IDirect3DTexture9* SmoothedNormals() const { return prepared ? prepTexture : nullptr; }
	static const int	SmoothComputeMaxReach = 64;
	ID3D9NvrExtPipeline*	smoothPipeline = nullptr;
	ID3D9NvrExtBuffer*		smoothResult = nullptr;
	UINT				smoothWidth = 0, smoothHeight = 0;
	bool				smoothComputeFailed = false;
	bool				smoothComputeReachLogged = false;
	UINT				smoothComputeFrames = 0;
	bool	SmoothCompute(IDirect3DDevice9* Device, IDirect3DSurface9* Target, UINT Width, UINT Height);
	void	SmoothComputeRelease();
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
	bool				gridWallMemory = true;
	IDirect3DTexture9*	gridScreenWalls[2] = {};
	IDirect3DSurface9*	gridScreenWallsSurface[2] = {};
	bool				gridWallsValid = false;
	CachedHandle		paramGridWalls;
	static const int	GridLampsMax = 32;
	float				gridLamps = 4.0f;
	CachedHandle		paramGridLamps, paramGridLampColors, paramGridLampInfo;
	int		SetGridLamps();
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

	bool	RenderBlurCompute(const D3DXVECTOR4& layout, UINT width, UINT height);
	ID3D9NvrExtPipeline*	blurPipeline = nullptr;
	bool				blurComputeTried = false;
	bool				blurComputeOk = false;
	UINT				blurComputeFrames = 0;

	enum ExtShader { ExtPrepare, ExtSmooth, ExtEstimate, ExtBlur, ExtAccumulate, ExtStats, ExtReduce, ExtApply, ExtScreen, ExtInject, ExtPropagate, ExtEstimateHiZ,
		ExtGILight, ExtGIEstimate0, ExtGIEstimate1, ExtGIEstimate2, ExtGIAccumulate, ExtGIApply, ExtShaderCount };
	ExtPass				extPasses[ExtShaderCount][NVR_EXT_MAX_TARGETS];
	ExtUniformBuffer	extUniforms;
	CachedHandle		extHandles[38];
	bool	ExtOn() const;
	bool	ExtDraw(ExtShader Shader, IDirect3DSurface9* const* Targets, UINT TargetCount, IDirect3DBaseTexture9* const* Textures, UINT Variant, const D3DVIEWPORT9& Viewport);

	bool				computeGI = false;
	float				giStrength = 1.0f;
	float				giThickness = 100.0f;
	int					giQuality = 0;
	bool				giThinOcclusion = false;
	int					giTest = 0;
	IDirect3DTexture9*	giLightTexture = nullptr;
	IDirect3DSurface9*	giLightSurface = nullptr;
	bool				giLightTried = false;
	bool				giFailed = false;
	int					giFiles = -1;
	int					giReported = -1;
	UINT				giFrames = 0;
	bool				giDrawn = false;
	bool	GIActive();

	void	EnsureBuffers();
	void	UpdateConstants() {};
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
