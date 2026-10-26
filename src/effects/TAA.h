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
	};
	FSRSettingsStruct	FSRSettings;

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
	void	BeginJitter();
	void	EndJitter();
	bool	IsJitterActive() const { return jitterActive; }

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
	bool		texturesFailed = false;

	bool	EnsureTextures();

	// UNOFFICIAL 2026-10-25 (NVR DXVK): the resolve, the debug view and the output as Vulkan passes (src\hlsl\NewVegas\Compute\TAA*.frag);
	// Render uses them when NvrDxvkPasses is on, and the D3D9 techniques otherwise. Writes the history buffer that is not last frame's.
	bool	ExtDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, int write);
	ExtPass			extPasses[3];	// Resolve, Debug, Output
	ExtUniformBuffer	extUniforms;

	void	SetMipBias(float bias);

	// --- FSR 2 (effects/TAAFSR.cpp) ---
	float		frameJitterX = 0.0f;	// this frame's jitter (TAA's pixel offsets, as BeginJitter applied it)
	float		frameJitterY = 0.0f;
	bool		frameJittered = false;
	bool		lastPathFSR = false;	// the history in hand is FSR's (true) or TAA's own (false)
	bool		FSRWanted();			// FSRSettings.Enabled and the extension there and no failure so far
	bool		FSRDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, bool reset);
	bool		FSREnsureResources();
	void		FSRReleaseResources();
	bool		FSRDispatch(int pipeline, const NVR_EXT_RESOURCE_BINDING* bindings, UINT count, const void* push, UINT pushSize, UINT groupsX, UINT groupsY);
	struct FSRTexture { IDirect3DTexture9* Texture = nullptr; IDirect3DSurface9* Surface = nullptr; };
	enum { FSRPrep, FSRLumaMip, FSRReconstruct, FSRDepthClip, FSRLock, FSRAccumulate, FSRAccumulateSharpen, FSRRCAS, FSRPipelineCount };
	ID3D9NvrExtPipeline*	fsrPipelines[FSRPipelineCount] = {};
	ExtPass			fsrComposite;
	ExtUniformBuffer	fsrPrepUniforms;
	ExtUniformBuffer	fsrUniforms;
	ID3D9NvrExtBuffer*	fsrReconstructedDepth = nullptr;	// one uint per pixel (D3D9 has no 32-bit uint render target)
	FSRTexture	fsrMotion, fsrDepth, fsrLumaMip, fsrDilatedMotion[2], fsrDilatedDepth, fsrLockLuma, fsrNewLocks, fsrPrepared,
				fsrLumaHistory[2], fsrReactiveMasks, fsrUpscaled[2], fsrLockStatus[2], fsrOutput;
	int			fsrWidth = 0;
	int			fsrHeight = 0;
	bool		fsrTried = false;		// pipelines created (or failed) once
	bool		fsrFailed = false;		// FSR is off for the session (logged)
	bool		fsrMissingLogged = false;
	int			fsrParity = 0;			// which of the ping-pong resources is this frame's history (AMD's isOddFrame)
	int			fsrFrameIndex = 0;
	LARGE_INTEGER	fsrLastTime = {};
	UInt32		fsrFrames = 0;

	bool	WillResolveThisFrame();

	void	RememberCamera();
	bool	DrawTechnique(const char* TechniqueName);
};
