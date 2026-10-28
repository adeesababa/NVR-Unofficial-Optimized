#pragma once

class UnderwaterEffect : public EffectRecord
{
public:
	UnderwaterEffect() : EffectRecord("Underwater") {};

	struct UnderwaterStruct {
		D3DXVECTOR4		Data;
	};
	UnderwaterStruct	Constants;

	void	UpdateConstants() {};
	void	RegisterConstants() {};
	void	UpdateSettings() {};
	bool	ShouldRender();

	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
	ExtPass	extGodrays, extBlur, extWater, extDistortion;
	ExtUniformBuffer extUniforms;
}; 