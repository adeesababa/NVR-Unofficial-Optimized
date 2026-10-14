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

	void	BeginJitter();
	void	EndJitter();
	bool	IsJitterActive() const { return jitterActive; }

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
	bool		texturesFailed = false;

	bool	EnsureTextures();

	void	SetMipBias(float bias);

	bool	WillResolveThisFrame();

	void	RememberCamera();
	bool	DrawTechnique(const char* TechniqueName);
};
