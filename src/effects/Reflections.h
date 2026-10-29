#pragma once
#include "../core/TemporalHistoryStats.h"

class ReflectionsEffect : public EffectRecord
{
public:
	ReflectionsEffect() : EffectRecord("Reflections") {};

	struct SettingsStruct {
		float	GroundSheen;
		float	RainWetness;
		float	InteriorFloors;
		float	Roughness;
		float	WetRoughness;
		float	MaxDistance;
		int		Steps;
		float	Thickness;
		float	Temporal;
		float	BlurRadius;
		float	DrawDistance;
		bool	SkyFallback;
		bool	CubeMap;
		float	CubeCapture;
		float	CubeFade;
		int		DebugView;
		bool	GridFallback;
		float	GridBrightness;
		int		MaterialAware;
		float	MaterialReflect;
		float	MaterialMaxRoughness;
		float	ReflectAdd;
		float	SpecularOcclusion;
		float	SpecularOcclusionLevel;
		bool	HiZ;
		bool	HiZSafeCells;
		bool	HiZInteriorsTest;
		float	ReflectivityExterior;
		float	ReflectivityInterior;
		bool	RtEnabled;
		float	RtMaxRoughness;
		float	RtDistance;
		int		RtDebugView;
		float	RtScreenTolerance;
		float	RtEstimate;
		bool	RtSunShadows;
		bool	RtSplitTest;
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	Data2;
		D3DXVECTOR4	Data3;
		D3DXVECTOR4	Data4;
		D3DXVECTOR4	Layout;
		D3DXMATRIX	Reproject;
		D3DXVECTOR4	Cube;
		D3DXVECTOR4	Cube2;
		D3DXVECTOR4	Grid;
		D3DXVECTOR4	Grid2;
		D3DXVECTOR4	Grid3;
		D3DXVECTOR4	Material;
	};
	ConstantsStruct	Constants;
	IDirect3DBaseTexture9*	GridLightSlot = nullptr;
	IDirect3DBaseTexture9*	GridColorSlot = nullptr;

	static const UINT CubeSize = 64;
	static const UINT CubeLevels = 7;
	struct CubesStruct {
		IDirect3DCubeTexture9*	Capture[2] = { nullptr, nullptr };
		IDirect3DSurface9*		CaptureFace[2][6] = {};
		IDirect3DCubeTexture9*	Env = nullptr;
		IDirect3DSurface9*		EnvFace[CubeLevels][6] = {};
		IDirect3DBaseTexture9*	CaptureLastSlot = nullptr;
		IDirect3DBaseTexture9*	CaptureNewSlot = nullptr;
		IDirect3DBaseTexture9*	EnvSlot = nullptr;
	};
	CubesStruct	Cubes;

	struct TexturesStruct {
		IDirect3DTexture9* TraceTexture = nullptr;
		IDirect3DSurface9* TraceSurface = nullptr;
		IDirect3DTexture9* BufferTexture = nullptr;
		IDirect3DSurface9* BufferSurface = nullptr;
		IDirect3DTexture9* TempTexture = nullptr;
		IDirect3DSurface9* TempSurface = nullptr;
		IDirect3DTexture9* HistoryTexture = nullptr;
		IDirect3DSurface9* HistorySurface = nullptr;
		IDirect3DTexture9* MaterialTexture = nullptr;
		IDirect3DSurface9* MaterialSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	RenderBuffers(IDirect3DDevice9* Device);
	bool	CubeWanted();
	void	RenderCubeOnly(IDirect3DDevice9* Device);
	bool	CubeReady() const;
	void	CaptureMaterial(IDirect3DDevice9* Device);
	void	BindMaterial();

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	bool	HiZWanted() const;

private:
	ExtPass				extPasses[8];
	ExtPass				extTraceHiZ;
	bool				hizTraceFailed = false;
	UINT				hizTraceFrames = 0;
	ExtPass				extTraceHiZCount;
	ExtPass				extRtPasses[2];
	bool				rtTraceFailed = false;
	UINT				rtTraceFrames = 0, rtFallbackFrames = 0;
	ExtUniformBuffer	extRtUniforms;
public:
	struct RtImagesStruct {
		IDirect3DTexture9* March = nullptr;	IDirect3DSurface9* MarchSurface = nullptr;
		IDirect3DTexture9* Dir = nullptr;	IDirect3DSurface9* DirSurface = nullptr;
		IDirect3DTexture9* Ray = nullptr;	IDirect3DSurface9* RaySurface = nullptr;
		IDirect3DTexture9* Hit = nullptr;	IDirect3DSurface9* HitSurface = nullptr;
		IDirect3DTexture9* Shade = nullptr;	IDirect3DSurface9* ShadeSurface = nullptr;
	};
	RtImagesStruct		Rt;
	void				RtRegister();
private:
	bool				rtRays = false;
	bool				RtWanted() const;
	bool				RtImages();
	float				RtSunAmount() const;
	bool				RtTrace(IDirect3DDevice9* Device, IDirect3DSurface9* target);
	ExtUniformBuffer	extUniforms;
	D3DXVECTOR4			extSky = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);
	IDirect3DTexture9*	extSkyTexture = nullptr;
	D3DXMATRIX	lastView, lastInvView, lastProj;
	UINT		lastRenderedFrame = 0;
	bool		haveHistory = false;
	TemporalHistoryStats	stats;
	UINT		cubeSteps = 0, cubeEmptied = 0;
	void		ReportHistory(const char* when);
	int			captureIndex = 0;
	bool		cubeValid = false;
	void*		cubeCell = nullptr;
	UINT		cubeFrame = 0;
	D3DXMATRIX	cubeInvView;
	bool		cubeOnly = false;
	bool		materialOnly = false;
	bool		materialFilled = false;
	UINT		gridFrames = 0, gridUsedFrames = 0;
	void	GridStep();
	void	CubeStep(IDirect3DDevice9* Device);
	void	RenderCube(IDirect3DDevice9* Device, bool reset);
};
