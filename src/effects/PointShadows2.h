#pragma once

class PointShadows2Effect : public EffectRecord
{
public:
	PointShadows2Effect() : EffectRecord("PointShadows2") {};

	struct PointShadows2Struct {
	};
	PointShadows2Struct	Constants;

	void	UpdateConstants() {};
	void	RegisterConstants() {};
	void	UpdateSettings() {};

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass				extPass;
	ExtUniformBuffer	extUniforms;
	IDirect3DTexture9*	copyTexture = nullptr;
	IDirect3DSurface9*	copySurface = nullptr;

};
