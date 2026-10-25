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
	CachedHandle		paramSunFarFlags;	// UNOFFICIAL 2026-10-19 SunFar (NVR_SunFarFlags)
	CachedHandle		paramScene;					// UNOFFICIAL 2026-10-18 (ambfix): NVR_ALScene, the scene's own light
	CachedHandle		techSky;					// UNOFFICIAL 2026-10-20 (skyamb) Skylighting: technique AmbientLightSky
	D3DXVECTOR4			sceneLight = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 1.0f);	// the light every surface gets (linear; Render)
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
	static const int	GridAtlasWidth = 256;	// UNOFFICIAL 2026-10-22 (lightgrid2): two levels side by side (coarse, fine; GridFine)
	bool				gridFine = true;		// GridFine: the fine level (a quarter the cell size) round the camera, outdoors with the sun map
	double				gridPendingOrigin1[3] = {};	// the fine level's corner for the grid being made (absolute world)
	double				gridOrigin1[3] = {};		// ... and for the finished grid
	bool				gridFineValid = false;		// the finished grid has a fine level
	CachedHandle		paramGridOrigin1;
	float				gridBounce = 0.0f;		// strength (0: off, nothing made or drawn)
	float				gridCell = 200.0f;		// cell size, world units
	int					gridSteps = 8;			// spreading steps
	float				gridSpread = 0.3f;		// share of a neighbour's light passed on per step
	float				gridBlocking = 1.0f;	// how much the surfaces in a cell stop light crossing it
	IDirect3DTexture9*	gridGeometry = nullptr;	// per cell: the surfaces' average normal and cover (blocking; A16B16G16R16F)
	IDirect3DSurface9*	gridGeometrySurface = nullptr;
	CachedHandle		paramGridStep, paramGridShift, paramGridScreen;
	float				gridScreenStrength = 6.0f;	// GridScreenStrength
	float				gridColour = 0.25f;			// UNOFFICIAL 2026-10-18 (ambfix) GridColour: share of the grid light's own colour kept
	// UNOFFICIAL 2026-10-18 (ambfix2) indoor balance (InteriorBalance, InteriorBounceShare; passes 5-6 of AmbientLight.fx.hlsl).
	static const int	StatsWidth = 16, StatsHeight = 9;	// 2 x 2 samples per texel (StatsSample)
	bool				interiorBalance = true;
	float				interiorBounceShare = 1.0f;
	// UNOFFICIAL 2026-10-21 (aloutline) NoEdgeOutline: the busy-depth rule (Apply) needs a bend at three pixels in a row, so the edges
	// of things no longer count as grass (NVR_ALFlags.y).
	bool				noEdgeOutline = true;
	// UNOFFICIAL 2026-10-21 (alrocks; the occlusion cut off hard along low-poly rocks' triangle edges): NormalSmoothing (half-resolution
	// texels the normal is averaged over after Prepare, 0 off) and HorizonBias (degrees above the tangent plane each horizon starts, 0 off); NVR_ALEdges.
	int					normalSmoothing = 36;
	// UNOFFICIAL 2026-10-24 (charfacets; owner: indoors every triangle of a character showed, flat, hard-edged, even with SmoothObjectNormals):
	// NormalSmoothingWorld, the smoothing's reach in WORLD units, used where it is wider than NormalSmoothing's texels (close to the camera,
	// where a character's triangles are 100-300 pixels across and the pixel reach covered only a band at each edge). NVR_ALEdges.z, 0 off.
	float				normalSmoothingWorld = 0.0f;
	float				horizonBias = 0.0f;
	CachedHandle		paramEdges, techSmooth;
	bool				prepared = false;			// RenderPrepare drew Prepare + the smoothing this frame (Render skips its Prepare)
	bool	RenderPrepare(IDirect3DDevice9* Device);	// ShaderManager, before the AO's sky pass: Prepare, then technique AmbientLightSmooth
	IDirect3DTexture9* SmoothedNormals() const { return prepared ? prepTexture : nullptr; }
	IDirect3DTexture9*	statsTexture = nullptr;			// per sample: light, the occlusion's darkening, the bounce (luma)
	IDirect3DSurface9*	statsSurface = nullptr;
	IDirect3DTexture9*	balanceTexture[2] = {};		// x: the bounce's scale (eased), yzw: the averages
	IDirect3DSurface9*	balanceSurface[2] = {};
	int					balanceIndex = 0;			// the one holding last frame's
	bool				balanceValid = false;
	CachedHandle		paramInterior;
	// UNOFFICIAL 2026-10-14 screen memory (indoors, at night): per cell the light its surfaces showed on screen (ScreenInject).
	IDirect3DTexture9*	gridScreen[2] = {};			// harmonics (A16B16G16R16F, the atlas), ping-pong
	IDirect3DSurface9*	gridScreenSurface[2] = {};
	IDirect3DTexture9*	gridScreenColor[2] = {};	// colour x light
	IDirect3DSurface9*	gridScreenColorSurface[2] = {};
	// UNOFFICIAL 2026-10-22 (lightgrid2) GridWallMemory: per cell the cover of its surfaces per axis, from the screen memory's reads
	// (blocking indoors and at night, where the sun map gives none). Ping-pong with gridScreen (same index).
	bool				gridWallMemory = true;
	IDirect3DTexture9*	gridScreenWalls[2] = {};	// A16B16G16R16F, the atlas
	IDirect3DSurface9*	gridScreenWallsSurface[2] = {};
	bool				gridWallsValid = false;		// the memory at gridScreenIndex holds walls
	CachedHandle		paramGridWalls;
	// UNOFFICIAL 2026-10-22 (lightgrid2) GridLamps: the game's lamps light the grid's surfaces (indoors and at night).
	static const int	GridLampsMax = 32;			// AmbientLight.fx.hlsl GridLampsMax
	float				gridLamps = 4.0f;			// strength (0: off)
	CachedHandle		paramGridLamps, paramGridLampColors, paramGridLampInfo;
	int		SetGridLamps();						// fills NVR_GridLamps*; returns how many
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
	UINT				gridRenderedFrame = 0;		// UNOFFICIAL 2026-10-19: FrameNumber of the last finished grid (Reflections' GridFallback)
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
