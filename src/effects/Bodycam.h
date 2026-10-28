#pragma once

class BodycamEffect : public EffectRecord
{
public:
	BodycamEffect() : EffectRecord("Bodycam") {};

	struct BodycamStruct {
		D3DXVECTOR4	Lens;
		D3DXVECTOR4	Image;
		D3DXVECTOR4	Noise;
		D3DXVECTOR4	Text;
		D3DXVECTOR4	Frame;
		D3DXVECTOR4	Motion;
		D3DXVECTOR4	Grade;
		D3DXVECTOR4	Video;
		D3DXVECTOR4	Adapt;
		D3DXVECTOR4	Body;
		D3DXVECTOR4	Flash;
		D3DXVECTOR4	Glitch;
	};
	BodycamStruct	Constants;

	enum LookKey { Fisheye, FitEdges, Border, Fringe, EdgeBlur, LensBlur, Vignette, Sharpen, Exposure, Contrast, Saturation, Lift, Cool,
		Glow, Grain, Blocks, MotionBlur, LensDirt, Tint, Text, VideoFps, AutoExposure, ExposureSpeed, Breakup, RollingShutter,
		Bounce, GunJolt, Overload, LowLight, LightSmear, Hiccups, Autofocus, LookKeys };
	struct LookStruct { float v[LookKeys]; };
	LookStruct		Day = {}, Night = {}, Interiors = {};
	float			LastYaw = 0.0f, LastPitch = 0.0f;
	bool			HaveLast = false;

	static const int TextWidth = 256;
	static const int TextHeight = 12;
	IDirect3DTexture9*	TextTexture = nullptr;
	IDirect3DTexture9*	HoldTexture = nullptr;
	IDirect3DSurface9*	HoldSurface = nullptr;
	D3DSURFACE_DESC		HoldDesc = {};
	bool				HoldValid = false;
	double				HoldTick = -1.0;
	double				LastRender = -1.0;
	IDirect3DTexture9*	MeasureTexture[2] = {};
	IDirect3DSurface9*	MeasureSurface[2] = {};
	IDirect3DTexture9*	MeasureRead = nullptr;
	int					MeasureIndex = 0;
	bool				MeasureValid = false;
	D3DXVECTOR3			LastPos = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	float				MotionLevel = 0.0f;
	float				VideoFrameRate = 0.0f;
	char				Shown[2][65] = {};
	double				Clock = 0.0;
	float				StepPhase = 0.0f, StepAmount = 0.0f;
	D3DXVECTOR3			Jolt = D3DXVECTOR3(0.0f, 0.0f, 0.0f), JoltSpeed = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
	float				TurnRoll = 0.0f;
	const void*			AmmoType = nullptr;
	int					AmmoCount = -1;
	float				FlashNow = 0.0f, DipNow = 0.0f;
	float				HiccupWait = 3.0f, GlitchLeft = 0.0f, FreezeLeft = 0.0f;
	bool				Frozen = false;
	float				WideView = 0.0f;
	static void			ApplyWideView();

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();
	bool	ShouldRender() { return true; };
	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex,
		bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;

private:
	void	WriteText(const char* line1, const char* line2);
	bool	ExtTwoSamplers(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, IDirect3DSurface9* SourceBuffer,
				ExtPass& pass, const char* spvName, IDirect3DBaseTexture9* const* textures, UINT count, const void* push, UINT pushSize, UINT samplerPushOffset);
	ExtPass	extPass, extMeasure;
};
