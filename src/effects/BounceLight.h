#pragma once

class BounceLightEffect : public EffectRecord
{
public:
	BounceLightEffect() : EffectRecord("BounceLight") {};

	struct BounceLightStruct {
		D3DXVECTOR4		Data;
	};
	BounceLightStruct	Constants;
	bool				quarterResolution = false;
	float				offScreenLight = 0.0f;
	float				multiBounce = 0.0f;
	bool				temporal = false;
	bool				halfSamples = false;
	static const int	SpillLevels = 7;
	float				strength = 0.0f;
	float				neonSpill = 0.0f;
	float				spillReach = 1.0f;
	float				spillLightLevel = 1.5f;
	float				spillReportedLight = -1.0f;
	int					spillDebug = 0;
	bool	SpillOn() const { return Enabled && neonSpill > 0.0f; }
	bool	HardEdgesOn() const { return Enabled && hardEdges; }
	bool	hardEdges = false;
	float				spillMemorySeconds = 1.0f;
	float				spillNear = 1.0f, spillFar = 1.0f;
	float				spillCap = 0.0f;
	float				spillHoldAfterTurn = 0.0f;
	float				spillSeparateDepth = 0.0f;
	float				spillCapSceneLight = 0.0f;
	float				spillHardEdges = 0.0f;
	float				spillMaskSpillOnly = 0.0f;
	float				spillStillSeconds = 0.0f;
	IDirect3DTexture9*	spillTexture[SpillLevels] = {};
	IDirect3DSurface9*	spillSurface[SpillLevels] = {};
	bool				spillTried = false;
	IDirect3DTexture9*	spillMemoryTexture[2][2] = {};
	IDirect3DSurface9*	spillMemorySurface[2][2] = {};
	int					spillMemoryIndex = 0;
	bool				spillMemoryValid = false;
	float				spillSteadySeconds = 0.0f;
	IDirect3DTexture9*	spillSteadyTexture[2] = {};
	IDirect3DSurface9*	spillSteadySurface[2] = {};
	int					spillSteadyIndex = 0;
	bool				spillSteadyValid = false;
	double				spillSteadyTime = 0.0;
	D3DXMATRIX			spillSteadyView, spillSteadyInvView, spillSteadyProj;
	bool	EnsureSpill();
	bool	RenderSpillPyramid(IDirect3DDevice9* Device, IDirect3DTexture9* scene, float threshold);
	float	SpillThreshold();
	IDirect3DTexture9*	RenderSpillGather(IDirect3DDevice9* Device, bool quarter, const D3DVIEWPORT9& reduced);
	IDirect3DTexture9*	halfHistory[2] = {};
	IDirect3DSurface9*	halfHistorySurface[2] = {};
	IDirect3DTexture9*	quarterHistory[2] = {};
	IDirect3DSurface9*	quarterHistorySurface[2] = {};
	bool				halfHistoryTried = false;
	bool				quarterHistoryTried = false;
	void	EnsureHistory(bool quarter);
	D3DXMATRIX			lastView, lastInvView, lastProj;
	bool				lastValid = false;
	bool				lastQuarter = false;
	bool				historyValid = false;
	int					historyIndex = 0;
	unsigned			frameNumber = 0;
	double				lastTime = 0.0;
	IDirect3DTexture9*	halfTexture[2] = {};
	IDirect3DSurface9*	halfSurface[2] = {};
	IDirect3DTexture9*	quarterTexture[2] = {};
	IDirect3DSurface9*	quarterSurface[2] = {};
	IDirect3DTexture9*	halfPrepTexture = nullptr;
	IDirect3DSurface9*	halfPrepSurface = nullptr;
	IDirect3DTexture9*	quarterPrepTexture = nullptr;
	IDirect3DSurface9*	quarterPrepSurface = nullptr;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	EnsureBuffers(bool quarter);
	bool	halfTried = false;
	bool	quarterTried = false;
	CachedHandle paramLayout, paramTemporal, paramAmbient, paramReproject, techTemporalHalf, techTemporal, techPlus;
	CachedHandle paramSpill, paramSpillTarget, paramSpillDebug, paramSpillMemory, paramSpillShape, techSpillPyramid, techSpillGather;
	CachedHandle paramSpillOptions, techSpillGatherSeparate, techPlusHard, techTemporalHard, techTemporalHalfHard;
	CachedHandle paramSpillSteady;
	bool				deferCombine = false;
	bool				combineOnly = false;
	bool				deferredReady = false;
	IDirect3DTexture9*	deferredTexture = nullptr;
	D3DXVECTOR4			deferredLayout = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	void	UpdateSettings();
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget,
		IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget,
		IDirect3DSurface9* SourceBuffer) override;
};
