#pragma once

// CREDIT: TAA for New Vegas Reloaded by Conor. See TAA.fx.hlsl for upstream credits.

// Temporal anti-aliasing: temporal accumulation with camera reprojection, plus sub-pixel jitter of
// the world render so a still image gets anti-aliased too. Ported from Oblivion Reloaded E3's TAA;
// see TAA.fx.hlsl for what changed and why, and BeginJitter below for how the jitter is applied.
//
// Two passes per frame, both full resolution:
//   Resolve: current frame + reprojected history (one FP16 buffer) -> the other FP16 buffer
//   Output:  that buffer -> the frame; it is next frame's history
// Resolve cannot write the history it is reading, hence two buffers. UNOFFICIAL 2026-10-13: they take turns (historyIndex)
// instead of the resolve being copied into a fixed history buffer every frame (a full-screen FP16 copy, about 0.15 ms at
// 1440p, for the same result). The history is FP16
// rather than the frame's 8 bits because at a 0.9 blend weight each frame moves the history only
// 10% of the way to the current value, and in 8 bits any difference under about 5/255 rounds
// straight back to the old value -- a ghost that stops fading and never clears.
class TAAEffect : public EffectRecord
{
public:
	TAAEffect() : EffectRecord("TAA") {};

	struct TAASettingsStruct {
		float	HistoryWeight;
		float	ClipGamma;
		int		DebugView;
		bool	Jitter;
		float	WeaponTAA;
		float	Sharpness;		// sharpening of the TAA output (never of the history), 0 off
		float	MipBias;		// texture mip LOD bias while the jittered world draws, 0 off
	};
	TAASettingsStruct	Settings;

	// UNOFFICIAL 2026-10-26 (NVR DXVK, [Shaders.TAA.FSR]): AMD FidelityFX Super Resolution 2.2.1 (MIT, GPUOpen) as TAA's resolve,
	// at native resolution ("native AA"), on TAA's own jitter and camera reprojection. Needs NVR DXVK (compute passes through the
	// extension); without it, or if a pass fails, TAA's own resolve runs as before. See effects/TAAFSR.cpp.
	struct FSRSettingsStruct {
		bool	Enabled;		// FSR 2 replaces TAA's resolve (TAA itself must be on)
		float	Sharpness;		// [0-1] FSR's RCAS sharpening of the output, 0 off
		float	WeaponReactive;	// [0-1] FSR reactive mask on the first-person weapon (it is put back as rendered either way)
		// UNOFFICIAL 2026-10-27 RenderScale (effects/TAAUpscale.cpp): below 1, the world scene is drawn at this fraction of the
		// screen's width and height and FSR 2 upscales it to the screen before NVR's effects, instead of resolving at native
		// resolution after tonemapping. 1 (default) = off: native, exactly as before.
		float	RenderScale;	// [0.5-1]
		int		Upscaler;		// 0 FSR 2, 1 DLSS (not built yet: FSR 2 runs, logged once; core/Upscaler.h)
		int		DebugView;		// RenderScale: 0 off, 1 the low-resolution world as drawn (its corner), 2 it stretched without FSR, 3 FSR's motion input
		bool	LowResEffects;	// UNOFFICIAL 2026-10-27 RenderScale v2: the weapon and NVR's pre-tonemap effects run small too, upscaled after them
	};
	FSRSettingsStruct	FSRSettings;

	// UNOFFICIAL 2026-10-27 RenderScale: [Shaders.TAA.FSR] RenderScale below 1 (effects/TAAUpscale.cpp). Read by code that checks
	// the viewport of world draws (lamp scissor, forward lamp shadows, the scope fix): while the world scene is drawn at the lower
	// internal resolution its viewport is the top-left RenderWidth x RenderHeight of the screen, which these accept like the full one.
	static bool			IsWorldViewport(const D3DVIEWPORT9& port);
	static bool			UpscaledThisFrame();		// this frame's world was drawn at the lower resolution and upscaled (TAA's resolve skips)
	static void			NoteWorldDraw(bool firstPerson = false);	// RenderPassStandard/SkinnedHook, world (and v2: first-person) passes: counts which viewport they drew with (a check)
	static D3DXVECTOR4	RenderScaleConstant;		// TESR_RenderScale: xy = render size / screen size while the low-resolution world draws, else 1
	// UNOFFICIAL 2026-10-28: in an FSR / DLSS LowResEffects frame whose world was jittered, the jitter as a screen-uv offset (a pixel of the
	// small frame shows the unjittered screen at uv + this); else 0. VolumetricClouds' composite reads its clouds there.
	D3DXVECTOR2	LowResJitterUV() const;

	struct TAAStruct {
		D3DXVECTOR4	Data;				// x: history weight, y: clip gamma, z: history valid (0/1), w: debug view (0 = off)
		D3DXVECTOR4	PrevProjection;		// x: last frame's projMatrix._11, y: its _22
		D3DXVECTOR4	CameraDelta;		// xyz: camera position this frame minus last frame's
		D3DXVECTOR4	Weapon;				// x: history weight scale on first-person weapon pixels
		D3DXVECTOR4	Sharpen;			// x: sharpness of the output pass
		D3DXMATRIX	PrevViewTransform;	// last frame's viewMatrix; the shader reads only its rotation
	};
	TAAStruct	Constants;

