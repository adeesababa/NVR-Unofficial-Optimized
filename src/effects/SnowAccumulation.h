#pragma once

class SnowAccumulationEffect : public EffectRecord
{
public:
	SnowAccumulationEffect() : EffectRecord("SnowAccumulation") {
		Constants.SnowAccumulationAnimator.Initialize(0);
		Constants.Data.w = 0.0f;
	};

	struct SnowAccumulationStruct {
		Animator		SnowAccumulationAnimator;
		D3DXVECTOR4		Data;
		D3DXVECTOR4		Color;
	};
	SnowAccumulationStruct	Constants;

	float increaseRate;
	float decreaseRate;

	void	UpdateConstants();
	void	RegisterConstants();
	void	UpdateSettings();
	bool	ShouldRender();

	// UNOFFICIAL 2026-10-25 (NVR DXVK): the five passes as src\hlsl\NewVegas\Compute\SnowAccumulation.frag (EffectRecord::ExtRender)
	bool	ExtRender(IDirect3DDevice9* Device, IDirect3DSurface9* RenderTarget, IDirect3DSurface9* RenderedSurface, UINT techniqueIndex, bool ClearRenderTarget, IDirect3DSurface9* SourceBuffer) override;
private:
	ExtPass				extPass;
	ExtUniformBuffer	extUniforms;
};