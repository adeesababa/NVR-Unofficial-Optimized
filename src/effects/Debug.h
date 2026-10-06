#pragma once

class DebugEffect : public EffectRecord
{
public:
	DebugEffect() : EffectRecord("Debug") {};

	struct DebugStruct {
		D3DXVECTOR4				DebugVar;
		D3DXVECTOR4				ClipView;
	};
	DebugStruct	Constants;

	int                     ClipViewMode = 0;
	IDirect3DTexture9*      ClipStartTexture = nullptr;
	IDirect3DSurface9*      ClipStartSurface = nullptr;
	int                     ClipTechnique();
	bool                    ClipViewActive();
	void                    CaptureClipStart(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
	void                    RenderClipView(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget);
	void                    ReleaseClipStart();

	void	UpdateConstants();
	void	RegisterConstants();
	void	RegisterTextures();
	void	UpdateSettings();

};