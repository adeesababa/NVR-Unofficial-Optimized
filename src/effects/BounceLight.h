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