	struct TAATexturesStruct {
		IDirect3DTexture9*	ResolveTexture = nullptr;
		IDirect3DSurface9*	ResolveSurface = nullptr;
		IDirect3DTexture9*	HistoryTexture = nullptr;
		IDirect3DSurface9*	HistorySurface = nullptr;
	};
	TAATexturesStruct	Textures;

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();

	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer);

	// Bracket the world scene render (RenderWorldSceneGraphHook) with these. Begin shifts the
	// world camera's frustum by this frame's sub-pixel offset; End puts it back. Both are no-ops
	// unless TAA will actually resolve this frame, and End is safe to call when Begin did nothing.
	// UNOFFICIAL 2026-10-27: firstWorld = the frame's first world scene (RenderScale only scales that one).
	void	BeginJitter(bool firstWorld = true);
	void	EndJitter();
	bool	IsJitterActive() const { return jitterActive; }

	// UNOFFICIAL 2026-10-27 RenderScale (effects/TAAUpscale.cpp): right after the world scene (RenderWorldSceneGraphHook, after
	// EndJitter): when this world scene was drawn at the lower internal resolution, FSR 2 upscales it into the world's own target
	// (colour, and the depth buffer nearest-upsampled), so everything after (the game's refraction, the first-person weapon, the
	// image space and every NVR effect) sees a full-resolution picture. A no-op otherwise.
	void	UpscaleWorld();
	// UNOFFICIAL 2026-10-27 RenderScale v2 ([Shaders.TAA.FSR] LowResEffects, docs/next/RENDERSCALE-V2.md): UpscaleWorld only checks
	// the world was drawn small and leaves it (UpscalePending); the first-person weapon is drawn into the same rect; the image-space
	// hook calls UpscaleBeginEffects (RenderRect::Begin) before ShaderManager::RenderEffectsPreTonemapping and UpscaleAfterEffects
	// after it (FSR 2 / DLSS on GameSurface's rect -> the whole of it, depths upsampled and re-resolved, CombineDepth + Normals again
	// at full size, RenderRect::End). UpscaleAfterEffects is also the fallback for frames whose chain did not run (no-op if nothing pends).
	bool	UpscalePending() const { return Up.Deferred; }
	// UNOFFICIAL 2026-10-28: the texture mip bias the frame's first world scene was drawn with (SetMipBias in BeginJitter; 0 = none).
	// The land's tessellation reads it to keep its height lookups at their own detail level ([Shaders.Terrain.Displacement] IgnoreMipBias).
	float	WorldMipBias() const { return worldMipBias; }
	void	UpscaleBeginEffects();
	void	UpscaleAfterEffects(IDirect3DSurface9* GameSurface);

