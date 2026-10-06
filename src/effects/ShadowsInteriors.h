#pragma once

class ShadowsInteriorsEffect : public EffectRecord
{
public:
	ShadowsInteriorsEffect() : EffectRecord("ShadowsInteriors") {};

	struct ShadowsInteriorsStruct {
	};
	ShadowsInteriorsStruct	Constants;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();

	void	Render(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface,
		UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;

	bool	dedicatedFailed = false;
};