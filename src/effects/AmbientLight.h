#pragma once

// UNOFFICIAL 2026-10-14: ambient occlusion and bounce light in one pass (Effects\AmbientLight.fx.hlsl). Off by default
// ([Shaders.AmbientLight.Status] Enabled); while it renders, ShaderManager skips the AmbientOcclusion and BounceLight effects
// (it replaces both). Four half-resolution passes into the buffers below, then the apply in the frame chain.
class AmbientLightEffect : public EffectRecord
{
public:
	AmbientLightEffect() : EffectRecord("AmbientLight") {};

	struct ConstantsStruct {
		D3DXVECTOR4	Data;	// x: radius, y: occlusion strength, z: bounce strength, w: draw distance
		D3DXVECTOR4	Data2;	// x: darkest occlusion, y: brightness above which it fades, z: blur radius, w: debug view
	};
	ConstantsStruct	Constants = {};

	IDirect3DTexture9*	prepTexture = nullptr;		// depth, normal and colour per half-resolution pixel (A32B32G32R32F)
	IDirect3DSurface9*	prepSurface = nullptr;
	IDirect3DTexture9*	bufferTexture[2] = {};		// rgb bounce, a visibility (A16B16G16R16F), ping-pong for the blur
	IDirect3DSurface9*	bufferSurface[2] = {};
	bool				buffersTried = false;
	CachedHandle		paramLayout, paramFlags, paramTemporal, paramReproject;
	// UNOFFICIAL 2026-10-14 Temporal: the bounce kept from frame to frame (rgb bounce, a view depth; A16B16G16R16F), one read
	// while the other is written, and last frame's camera to find each spot in it.
	IDirect3DTexture9*	historyTexture[2] = {};
	IDirect3DSurface9*	historySurface[2] = {};
	IDirect3DTexture9*	occlusionHistory[2] = {};		// UNOFFICIAL 2026-10-14: the occlusion's (r visibility, g view depth; A16B16G16R16F like the bounce history: one MRT bit depth)
	IDirect3DSurface9*	occlusionHistorySurface[2] = {};
	int					historyIndex = 0;		// the one holding last frame's
	bool				historyValid = false;
	bool				temporal = true;
	D3DXMATRIX			lastView, lastInvView, lastProj;
	double				lastTime = 0.0;
	unsigned			frameNumber = 0;

	// UNOFFICIAL 2026-10-14 light grid (GridBounce; AmbientLight.fx.hlsl GridInject): the sun's bounce light in a 32 x 32 x 16
	// grid of cells round the player, from a small depth map of the world seen from the sun (drawn by ShadowManager).
	static const int	GridSunMapSize = 256;
	static const int	GridAtlasSize = 128;	// 4 x 4 tiles of 32 x 32 cells
	float				gridBounce = 0.0f;		// strength (0: off, nothing made or drawn)
	float				gridCell = 200.0f;		// cell size, world units
	int					gridSteps = 8;			// spreading steps
	float				gridSpread = 0.3f;		// share of a neighbour's light passed on per step
	float				gridBlocking = 1.0f;	// how much the surfaces in a cell stop light crossing it
	IDirect3DTexture9*	gridGeometry = nullptr;	// per cell: the surfaces' average normal and cover (blocking; A16B16G16R16F)
	IDirect3DSurface9*	gridGeometrySurface = nullptr;
	CachedHandle		paramGridStep, paramGridShift, paramGridScreen;
	float				gridScreenStrength = 6.0f;	// GridScreenStrength
	// UNOFFICIAL 2026-10-14 screen memory (indoors, at night): per cell the light its surfaces showed on screen (ScreenInject).
	IDirect3DTexture9*	gridScreen[2] = {};			// harmonics (A16B16G16R16F, the atlas), ping-pong
	IDirect3DSurface9*	gridScreenSurface[2] = {};
	IDirect3DTexture9*	gridScreenColor[2] = {};	// colour x light
	IDirect3DSurface9*	gridScreenColorSurface[2] = {};
	int					gridScreenIndex = 0;
	bool				gridScreenValid = false;
	double				gridScreenOrigin[3] = {};	// the grid corner the memory was stored for
	bool				gridSunActive = false;		// the sun map feeds the grid now (outdoors, sun up; set by WantsGridSunMap)
	bool	GridMoveOrDue();						// works the grid's corner out; true when it is time to update
	IDirect3DTexture9*	gridSunTexture = nullptr;	// R32F depth from the sun
	IDirect3DSurface9*	gridSunSurface = nullptr;
	IDirect3DSurface9*	gridSunDepth = nullptr;
	IDirect3DTexture9*	gridSunColorTexture = nullptr;	// A8R8G8B8: the sunlit surfaces' own colours (ShadowGridColor.pso)
	IDirect3DSurface9*	gridSunColorSurface = nullptr;
	bool				gridSunHasColor = false;
	bool				gridColorValid = false;			// the finished grid carries colours
	IDirect3DTexture9*	gridWaveColor[2] = {};			// rgb: colour x light, a: light (each step, as gridWave)
	IDirect3DSurface9*	gridWaveColorSurface[2] = {};
	IDirect3DTexture9*	gridAccumColor[2] = {};			// the same over all steps
	IDirect3DSurface9*	gridAccumColorSurface[2] = {};
	IDirect3DTexture9*	gridWave[2] = {};			// this step's light (A16B16G16R16F, the atlas)
	IDirect3DSurface9*	gridWaveSurface[2] = {};
	IDirect3DTexture9*	gridAccum[2] = {};			// the light over all steps so far
	IDirect3DSurface9*	gridAccumSurface[2] = {};
	int					gridAccumIndex = 0;			// the one holding the finished grid
	bool				gridBuffersTried = false;
	bool				gridSunFresh = false;		// the sun map was drawn this frame: inject and spread again
	bool				gridValid = false;			// a finished grid exists (for gridOrigin)
	double				gridOrigin[3] = {};			// the finished grid's corner (absolute world)
	double				gridPendingOrigin[3] = {};	// the corner of the grid the sun map just drawn is for
	unsigned			gridFrame = 0;
	float				gridSunTexel = 1.0f;		// world units per sun map texel
	D3DXMATRIX			gridSunToGrid, gridToSun;
	ShadowsExteriorEffect::ShadowMapSettings gridSunMap = {};
	CachedHandle		paramGridOrigin, paramGridData, paramGridLight, paramGridSunToGrid, paramGridToSun;

	void	EnsureGridBuffers();
	bool	WantsGridSunMap(const D3DXVECTOR3& sunDir);		// ShadowManager: draw the sun map this frame?
	D3DXMATRIX GridSunViewProj(const D3DXVECTOR3& sunDir);	// sets gridSunMap up for it and returns its view-projection
	void	RenderGrid(IDirect3DDevice9* Device, bool sun, bool screen);
	// UNOFFICIAL 2026-10-14 (set by ShaderManager for one Render): apply the sun shadows in this effect's passes, their own pass
	// skipped; shadowApplied reports that it was done.
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
