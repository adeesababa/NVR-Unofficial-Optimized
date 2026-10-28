#pragma once


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
		float	Sharpness;
		float	MipBias;
	};
	TAASettingsStruct	Settings;

	struct FSRSettingsStruct {
		bool	Enabled;
		float	Sharpness;
		float	WeaponReactive;
		float	RenderScale;
		int		Upscaler;
		int		DebugView;
		bool	LowResEffects;
	};
	FSRSettingsStruct	FSRSettings;

	static bool			IsWorldViewport(const D3DVIEWPORT9& port);
	static bool			UpscaledBeforeEffects();
	static bool			UpscaledThisFrame();
	static void			NoteWorldDraw(bool firstPerson = false);
	static D3DXVECTOR4	RenderScaleConstant;
	D3DXVECTOR2	LowResJitterUV() const;

	struct TAAStruct {
		D3DXVECTOR4	Data;
		D3DXVECTOR4	PrevProjection;
		D3DXVECTOR4	CameraDelta;
		D3DXVECTOR4	Weapon;
		D3DXVECTOR4	Sharpen;
		D3DXMATRIX	PrevViewTransform;
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

	void	BeginJitter(bool firstWorld = true);
	void	EndJitter();
	bool	IsJitterActive() const { return jitterActive; }

	void	UpscaleWorld();
	bool	UpscalePending() const { return Up.Deferred; }
	float	WorldMipBias() const { return worldMipBias; }
	void	UpscaleBeginEffects();
	void	UpscaleAfterEffects(IDirect3DSurface9* GameSurface);

private:
	bool		historyValid = false;
	int			historyIndex = 0;
	D3DXVECTOR4	prevCameraPosition = D3DXVECTOR4(0.0f, 0.0f, 0.0f, 0.0f);

	static const int JitterSequenceLength = 8;
	int			jitterIndex = 0;
	bool		jitterActive = false;
	NiCamera*	jitterCamera = nullptr;
	float		frustumShiftX = 0.0f;
	float		frustumShiftY = 0.0f;
	bool		mipBiasActive = false;
	float		worldMipBias = 0.0f;
	bool		texturesFailed = false;

	bool	EnsureTextures();

	bool	ExtDraw(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, int write);
	ExtPass			extPasses[3];
	ExtUniformBuffer	extUniforms;

	void	SetMipBias(float bias);

	float		frameJitterX = 0.0f;
	float		frameJitterY = 0.0f;
	bool		frameJittered = false;
	bool		lastPathFSR = false;
	bool		FSRWanted();
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
	bool		fsrTried = false;
	bool		fsrFailed = false;
	bool		fsrMissingLogged = false;
	LARGE_INTEGER	fsrLastTime = {};
	UInt32		fsrFrames = 0;

	struct UpscaleStruct {
		bool		Active = false;
		bool		Upscaled = false;
		bool		PortSet = false;
		NiCamera*	Camera = nullptr;
		NiViewport	SavedPort = {};
		UINT		Width = 0, Height = 0;
		UINT		RenderWidth = 0, RenderHeight = 0;
		IDirect3DSurface9*	Target = nullptr;
		IDirect3DSurface9*	DepthStencil = nullptr;
		D3DFORMAT	TargetFormat = D3DFMT_UNKNOWN;
		bool		Hdr = false;
		int			JitterIndex = 0;
		float		JitterPhases = 8.0f;
		float		JitterX = 0.0f, JitterY = 0.0f;
		bool		Jittered = false;
		bool		HistoryValid = false;
		UINT		LastRenderWidth = 0, LastRenderHeight = 0;
		D3DXMATRIX	PrevView;
		D3DXVECTOR4	PrevProjection = D3DXVECTOR4(0, 0, 0, 0);
		D3DXVECTOR4	PrevCameraPosition = D3DXVECTOR4(0, 0, 0, 0);
		LARGE_INTEGER	LastTime = {};
		FSRTexture	Input, Motion, Depth;
		D3DFORMAT	InputFormat = D3DFMT_UNKNOWN;
		ID3D9NvrExtPipeline*	Prep = nullptr;
		bool		PrepTried = false;
		ExtUniformBuffer	PrepUniforms;
		IDirect3DPixelShader9*	CompositePS = nullptr;
		IDirect3DStateBlock9*	CompositeStates = nullptr;
		bool		CompositeTried = false;
		bool		Failed = false;
		bool		Whole = false;
		UInt32		DrawsSmall = 0, DrawsFull = 0, DrawsOther = 0;
		bool		MixedLogged = false;
		UInt32		Frames = 0, SkippedFrames = 0, LoggedSkips = 0;
		float		LoggedScale = 0.0f;
		UINT		LoggedWidth = 0, LoggedHeight = 0;
		int			LastSkip = -1;
		bool		Deferred = false;
		bool		V2Logged = false;
	};
	UpscaleStruct	Up;
	bool	UpscaleBegin(bool firstWorld);
	void	UpscaleSkip(int reason);
	void	UpscaleRestorePort();
	void	UpscaleReleaseTargets();
	bool	UpscaleEnsureResources();
	bool	UpscaleCheckDrawn(IDirect3DDevice9* Device);
	bool	UpscaleRun(IDirect3DDevice9* Device, bool resolveDepth = true);
	bool	UpscaleComposite(IDirect3DDevice9* Device, IDirect3DTexture9* fsrOutput, IDirect3DTexture9* depthSource = nullptr, bool colour = true, bool jittered = true);
	void	UpscaleFinish(bool ok, bool v2);

	bool	WillResolveThisFrame();

	void	RememberCamera();
	bool	DrawTechnique(const char* TechniqueName);
};
