#pragma once
#include "../core/TemporalHistoryStats.h"

// UNOFFICIAL: screen-space reflections on the ground (Effects\Reflections.fx.hlsl): wet streets mirroring the neon and the
// sky in the rain, an optional dry sheen on outdoor ground and on interior floors. Half-resolution trace, history and
// blur into its own buffers, then a full-resolution composite in the HDR chain (after Specular, before the fog).
class ReflectionsEffect : public EffectRecord
{
public:
	ReflectionsEffect() : EffectRecord("Reflections") {};

	struct SettingsStruct {
		float	GroundSheen;	// outdoors: how much dry ground facing up reflects
		float	RainWetness;	// outdoors: how much the rain's wetness adds
		float	InteriorFloors;	// indoors: how much floors reflect
		float	Roughness;		// dry surfaces (0 mirror .. 1 matte)
		float	WetRoughness;	// wet surfaces
		float	MaxDistance;	// ray length, game units
		int		Steps;
		float	Thickness;		// how thick things count as, for a ray passing behind them
		float	Temporal;		// share of last frame's result kept (0: no history)
		float	BlurRadius;		// half-resolution texels each way
		float	DrawDistance;	// surfaces farther than this do not reflect
		bool	SkyFallback;	// outdoors: rays that find nothing take the sky
		bool	CubeMap;		// rays that find nothing take the surroundings cube
		float	CubeCapture;	// how much of the cube each frame's view replaces
		float	CubeFade;		// how much of what is off screen fades each frame
		int		DebugView;		// 1: the reflection alone, 2: where it found something, 3: the surroundings cube (4-6: see the .fx)
		bool	GridFallback;	// UNOFFICIAL 2026-10-19: rays that find nothing on screen also take AmbientLight's light grid
		float	GridBrightness;	// its brightness
		int		MaterialAware;	// UNOFFICIAL 2026-10-20: 1 = the _rmaos materials decide what reflects (core/ReflMaterial.h); 2 = filled, not used (test)
		float	MaterialReflect;	// how much a smooth known material reflects
		float	MaterialMaxRoughness;	// known materials at least this rough do not reflect
	};
	SettingsStruct	Settings;

	struct ConstantsStruct {
		D3DXVECTOR4	Data;		// x: dry reflectivity now, y: wetness now, z: dry roughness, w: wet roughness
		D3DXVECTOR4	Data2;		// x: ray length, y: steps, z: thickness scale, w: noise frame offset
		D3DXVECTOR4	Data3;		// x: history weight this frame, y: blur radius, z: debug view, w: sky fallback
		D3DXVECTOR4	Data4;		// x: draw distance
		D3DXVECTOR4	Layout;		// xy: one half-resolution texel in screen UV
		D3DXMATRIX	Reproject;	// camera-relative world -> last frame's clip space
		D3DXVECTOR4	Cube;		// x: face being drawn, y: its size, z: mip level, w: 1 = empty the cube
		D3DXVECTOR4	Cube2;		// x: capture blend, y: fade per frame, z: last mip level, w: 1 = the cube is in use
		D3DXVECTOR4	Grid;		// xyz: the light grid's corner relative to the camera, w: cell size
		D3DXVECTOR4	Grid2;		// x: 1 = the grid is in use, y: brightness, z: 1 = it carries colours
		D3DXVECTOR4	Grid3;		// rgb: the light every surface gets here (AmbientLight's, linear)
		D3DXVECTOR4	Material;	// UNOFFICIAL 2026-10-20 x: 1 = the material buffer is read, y: MaterialMaxRoughness, z: MaterialReflect
	};
	ConstantsStruct	Constants;
	IDirect3DBaseTexture9*	GridLightSlot = nullptr;	// AmbientLight's finished grid this frame (TextureManager slots)
	IDirect3DBaseTexture9*	GridColorSlot = nullptr;

	// The surroundings cube: two capture cubes (this frame's, last frame's) and the blurred reflection cube with mips.
	static const UINT CubeSize = 64;
	static const UINT CubeLevels = 7;	// 64 .. 1
	struct CubesStruct {
		IDirect3DCubeTexture9*	Capture[2] = { nullptr, nullptr };
		IDirect3DSurface9*		CaptureFace[2][6] = {};
		IDirect3DCubeTexture9*	Env = nullptr;
		IDirect3DSurface9*		EnvFace[CubeLevels][6] = {};
		IDirect3DBaseTexture9*	CaptureLastSlot = nullptr;	// what the effect's samplers read (TextureManager slots)
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
		IDirect3DTexture9* MaterialTexture = nullptr;	// UNOFFICIAL 2026-10-20 MaterialAware: full resolution, made only when compiled in
		IDirect3DSurface9* MaterialSurface = nullptr;
	};
	TexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender();
	void	RenderBuffers(IDirect3DDevice9* Device);	// trace, history, blur: everything before the composite
	bool	CubeWanted();								// the cube is kept up even when no floor reflects (metal, env maps)
	void	RenderCubeOnly(IDirect3DDevice9* Device);	// the cube alone, on a frame RenderBuffers does not run
	bool	CubeReady() const;							// the cube was kept up last frame (what the object shaders read now)
	void	CaptureMaterial(IDirect3DDevice9* Device);	// UNOFFICIAL 2026-10-20 MaterialAware: the scene's alpha into the material buffer, before any effect draws
	void	BindMaterial();								// the material buffer on stage 15 for the next passes (ClearPassTextures unbinds it)

private:
	D3DXMATRIX	lastView, lastInvView, lastProj;
	UINT		lastRenderedFrame = 0;	// TheFrameRateManager->FrameNumber of the last frame this effect drew (UNOFFICIAL 2026-10-18: it counted
										// UpdateConstants calls, two a frame, so last frame's history and cube were never kept)
	bool		haveHistory = false;
	TemporalHistoryStats	stats;		// the trace's history use, for the log
	UINT		cubeSteps = 0, cubeEmptied = 0;	// for the log: cube updates, and how many of them started from an empty cube
	void		ReportHistory(const char* when);
	int			captureIndex = 0;		// Cubes.Capture[captureIndex] holds the latest capture
	bool		cubeValid = false;
	void*		cubeCell = nullptr;		// the cell the cube was captured in
	UINT		cubeFrame = 0;			// FrameNumber of the last cube update
	D3DXMATRIX	cubeInvView;			// the camera at the last cube update (for the jump test)
	bool		cubeOnly = false;		// RenderCubeOnly is drawing (ShouldRender lets its passes through)
	bool		materialOnly = false;	// CaptureMaterial is drawing
	bool		materialFilled = false;	// the material buffer was filled this frame
	UINT		gridFrames = 0, gridUsedFrames = 0;	// for the log: frames drawn with GridFallback on, and of them with a grid to read
	void	GridStep();							// the light grid's constants and textures for this frame's trace
	void	CubeStep(IDirect3DDevice9* Device);
	void	RenderCube(IDirect3DDevice9* Device, bool reset);
};