private:
	bool		historyValid = false;
	int			historyIndex = 0;	// which of the two buffers (0 Resolve, 1 History) holds last frame's result
	D3DXVECTOR4	prevCameraPosition = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	static const int JitterSequenceLength = 8;
	int			jitterIndex = 0;
	bool		jitterActive = false;
	NiCamera*	jitterCamera = nullptr;
	float		frustumShiftX = 0.0f;
	float		frustumShiftY = 0.0f;
	bool		mipBiasActive = false;
	float		worldMipBias = 0.0f;	// UNOFFICIAL 2026-10-28 (WorldMipBias)
	bool		texturesFailed = false;

	bool	EnsureTextures();

	// UNOFFICIAL 2026-10-25 (NVR DXVK): the resolve, the debug view and the output as Vulkan passes (src\hlsl\NewVegas\Compute\TAA*.frag);
	// Render uses them when NvrDxvkPasses is on, and the D3D9 techniques otherwise. Writes the history buffer that is not last frame's.
	bool	ExtDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, int write);
	ExtPass			extPasses[3];	// Resolve, Debug, Output
	ExtUniformBuffer	extUniforms;

	void	SetMipBias(float bias);

	// --- FSR 2 at native resolution (effects/TAAFSR.cpp; AMD's passes are core/Upscaler.cpp's since 2026-10-27) ---
	float		frameJitterX = 0.0f;	// this frame's jitter (TAA's pixel offsets, as BeginJitter applied it)
	float		frameJitterY = 0.0f;
	bool		frameJittered = false;
	bool		lastPathFSR = false;	// the history in hand is FSR's (true) or TAA's own (false)
	bool		FSRWanted();			// FSRSettings.Enabled and the extension there and no failure so far
	bool		FSRDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, bool reset);
	bool		FSREnsureResources();
	void		FSRReleaseResources();
	bool		FSRDispatchPrep(const NVR_EXT_RESOURCE_BINDING* bindings, UINT count, const void* push, UINT pushSize, UINT groupsX, UINT groupsY);
	struct FSRTexture { IDirect3DTexture9* Texture = nullptr; IDirect3DSurface9* Surface = nullptr; };
	ID3D9NvrExtPipeline*	fsrPrep = nullptr;
	ExtPass			fsrComposite;
	ExtUniformBuffer	fsrPrepUniforms;
	FSRTexture	fsrMotion, fsrDepth;
	int			fsrWidth = 0;
	int			fsrHeight = 0;
	bool		fsrTried = false;		// pipelines created (or failed) once
	bool		fsrFailed = false;		// FSR is off for the session (logged)
	bool		fsrMissingLogged = false;
	LARGE_INTEGER	fsrLastTime = {};
	UInt32		fsrFrames = 0;

	// --- RenderScale: the world at a lower internal resolution, upscaled by FSR 2 (effects/TAAUpscale.cpp) ---
	struct UpscaleStruct {
		bool		Active = false;			// this frame's world scene is being drawn at the lower resolution (BeginJitter .. UpscaleWorld)
		bool		Upscaled = false;		// UpscaleWorld put the upscaled picture into the frame (TAA's resolve then skips this frame)
		bool		PortSet = false;		// the world camera's viewport port is the render size (EndJitter puts it back)
		NiCamera*	Camera = nullptr;
		NiViewport	SavedPort = {};
		UINT		Width = 0, Height = 0;				// the screen (= the world's targets)
		UINT		RenderWidth = 0, RenderHeight = 0;	// this frame's internal resolution
		IDirect3DSurface9*	Target = nullptr;		// the world's colour target and depth buffer (held from BeginJitter to UpscaleWorld)
		IDirect3DSurface9*	DepthStencil = nullptr;
		D3DFORMAT	TargetFormat = D3DFMT_UNKNOWN;
		bool		Hdr = false;			// the world's target is a float (HDR) target: the FSR passes that care run their HDR build
		int			JitterIndex = 0;
		float		JitterPhases = 8.0f;
		float		JitterX = 0.0f, JitterY = 0.0f;	// this frame's offset in render pixels (as BeginJitter applied it)
		bool		Jittered = false;
		bool		HistoryValid = false;	// last world scene was upscaled too, with the same render size
		UINT		LastRenderWidth = 0, LastRenderHeight = 0;
		D3DXMATRIX	PrevView;				// the camera of the last upscaled frame (motion vectors)
		D3DXVECTOR4	PrevProjection = D3DXVECTOR4(0, 0, 0, 0);
		D3DXVECTOR4	PrevCameraPosition = D3DXVECTOR4(0, 0, 0, 0);
		LARGE_INTEGER	LastTime = {};
		// NVR's inputs (the screen's size; the render size used in their top-left)
		FSRTexture	Input, Motion, Depth;
		D3DFORMAT	InputFormat = D3DFMT_UNKNOWN;
		ID3D9NvrExtPipeline*	Prep = nullptr;
		bool		PrepTried = false;
		ExtUniformBuffer	PrepUniforms;
		IDirect3DPixelShader9*	CompositePS = nullptr;	// D3D9: FSR's output + the depth buffer back into the world's targets
		IDirect3DStateBlock9*	CompositeStates = nullptr;
		bool		CompositeTried = false;
		bool		Failed = false;			// RenderScale is off for the session (logged)
		bool		Whole = false;			// UpscaleRun found the world drawn at full resolution anyway (nothing to put back)
		UInt32		DrawsSmall = 0, DrawsFull = 0, DrawsOther = 0;	// this world scene's passes by viewport (NoteWorldDraw)
		bool		MixedLogged = false;
		UInt32		Frames = 0, SkippedFrames = 0, LoggedSkips = 0;
		float		LoggedScale = 0.0f;
		UINT		LoggedWidth = 0, LoggedHeight = 0;
		int			LastSkip = -1;
		bool		Deferred = false;		// v2: the world (and weapon) drawn small, the upscale waits for the end of the pre-tonemap chain
		bool		V2Logged = false;
	};
	UpscaleStruct	Up;
	bool	UpscaleBegin(bool firstWorld);		// decides RenderScale for this world scene; sets the camera port; true = active
	void	UpscaleSkip(int reason);			// not this frame (counted, the first of each reason logged)
	void	UpscaleRestorePort();				// the world camera's port back (EndJitter)
	void	UpscaleReleaseTargets();
	bool	UpscaleEnsureResources();
	bool	UpscaleCheckDrawn(IDirect3DDevice9* Device);	// the world really was drawn small (else Failed for the session)
	bool	UpscaleRun(IDirect3DDevice9* Device, bool resolveDepth = true);	// the upscale itself (UpscaleWorld; v2: UpscaleAfterEffects, depth already resolved)
	// D3D9: colour + depth into the world's targets. v2: depthSource (default the world's depth) and colour = false (depth only) for
	// the viewmodel depth; jittered = false reads it at its own pixels (the weapon is not jittered).
	bool	UpscaleComposite(IDirect3DDevice9* Device, IDirect3DTexture9* fsrOutput, IDirect3DTexture9* depthSource = nullptr, bool colour = true, bool jittered = true);
	void	UpscaleFinish(bool ok, bool v2);	// after the upscale (or its failure): state off, counted, logged

	bool	WillResolveThisFrame();

	void	RememberCamera();
	bool	DrawTechnique(const char* TechniqueName);
};
